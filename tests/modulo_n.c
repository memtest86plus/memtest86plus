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

int test_modulo_n(int my_cpu, int iterations, testword_t pattern1, testword_t pattern2, int n, int offset)
{
    int ticks = 0;

    if (my_cpu == master_cpu) {
        display_test_pattern_values(pattern1, offset);
    }

    // Write every nth location with pattern1.
    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start, *end;
        calculate_chunk(&start, &end, my_cpu, i, sizeof(testword_t));
        if ((end - start) < (n - 1)) SKIP_RANGE(1)  // we need at least n words for this test
        end -= n;  // avoids pointer overflow when incrementing p

        testword_t *p  = start + offset;  // we assume each chunk has at least 'n' words, so this won't overflow
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
                write_word(p, pattern1);
            } while (p <= (pe - n) && (p += n)); // test before increment in case pointer overflows
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    // Write the rest of memory "iteration" times with pattern2.
    for (int i = 0; i < iterations; i++) {
        for (int j = 0; j < vm_map_size; j++) {
            testword_t *start, *end;
            calculate_chunk(&start, &end, my_cpu, j, sizeof(testword_t));
            if ((end - start) < (n - 1)) SKIP_RANGE(1)  // we need at least n words for this test

            int k = 0;
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
                    if (k != offset) {
                        write_word(p, pattern2);
                    }
                    k++;
                    if (k == n) {
                        k = 0;
                    }
                } while (p++ < pe); // test before increment in case pointer overflows
                do_tick(my_cpu);
                BAILOUT;
            } while (!at_end && ++pe); // advance pe to next start point
        }
    }

    flush_caches(my_cpu);

    // Now check every nth location.
    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start, *end;
        calculate_chunk(&start, &end, my_cpu, i, sizeof(testword_t));
        if ((end - start) < (n - 1)) SKIP_RANGE(1)  // we need at least n words for this test
        end -= n;  // avoids pointer overflow when incrementing p

        testword_t *p  = start + offset;  // we assume each chunk has at least 'offset' words, so this won't overflow
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
                testword_t actual = read_word(p);
                if (unlikely(actual != pattern1)) {
                    data_error(p, pattern1, actual, true);
                }
            } while (p <= (pe - n) && (p += n)); // test before increment in case pointer overflows
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    return ticks;
}
