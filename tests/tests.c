// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from an extract of memtest86+ main.c:
//
// MemTest86+ V5 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.canardpc.com - http://www.memtest.org
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

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

test_pattern_t test_list[NUM_TEST_PATTERNS] = {
    // ena,  cpu, stgs, itrs, errs, description
    { true,  SEQ,    1,    6,    0, "[Address test, walking ones, no cache] "},
    { true,  SEQ,    1,    6,    0, "[Address test, own address in window]  "},
    { true,  SEQ,    2,    6,    0, "[Address test, own address + window]   "},
    { true,  PAR,    1,    6,    0, "[Moving inversions, 1s & 0s]           "},
    { true,  PAR,    1,    3,    0, "[Moving inversions, 8 bit pattern]     "},
    { true,  PAR,    1,   30,    0, "[Moving inversions, random pattern]    "},
#if TESTWORD_WIDTH > 32
    { true,  PAR,    1,    3,    0, "[Moving inversions, 64 bit pattern]    "},
#else
    { true,  PAR,    1,    3,    0, "[Moving inversions, 32 bit pattern]    "},
#endif
    { true,  PAR,    1,   81,    0, "[Block move]                           "},
    { true,  PAR,    1,   48,    0, "[Random number sequence]               "},
    { true,  PAR,    1,    6,    0, "[Modulo 20, random pattern]            "},
    { true,  ONE,    6,  240,    0, "[Bit fade test, 2 patterns]            "},
};

int ticks_per_pass[NUM_PASS_TYPES];
int ticks_per_test[NUM_PASS_TYPES][NUM_TEST_PATTERNS];

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

#define BARRIER \
    if (my_cpu >= 0) { \
        if (TRACE_BARRIERS) { \
            trace(my_cpu, "Run barrier wait at %s line %i", __FILE__, __LINE__); \
        } \
        if (power_save < POWER_SAVE_HIGH) { \
            barrier_spin_wait(run_barrier); \
        } else { \
            barrier_halt_wait(run_barrier); \
        } \
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

        // Moving inversions, 8 bit walking ones and zeros.
      case 4: {
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

        // Moving inversions, fixed random pattern.
      case 5:
        if (cpuid_info.flags.rdtsc) {
            prsg_state = get_tsc();
        } else {
            prsg_state = 1 + pass_num;
        }
        prsg_state *= 0x12345678;

        for (int i = 0; i < iterations; i++) {
            prsg_state = prsg(prsg_state);

            testword_t pattern1 = prsg_state;
            testword_t pattern2 = ~pattern1;

            BARRIER;
            ticks += test_mov_inv_fixed(my_cpu, 2, pattern1, pattern2);
            BAILOUT;
        }
        break;

        // Moving inversions, 32/64 bit shifting pattern.
      case 6:
        for (int offset = 0; offset < TESTWORD_WIDTH; offset++) {
            BARRIER;
            ticks += test_mov_inv_walk1(my_cpu, iterations, offset, false);
            BAILOUT;

            BARRIER;
            ticks += test_mov_inv_walk1(my_cpu, iterations, offset, true);
            BAILOUT;
        }
        break;

        // Block move.
      case 7:
        ticks += test_block_move(my_cpu, iterations);
        BAILOUT;
        break;

        // Moving inversions, fully random patterns.
      case 8:
        for (int i = 0; i < iterations; i++) {
            BARRIER;
            ticks += test_mov_inv_random(my_cpu);
            BAILOUT;
        }
        break;

        // Modulo 20 check, fixed random pattern.
      case 9:
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

        // Bit fade test.
      case 10:
        ticks += test_bit_fade(my_cpu, stage, iterations);
        BAILOUT;
        break;
    }
    return ticks;
}
