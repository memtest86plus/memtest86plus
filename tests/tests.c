// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2026 Sam Demeulemeester.
//
// ------------------------------------------------
// main.c - MemTest-86  Version 3.5
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"

#include "cache.h"
#include "cpuid.h"
#include "memsize.h"
#include "simd.h"
#include "tsc.h"
#include "vmem.h"

#include "barrier.h"

#include "config.h"
#include "display.h"
#include "test.h"

#include "test_funcs.h"
#include "test_helper.h"

#include "tests.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#ifndef TRACE_BARRIERS
#define TRACE_BARRIERS      0
#endif

#define MODULO_N            20

// The test whose description is patched with the SIMD tier by test_list_init().
#define MOV_INV_RNG_TEST    4

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

test_pattern_t test_list[NUM_TEST_PATTERNS] = {
    // ena,  cpu, stgs, itrs, errs, description
    { true,  ONE,    1,    6,    0, "[Address test, walking ones, no cache] "},
    {false,  ONE,    1,    6,    0, "[Address test, own address in window]  "},
    { true,  ONE,    2,    6,    0, "[Address test, own address + window]   "},
    { true,  PAR,    1,    6,    0, "[Moving inversions, 1s & 0s]           "},
    { true,  PAR,    1,  128,    0, "[Moving inversions, random sequence]   "},
    { true,  PAR,    1,    3,    0, "[Moving inversions, 8 bit pattern]     "},
    { true,  PAR,    1,    8,    0, "[Modulo 20, random pattern]            "},
    { true,  PAR,    1,   81,    0, "[Block move]                           "},
#if TESTWORD_WIDTH > 32
    { true,  PAR,    1,    1,    0, "[Moving inversions, 64 bit pattern]    "},
#else
    { true,  PAR,    1,    1,    0, "[Moving inversions, 32 bit pattern]    "},
#endif
    { true,  PAR,    1,   32,    0, "[Bus stress, R/W turnaround, random]   "},
    { true,  PAR,   12,  120,    0, "[Bit fade test, 0s, 1s, random]        "},
};

int ticks_per_pass[NUM_PASS_TYPES];
int ticks_per_test[NUM_PASS_TYPES][NUM_TEST_PATTERNS];

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

#define BARRIER \
    if (my_cpu >= 0) { \
        if (TRACE_BARRIERS) { \
            trace(my_cpu, "Run barrier wait begin at %s line %i", __FILE__, __LINE__); \
        } \
        if (power_save < POWER_SAVE_HIGH) { \
            barrier_spin_wait(run_barrier); \
        } else { \
            barrier_halt_wait(run_barrier); \
        } \
        if (TRACE_BARRIERS) { \
            trace(my_cpu, "Run barrier wait end at %s line %i", __FILE__, __LINE__); \
        } \
    }

void test_list_init(void)
{
    const char *tier_name = simd_tier_name();

    if (tier_name == NULL) {
        // Keep the generic description from the table.
        return;
    }

    // Rewrite the description to show the SIMD tier used by the test,
    // e.g. "[Moving inversions, random (AVX2)]".
    const char *prefix = "[Moving inversions, random (";
    char *desc = test_list[MOV_INV_RNG_TEST].description;

    int i = 0;
    while (prefix[i] != '\0') {
        desc[i] = prefix[i];
        i++;
    }
    for (int j = 0; tier_name[j] != '\0'; j++) {
        desc[i++] = tier_name[j];
    }
    desc[i++] = ')';
    desc[i++] = ']';
    while (i < (int)sizeof(test_list[MOV_INV_RNG_TEST].description) - 1) {
        desc[i++] = ' ';
    }
    desc[i] = '\0';
}

int run_test(int my_cpu, int test, int stage, int iterations)
{
    if (my_cpu == master_cpu) {
        if (window_num == 0) {
            // First window, so we need to test all selected lower memory.
            vm_map[0].start = first_word_mapping(pm_limit_lower);

            // For USB_WORKAROUND.
            if (vm_map[0].start < (uintptr_t *)0x500) {
                vm_map[0].start = (uintptr_t *)0x500;
            }
        }

        /* Update display of memory segments being tested */
        uintptr_t pb = page_of(vm_map[0].start);
        uintptr_t pe = page_of(vm_map[vm_map_size - 1].end) + 1;
        display_test_addresses(pb << 2, pe << 2, num_pages_to_test << 2);
    }
    BARRIER;

    testword_t prsg_state;

    int ticks = 0;

    switch (test) {
        // Address test, walking ones.
      case 0:
        if (my_cpu >= 0) cache_off();
        ticks += test_addr_walk1(my_cpu);
        if (my_cpu >= 0) cache_on();
        BAILOUT;
        break;

        // Address test, own address in window.
      case 1:
        ticks += test_own_addr1(my_cpu);
        BAILOUT;
        break;

        // Address test, own address + window.
      case 2:
        ticks += test_own_addr2(my_cpu, stage);
        BAILOUT;
        break;

        // Moving inversions, all ones and zeros.
      case 3: {
        testword_t pattern1 = 0;
        testword_t pattern2 = ~pattern1;

        BARRIER;
        ticks += test_mov_inv_fixed(my_cpu, iterations, pattern1, pattern2);
        BAILOUT;

        BARRIER;
        ticks += test_mov_inv_fixed(my_cpu, iterations, pattern2, pattern1);
        BAILOUT;
      } break;

        // Moving inversions, pseudo-random sequence (SIMD where available).
        // Every fourth round broadcasts a single random value to all vector
        // lanes, preserving the uniform-background model of the classic
        // random pattern test. Runs early: it has the highest fault
        // detection rate per second, so this minimises time to first fault.
      case 4:
        for (int i = 0; i < iterations; i++) {
            BARRIER;
            ticks += test_mov_inv_rng(my_cpu, (i & 3) == 3);
            BAILOUT;
        }
        break;

        // Moving inversions, 8 bit walking ones and zeros.
      case 5: {
#if TESTWORD_WIDTH > 32
            testword_t pattern1 = UINT64_C(0x8080808080808080);
#else
            testword_t pattern1 = 0x80808080;
#endif
        for (int i = 0; i < 8; i++) {
            testword_t pattern2 = ~pattern1;

            BARRIER;
            ticks += test_mov_inv_fixed(my_cpu, iterations, pattern1, pattern2);
            BAILOUT;

            BARRIER;
            ticks += test_mov_inv_fixed(my_cpu, iterations, pattern2, pattern1);
            BAILOUT;

            pattern1 >>= 1;
        }
      } break;

        // Modulo 20 check, fixed random pattern. Runs before the long
        // shifting pattern test: it is the only test immune to cache
        // masking, so every fault class has been probed early in the pass.
      case 6:
        if (cpuid_info.flags.rdtsc) {
            prsg_state = get_tsc();
        } else {
            prsg_state = 1 + pass_num;
        }
        prsg_state *= 0x87654321;

        for (int i = 0; i < iterations; i++) {
            for (int offset = 0; offset < MODULO_N; offset++) {
                prsg_state = prsg(prsg_state);

                testword_t pattern1 = prsg_state;
                testword_t pattern2 = ~pattern1;

                BARRIER;
                ticks += test_modulo_n(my_cpu, 2, pattern1, pattern2, MODULO_N, offset);
                BAILOUT;

                BARRIER;
                ticks += test_modulo_n(my_cpu, 2, pattern2, pattern1, MODULO_N, offset);
                BAILOUT;
            }
        }
        break;

        // Block move.
      case 7:
        ticks += test_block_move(my_cpu, iterations);
        BAILOUT;
        break;

        // Moving inversions, 32/64 bit shifting pattern. A single iteration
        // per pass suffices: the patterns are deterministic, so repeating
        // them within a pass adds nothing that the next pass doesn't. On the
        // fast first pass only every other offset is walked; full coverage
        // is restored on every full pass.
      case 8: {
        if (iterations < 1) {
            iterations = 1;     // the first pass divides the table value by 3
        }
        int offset_step = (pass_num == 0) ? 2 : 1;
        for (int offset = 0; offset < TESTWORD_WIDTH; offset += offset_step) {
            BARRIER;
            ticks += test_mov_inv_walk1(my_cpu, iterations, offset, false);
            BAILOUT;

            BARRIER;
            ticks += test_mov_inv_walk1(my_cpu, iterations, offset, true);
            BAILOUT;
        }
      } break;

        // Bus stress: NT-write/read burst interleave between two half-chunk streams, duty-cycled
        // on odd rounds to provoke PMIC load steps. Runs before bit fade so the DIMMs enter it hot.
      case 9:
        if (iterations < 1) {
            iterations = 1;     // the first pass divides the table value by 3
        }
        for (int i = 0; i < iterations; i++) {
            BARRIER;
            ticks += test_bus_stress(my_cpu, i);
            BAILOUT;
        }
        break;

        // Bit fade test: four fill/fade/check rounds - solid zeros, solid ones, then an
        // address-seeded random pattern and its complement. `iterations` = fade seconds per round.
      case 10:
        ticks += test_bit_fade(my_cpu, stage, iterations);
        BAILOUT;
        break;
    }
    return ticks;
}
