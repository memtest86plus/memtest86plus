// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2022 Sam Demeulemeester.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "acpi.h"
#include "cpuid.h"
#include "cpuinfo.h"
#include "io.h"
#include "tsc.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define PIT_TICKS_50mS      59659       // PIT clock is 1.193182MHz
#define APIC_TICKS_50mS     178977      // APIC clock is 3.579545MHz
#define BENCH_MIN_START_ADR 0x1000000   // 16MB

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static inline void setup_pit() {
    /* Set the gate high, disable speaker. */
    outb((inb(0x61) & ~0x02) | 0x01, 0x61);

    /**
     * 10       = Channel #2
     *   11     = Access mode: lobyte/hibyte
     *     000  = Mode 0: Interrupt On Terminal Count
     *        0 = 16-bit binary
     *
     * 10110010 = 0xb0
     */
    outb(0xb0, 0x43);
    outb(PIT_TICKS_50mS & 0xff, 0x42);
    outb(PIT_TICKS_50mS >> 8, 0x42);
}

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
    setup_pit();

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

static void calculate_loops_per_usec(void) {
    // For accuracy, uint64_t has to be used both here and in usleep().
    uint64_t loops_per_50ms = 0;
    uint64_t increment;
    uint32_t step = 0;

    /**
     * Start with an initial estimate. This is going to be significantly
     * underestimated due to extra cycles taken by executing and evaluating
     * inb() but should be a good starting point.
     */
    setup_pit();
    do {
       loops_per_50ms++;
    } while ((inb(0x61) & 0x20) == 0);

    // Fast systems could benefit from a larger initial value.
    increment = loops_per_50ms = loops_per_50ms * 8;

    /**
     * Now continue incremeneting count until we execute a loop that ends
     * when PIT has already triggered. Then execute binary search until
     * increment is lower than we care about.
     */
    for(;;) {
        volatile uint64_t count = loops_per_50ms;
        uint8_t pit_state;

        setup_pit();
        while (count > 0) {
            count--;
        }
        pit_state = inb(0x61);

        if (!(step % 2) ^ !(pit_state & 0x20)) {
            increment = increment / 2;

            // Stop if we are below the accuracy threshold.
            if (increment < 25000)
                break;

            step++;
        }

        if (step % 2)
            loops_per_50ms -= increment;
        else
            loops_per_50ms += increment;
    }

    // Calculate the loop count. Add 1 for rounding up.
    loops_per_usec = (loops_per_50ms / 50000) + 1;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void timers_init(void)
{
    correct_tsc();

    // Calculate loops_per_usec for the busy loop if TSC is not available.
    if (!clks_per_msec) {
        calculate_loops_per_usec();
    }

}
