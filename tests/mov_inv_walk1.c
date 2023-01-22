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

#include "display.h"
#include "error.h"
#include "test.h"

#include "test_funcs.h"
#include "test_helper.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int test_mov_inv_walk1(int my_cpu, int iterations, int offset, bool inverse)
{
    int ticks = 0;

    testword_t pattern = (testword_t)1 << offset;
    pattern = inverse ? ~pattern : pattern;

    if (my_cpu == master_cpu) {
        display_test_pattern_value(pattern);
    }

    // Initialize memory with the initial pattern.
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
                write_word(p, pattern);
                pattern = pattern << 1 | pattern >> (TESTWORD_WIDTH - 1);  // rotate left
            } while (p++ < pe); // test before increment in case pointer overflows
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    // Check for initial pattern and then write the complement for each memory location.
    // Test from bottom up and then from the top down.
    for (int i = 0; i < iterations; i++) {
        pattern = (testword_t)1 << offset;
        pattern = inverse ? ~pattern : pattern;

        flush_caches(my_cpu);

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
                    testword_t expect = pattern;
                    testword_t actual = read_word(p);
                    if (unlikely(actual != expect)) {
                        data_error(p, expect, actual, true);
                    }
                    write_word(p, ~expect);
                    pattern = pattern << 1 | pattern >> (TESTWORD_WIDTH - 1);  // rotate left
                } while (p++ < pe); // test before increment in case pointer overflows
                do_tick(my_cpu);
                BAILOUT;
            } while (!at_end && ++pe); // advance pe to next start point
        }

        pattern = ~pattern;

        flush_caches(my_cpu);

        for (int j = vm_map_size - 1; j >= 0; j--) {
            testword_t *start, *end;
            calculate_chunk(&start, &end, my_cpu, j, sizeof(testword_t));
            if (end < start) SKIP_RANGE(1)  // we need at least one word for this test

            testword_t *p  = end;
            testword_t *ps = end;

            bool at_start = false;
            do {
                // take care to avoid pointer underflow
                if ((ps - start) >= SPIN_SIZE) {
                    ps -= SPIN_SIZE - 1;
                } else {
                    at_start = true;
                    ps = start;
                }
                ticks++;
                if (my_cpu < 0) {
                    continue;
                }
                test_addr[my_cpu] = (uintptr_t)ps;
                do {
                    pattern = pattern >> 1 | pattern << (TESTWORD_WIDTH - 1);  // rotate right
                    testword_t expect = pattern;
                    testword_t actual = read_word(p);
                    if (unlikely(actual != expect)) {
                        data_error(p, expect, actual, true);
                    }
                    write_word(p, ~expect);
                } while (p-- > ps); // test before decrement in case pointer overflows
                do_tick(my_cpu);
                BAILOUT;
            } while (!at_start && --ps); // advance ps to next start point
        }
    }

    return ticks;
}
