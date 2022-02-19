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

#include "unistd.h"

#include "display.h"
#include "error.h"
#include "test.h"

#include "test_funcs.h"
#include "test_helper.h"

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static int pattern_fill(int my_cpu, testword_t pattern)
{
    int ticks = 0;

    if (my_cpu == master_cpu) {
        display_test_pattern_value(pattern);
    }

    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start = vm_map[i].start;
        testword_t *end   = vm_map[i].end;

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
            } while (p++ < pe); // test before increment in case pointer overflows
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    flush_caches(my_cpu);

    return ticks;
}

static int pattern_check(int my_cpu, testword_t pattern)
{
    int ticks = 0;

    for (int i = 0; i < vm_map_size; i++) {
        testword_t *start = vm_map[i].start;
        testword_t *end   = vm_map[i].end;

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
                if (unlikely(actual != pattern)) {
                    data_error(p, pattern, actual, true);
                }
            } while (p++ < pe); // test before increment in case pointer overflows
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    return ticks;
}

static int fade_delay(int my_cpu, int sleep_secs)
{
    int ticks = 0;

    if (my_cpu == master_cpu) {
        display_test_stage_description("fade over %i seconds", sleep_secs);
    }
    while (sleep_secs > 0) {
        sleep_secs--;
        ticks++;
        if (my_cpu < 0) {
            continue;
        }
        sleep(1);
        do_tick(my_cpu);
        BAILOUT;
    }

    return ticks;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int test_bit_fade(int my_cpu, int stage, int sleep_secs)
{
    const testword_t all_zero = 0;
    const testword_t all_ones = ~all_zero;

    static int last_stage = -1;

    int ticks = 0;

    switch (stage) {
      case 0:
        ticks = pattern_fill(my_cpu, all_zero);
        break;
      case 1:
        // Only sleep once.
        if (stage != last_stage) {
            ticks = fade_delay(my_cpu, sleep_secs);
        }
        break;
      case 2:
        ticks = pattern_check(my_cpu, all_zero);
        break;
      case 3:
        ticks = pattern_fill(my_cpu, all_ones);
        break;
      case 4:
        // Only sleep once.
        if (stage != last_stage) {
            ticks = fade_delay(my_cpu, sleep_secs);
        }
        break;
      case 5:
        ticks = pattern_check(my_cpu, all_ones);
        break;
      default:
        break;
    }
    last_stage = stage;

    return ticks;
}
