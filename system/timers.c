// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2026 Sam Demeulemeester.
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "acpi.h"
#include "cpuid.h"
#include "cpuinfo.h"
#include "io.h"
#include "tsc.h"

#if defined(__loongarch_lp64)
// LoongArch GCC builtin function
#include <larchintrin.h>
#include "registers.h"
#endif

#if defined(__aarch64__)
#include "registers.h"
#endif

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define PIT_TICKS_50mS      59659       // PIT clock is 1.193182MHz
#define APIC_TICKS_50mS     178977      // APIC clock is 3.579545MHz

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

#if defined(__i386__) || defined(__x86_64__)
static void correct_tsc(void)
{
    uint32_t start_time, end_time, run_time, counter;
    int loops = 0;

    if (cpuid_info.flags.rdtsc == 0) {
        return;
    }

    // If available, use APIC Timer to find TSC correction factor
    if (acpi_config.pm_is_io && acpi_config.pm_addr != 0) {
        rdtscl(start_time);

        counter = inl(acpi_config.pm_addr);

        // Generate a dirty delay
        for(volatile uint8_t i=0; i<100u; i++);

        // Make sure counter is incrementing
        if (inl(acpi_config.pm_addr) > counter) {

            while (1) {
                if (inl(acpi_config.pm_addr) > (counter + APIC_TICKS_50mS) || loops > 1000000) {
                    break;
                }
                loops++;
            }

            rdtscl(end_time);

            run_time = end_time - start_time;

            // Make sure we have a credible result
            if (loops >= 10 && run_time >= 50000) {
               clks_per_msec = run_time / 50;
               return;
            }
        }
    }

    // Use PIT Timer to find TSC correction factor if APIC not available
    outb((inb(0x61) & ~0x02) | 0x01, 0x61);
    outb(0xb0, 0x43);
    outb(PIT_TICKS_50mS & 0xff, 0x42);
    outb(PIT_TICKS_50mS >> 8, 0x42);

    rdtscl(start_time);

    loops = 0;
    do {
        loops++;
    } while ((inb(0x61) & 0x20) == 0);

    rdtscl(end_time);

    run_time = end_time - start_time;

    // Make sure we have a credible result
    if (loops >= 4 && run_time >= 50000) {
       clks_per_msec = run_time / 50;
    }
}
#elif defined(__loongarch_lp64)
static void correct_tsc(void)
{
    uint64_t start, end, excepted_ticks, current_ticks, calc_base_freq, clock_multiplier, clock_divide;
    uint64_t num_millisec = 50, millisec_div = 1000;

    //
    // Enable PMCNT0.
    //
    __csrxchg_d((0x1 << 16), (0x1 << 16) | 0x3FF, LOONGARCH_CSR_PERFCTRL0);

    //
    // Get stable count frequency
    //
    calc_base_freq   = __cpucfg(0x4);
    clock_multiplier = __cpucfg(0x5) & 0xFFFF;
    clock_divide     = (__cpucfg(0x5) >> 16) & 0xFFFF;

    excepted_ticks = (((calc_base_freq * clock_multiplier) / clock_divide) * num_millisec) / millisec_div;

    __asm__ __volatile__("rdtime.d %0, $zero":"=r"(current_ticks):);
    excepted_ticks += current_ticks;

    start = __csrrd_d(LOONGARCH_CSR_PERFCNTR0);
    do {
      __asm__ __volatile__("rdtime.d %0, $zero":"=r"(current_ticks):);
    } while (current_ticks < excepted_ticks);
    end = __csrrd_d(LOONGARCH_CSR_PERFCNTR0);

    //
    // Disable PMCNT0.
    //
    __csrxchg_d(0, (0x1 << 16) | 0x3FF, LOONGARCH_CSR_PERFCTRL0);

    clks_per_msec = (end - start) / num_millisec;
}
#elif defined(__aarch64__)
static void correct_tsc(void)
{
    // The generic timer has a known, constant frequency, and get_tsc()
    // returns the generic timer count, so all time calculations must use
    // the generic timer frequency. Note this is NOT the CPU clock frequency
    // on this architecture (typically 19.2MHz - 1GHz).
    uint64_t timer_freq = read_sysreg(cntfrq_el0);
    uint32_t ticks_per_msec = timer_freq / 1000;

    clks_per_msec = ticks_per_msec;

    // Use the PMU cycle counter to measure the actual CPU clock frequency
    // over a 50ms interval. This is only used for display purposes.
    uint64_t num_millisec = 50;

    write_sysreg(read_sysreg(pmcr_el0) | 0x5, pmcr_el0);    // enable, reset cycle counter
    write_sysreg(UINT64_C(1) << 31, pmcntenset_el0);        // enable the cycle counter
    write_sysreg(0, pmccfiltr_el0);                         // count cycles at EL1
    __asm__ __volatile__ ("isb");

    uint64_t expected_ticks, current_ticks;
    __asm__ __volatile__ ("isb; mrs %0, cntvct_el0" : "=r" (current_ticks));
    expected_ticks = current_ticks + ticks_per_msec * num_millisec;

    uint64_t start = read_sysreg(pmccntr_el0);
    do {
        __asm__ __volatile__ ("isb; mrs %0, cntvct_el0" : "=r" (current_ticks));
    } while (current_ticks < expected_ticks);
    uint64_t end = read_sysreg(pmccntr_el0);

    write_sysreg(read_sysreg(pmcr_el0) & ~UINT64_C(1), pmcr_el0);   // disable the PMU

    cpu_clk_mhz = (end - start) / num_millisec / 1000;

    // If the PMU is unimplemented or not counting, leave the CPU clock
    // frequency unknown rather than displaying the timer frequency.
    if (cpu_clk_mhz <= ticks_per_msec / 1000) {
        cpu_clk_mhz = 0;
    }
}
#endif

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void timers_init(void)
{
    correct_tsc();
}
