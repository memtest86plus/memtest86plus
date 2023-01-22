// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from an extract of memtest86+ test.c:
//
// MemTest86+ V5 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.canardpc.com - http://www.memtest.org
// Thanks to Passmark for calculate_chunk() and various comments !
// ----------------------------------------------------
// test.c - MemTest-86  Version 3.4
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include "cpuid.h"
#include "tsc.h"

#include "display.h"
#include "error.h"
#include "test.h"

#include "test_funcs.h"
#include "test_helper.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int test_mov_inv_random(int my_cpu)
{
    int ticks = 0;

    testword_t seed;
    if (cpuid_info.flags.rdtsc) {
        seed = get_tsc();
    } else {
        seed = 1 + pass_num;
    }
    seed *= 0x87654321;

    if (my_cpu == master_cpu) {
        display_test_pattern_value(seed);
    }

    // Initialize memory with the initial pattern.
    testword_t prsg_state = seed;
    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start, *end;
        calculate_chunk(&start, &end, my_cpu, i, sizeof(testword_t));
        if (end < start) SKIP_RANGE(1)  // we need at least one word for this test

        testword_t *p  = start;
        testword_t *pe = start;

        bool at_end = false;
        do {
            // take care to avoid pointer overflow
            if ((end - pe) >= SPIN_SIZE) {
                pe += SPIN_SIZE - 1;
            } else {
                at_end = true;
                pe = end;
            }
            ticks++;
            if (my_cpu < 0) {
                continue;
            }
            test_addr[my_cpu] = (uintptr_t)p;
            do {
                prsg_state = prsg(prsg_state);
                write_word(p, prsg_state);
            } while (p++ < pe); // test before increment in case pointer overflows
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    // Check for initial pattern and then write the inverse pattern for each
    // memory location. Repeat.
    testword_t invert = 0;
    for (int i = 0; i < 2; i++) {
        flush_caches(my_cpu);

        prsg_state = seed;
        for (int j = 0; j < vm_map_size; j++) {
            testword_t *start, *end;
            calculate_chunk(&start, &end, my_cpu, j, sizeof(testword_t));
            if (end < start) SKIP_RANGE(1)  // we need at least one word for this test

            testword_t *p  = start;
            testword_t *pe = start;

            bool at_end = false;
            do {
                // take care to avoid pointer overflow
                if ((end - pe) >= SPIN_SIZE) {
                    pe += SPIN_SIZE - 1;
                } else {
                    at_end = true;
                    pe = end;
                }
                ticks++;
                if (my_cpu < 0) {
                    continue;
                }
                test_addr[my_cpu] = (uintptr_t)p;
                do {
                    prsg_state = prsg(prsg_state);
                    testword_t expect = prsg_state ^ invert;
                    testword_t actual = read_word(p);
                    if (unlikely(actual != expect)) {
                        data_error(p, expect, actual, true);
                    }
                    write_word(p, ~expect);
                } while (p++ < pe); // test before increment in case pointer overflows
                do_tick(my_cpu);
                BAILOUT;
            } while (!at_end && ++pe); // advance pe to next start point
        }
        invert = ~invert;
    }

    return ticks;
}
