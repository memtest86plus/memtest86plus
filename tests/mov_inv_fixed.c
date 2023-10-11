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

#define HAND_OPTIMISED  1   // Use hand-optimised assembler code for performance.

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int test_mov_inv_fixed(int my_cpu, int iterations, testword_t pattern1, testword_t pattern2)
{
    int ticks = 0;

    if (my_cpu == master_cpu) {
        display_test_pattern_value(pattern1);
    }

    // Initialize memory with the initial pattern.
    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start, *end;
        calculate_chunk(&start, &end, my_cpu, i, sizeof(testword_t));
        if (end < start) continue;  // we need at least one word for this test

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
#if HAND_OPTIMISED
#ifdef __x86_64__
            uint64_t length = pe - p + 1;
            __asm__  __volatile__ ("\t"
                "rep    \n\t"
                "stosq  \n\t"
                :
                : "c" (length), "D" (p), "a" (pattern1)
                :
            );
            p += length;
#else
            uint32_t length = pe - p + 1;
            __asm__  __volatile__ ("\t"
                "rep    \n\t"
                "stosl  \n\t"
                :
                : "c" (length), "D" (p), "a" (pattern1)
                :
            );
            p += length;
#endif
#else
            do {
                write_word(p, pattern1);
            } while (p++ < pe); // test before increment in case pointer overflows
#endif
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    // Check for the current pattern and then write the alternate pattern for
    // each memory location. Test from the bottom up and then from the top down.
    for (int i = 0; i < iterations; i++) {
        flush_caches(my_cpu);

        for (int j = 0; j < vm_map_size; j++) {
            testword_t *start, *end;
            calculate_chunk(&start, &end, my_cpu, j, sizeof(testword_t));
            if (end < start) continue;  // we need at least one word for this test

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
                    testword_t actual = read_word(p);
                    if (unlikely(actual != pattern1)) {
                        data_error(p, pattern1, actual, true);
                    }
                    write_word(p, pattern2);
                } while (p++ < pe); // test before increment in case pointer overflows
                do_tick(my_cpu);
                BAILOUT;
            } while (!at_end && ++pe); // advance pe to next start point
        }

        flush_caches(my_cpu);

        for (int j = vm_map_size - 1; j >= 0; j--) {
            testword_t *start, *end;
            calculate_chunk(&start, &end, my_cpu, j, sizeof(testword_t));
            if (end < start) continue;  // we need at least one word for this test

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
                test_addr[my_cpu] = (uintptr_t)p;
                do {
                    testword_t actual = read_word(p);
                    if (unlikely(actual != pattern2)) {
                        data_error(p, pattern2, actual, true);
                    }
                    write_word(p, pattern1);
                } while (p-- > ps); // test before decrement in case pointer overflows
                do_tick(my_cpu);
                BAILOUT;
            } while (!at_start && --ps); // advance ps to next start point
        }
    }

    return ticks;
}
