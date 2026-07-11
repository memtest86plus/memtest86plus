// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2026 Sam Demeulemeester.
//
// Bit fade test with solid and random patterns.
//
// Memory is filled, left to fade for a number of seconds, then checked.
// Four rounds per pass: solid zeros, solid ones, then an address-seeded
// random pattern and its complement - probing both fade polarities in one
// delay, stressing cells with opposing-charge neighbours, exposing data
// movement during the fade, and testing a fresh configuration every pass.
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

#include <stdbool.h>
#include <stdint.h>

#include "unistd.h"

#include "vmem.h"

#include "display.h"
#include "error.h"
#include "test.h"

#include "test_funcs.h"
#include "test_helper.h"

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

// Mixes the address and seed into a pattern word (SplitMix64 / fmix32). Must
// be non-linear: the GF(2)-linear prsg() would repeat the pattern every pass.
static inline testword_t fade_mix(testword_t x)
{
#if TESTWORD_WIDTH > 32
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
#else
    x ^= x >> 16;
    x *= 0x85ebca6b;
    x ^= x >> 13;
    x *= 0xc2b2ae35;
    x ^= x >> 16;
#endif
    return x;
}

// Returns the seed for the random rounds: a fresh value every pass, identical
// on every CPU and every run, so pass N's patterns are exactly reproducible.
static inline testword_t fade_seed(void)
{
#if TESTWORD_WIDTH > 32
    return fade_mix((1 + (testword_t)pass_num) * UINT64_C(0x9e3779b97f4a7c15));
#else
    return fade_mix((1 + (testword_t)pass_num) * 0x9e3779b9);
#endif
}

// Returns the offset that makes the fade_mix() input stable across the fill
// and check stages (on 64-bit builds, the physical byte address).
static testword_t fade_addr_offset(void)
{
    testword_t offset;

    // Calculate the offset (in pages) between the virtual address and the physical address.
    offset = (vm_map[0].pm_base_addr / VM_WINDOW_SIZE) * VM_WINDOW_SIZE;
    offset = (offset >= VM_PINNED_SIZE) ? offset - VM_PINNED_SIZE : 0;
#if (ARCH_BITS == 64)
    // Convert to a byte address offset. This will translate the virtual address into a physical address.
    offset *= PAGE_SIZE;
#else
    // Convert to a VM window offset. This will get added into the LSBs of the virtual address.
    offset /= VM_WINDOW_SIZE;
#endif

    return offset;
}

static int pattern_fill(int my_cpu, int round)
{
    int ticks = 0;

    bool       random = (round >= 2);
    testword_t invert = (round & 1) ? ~(testword_t)0 : 0;

    testword_t seed   = 0;
    testword_t offset = 0;
    if (random) {
        seed   = fade_seed();
        offset = fade_addr_offset();
    }

    if (my_cpu == master_cpu) {
        display_test_pattern_value(random ? (seed ^ invert) : invert);
    }

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
            if (random) {
                do {
                    write_word(p, fade_mix(((testword_t)(uintptr_t)p + offset) ^ seed) ^ invert);
                } while (p++ < pe); // test before increment in case pointer overflows
            } else {
                do {
                    write_word(p, invert);
                } while (p++ < pe); // test before increment in case pointer overflows
            }
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    // Every CPU flushes its own caches: the fill must reach DRAM before the
    // fade delay starts.
    flush_caches_all(my_cpu);

    return ticks;
}

static int pattern_check(int my_cpu, int round)
{
    int ticks = 0;

    bool       random = (round >= 2);
    testword_t invert = (round & 1) ? ~(testword_t)0 : 0;

    testword_t seed   = 0;
    testword_t offset = 0;
    if (random) {
        seed   = fade_seed();
        offset = fade_addr_offset();
    }

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
                testword_t expect = random
                    ? fade_mix(((testword_t)(uintptr_t)p + offset) ^ seed) ^ invert
                    : invert;
                testword_t actual = read_word(p);
                if (unlikely(actual != expect)) {
                    data_error(p, expect, actual, true);
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
        // Only the master busy-waits; the other CPUs go straight to the
        // tick barrier and halt or spin there, per the power policy.
        if (my_cpu == master_cpu) {
            sleep(1);
        }
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
    static int last_stage = -1;

    int ticks = 0;

    // Four rounds of three stages each: fill, fade, check.
    int round = stage / 3;

    switch (stage % 3) {
      case 0:
        ticks = pattern_fill(my_cpu, round);
        break;
      case 1:
        // Only sleep once.
        if (stage != last_stage) {
            ticks = fade_delay(my_cpu, sleep_secs);
        }
        break;
      case 2:
        ticks = pattern_check(my_cpu, round);
        break;
      default:
        break;
    }
    last_stage = stage;

    return ticks;
}
