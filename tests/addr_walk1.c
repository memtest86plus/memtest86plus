// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from an extract of memtest86+ test.c:
//
// MemTest86+ V5 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.canardpc.com - http://www.memtest.org
// ----------------------------------------------------
// test.c - MemTest-86  Version 3.4
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdint.h>

#include "display.h"
#include "error.h"
#include "test.h"

#include "test_funcs.h"
#include "test_helper.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int test_addr_walk1(int my_cpu)
{
    int ticks = 0;

    // There isn't a meaningful address for this test.
    test_addr[my_cpu] = 0;

    testword_t invert = 0;
    for (int i = 0; i < 2; i++) {
        if (my_cpu == master_cpu) {
            display_test_pattern_value(invert);
        }
        ticks++;
        if (my_cpu < 0) {
            continue;
        }

        for (int j = 0; j < vm_map_size; j++) {
            uintptr_t pb = (uintptr_t)vm_map[j].start;
            uintptr_t pe = (uintptr_t)vm_map[j].end;

            // Walking one on our first address.
            uintptr_t mask1 = sizeof(testword_t);
            do {
                testword_t *p1 = (testword_t *)(pb | mask1);
                mask1 <<= 1;
                if (p1 > (testword_t *)pe) {
                    break;
                }
                testword_t expect = invert ^ (testword_t)p1;
                write_word(p1, expect);

                // Walking one on our second address.
                uintptr_t mask2 = sizeof(testword_t);
                do {
                    testword_t *p2 = (testword_t *)(pb | mask2);
                    mask2 <<= 1;
                    if (p2 == p1) {
                        continue;
                    }
                    if (p2 > (testword_t *)pe) {
                        break;
                    }
                    write_word(p2, ~invert ^ (testword_t)p2);

                    testword_t actual = read_word(p1);
                    if (unlikely(actual != expect)) {
                        addr_error(p1, p2, expect, actual);
                        write_word(p1, expect);  // recover from error
                    }
                } while (mask2);

            } while (mask1);
        }

        invert = ~invert;

        do_tick(my_cpu);
        BAILOUT;
    }

    return ticks;
}
