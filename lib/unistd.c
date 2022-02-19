// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.

#include <stdint.h>

#include "cpuinfo.h"
#include "tsc.h"

#include "unistd.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void usleep(unsigned int usec)
{
    if (clks_per_msec > 0) {
        // If we've measured the CPU speed, we know the TSC is available.
        uint64_t cycles = ((uint64_t)usec * clks_per_msec) / 1000;
        uint64_t t0 = get_tsc();
        do {
            __builtin_ia32_pause();
        } while ((get_tsc() - t0) < cycles);
    } else {
        // This will be highly inaccurate, but should give at least the requested delay.
        volatile uint64_t count = (uint64_t)usec * 1000;
        while (count > 0) {
            count--;
        }
    }
}

void sleep(unsigned int sec)
{
    while (sec > 0) {
        usleep(1000000);
        sec--;
    }
}
