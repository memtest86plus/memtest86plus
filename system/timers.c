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

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

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

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void timers_init(void)
{
    correct_tsc();
}
