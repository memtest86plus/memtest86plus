// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2021 Martin Whitaker.
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
// Private Functions
//------------------------------------------------------------------------------

static int pattern_fill(int my_vcpu, testword_t offset)
{
    int ticks = 0;

    if (my_vcpu == master_vcpu) {
        display_test_pattern_name("own address");
    }

    // Write each address with it's own address.
    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start = vm_map[i].start;
        testword_t *end   = vm_map[i].end;

        volatile testword_t *p  = start;
        volatile testword_t *pe = start;

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
            if (my_vcpu < 0) {
                continue;
            }
            test_addr[my_vcpu] = (uintptr_t)p;
            do {
                write_word(p, (testword_t)p + offset);
            } while (p++ < pe); // test before increment in case pointer overflows
            do_tick(my_vcpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    flush_caches(my_vcpu);

    return ticks;
}

static int pattern_check(int my_vcpu, testword_t offset)
{
    int ticks = 0;

    // Check each address has its own address.
    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start = vm_map[i].start;
        testword_t *end   = vm_map[i].end;

        volatile testword_t *p  = start;
        volatile testword_t *pe = start;

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
            if (my_vcpu < 0) {
                continue;
            }
            test_addr[my_vcpu] = (uintptr_t)p;
            do {
                testword_t expect = (testword_t)p + offset;
                testword_t actual = read_word(p);
                if (unlikely(actual != expect)) {
                    data_error(p, expect, actual, true);
                }
            } while (p++ < pe); // test before increment in case pointer overflows
            do_tick(my_vcpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    return ticks;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int test_own_addr1(int my_vcpu)
{
    int ticks = 0;

    ticks += pattern_fill(my_vcpu, 0);
    ticks += pattern_check(my_vcpu, 0);

    return ticks;
}

int test_own_addr2(int my_vcpu, int stage)
{
    static testword_t offset = 0;
    static int last_stage = -1;

    int ticks = 0;

    offset = (stage == last_stage) ? offset + 1 : 1;

    switch (stage) {
      case 0:
        ticks = pattern_fill(my_vcpu, offset);
        break;
      case 1:
        ticks = pattern_check(my_vcpu, offset);
        break;
      default:
        break;
    }

    last_stage = stage;
    return ticks;
}
