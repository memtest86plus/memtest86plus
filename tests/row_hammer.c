// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2002-2026 Sam Demeulemeester
//
// Row hammer test for Memtest86+
//
// Derived from an extract of memtest86+ test.c:
// ----------------------------------------------------
// Released under version 2 of the GNU Public License.

#include <stdbool.h>
#include <stdint.h>

#include "display.h"
#include "error.h"
#include "memsize.h"
#include "test.h"

#include "test_funcs.h"
#include "test_helper.h"

#include "cache.h"

#define ROW_BYTES       PAGE_SIZE
#define HAMMER_READS    10000

static inline void flush_cache_line(const void *addr)
{
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__("clflush (%0)" :: "r"(addr) : "memory");
#else
    (void)addr;
    cache_flush();
#endif
}

static inline void hammer_read(const testword_t *addr)
{
#if defined(__x86_64__)
    uint64_t value;
    __asm__ __volatile__("movq (%1), %0" : "=r"(value) : "r"(addr) : "memory");
    (void)value;
#elif defined(__i386__)
    uint32_t value;
    __asm__ __volatile__("movl (%1), %0" : "=r"(value) : "r"(addr) : "memory");
    (void)value;
#elif defined(__loongarch_lp64)
    uint64_t value;
    __asm__ __volatile__("ld.d %0, %1" : "=r"(value) : "m"(*addr) : "memory");
    (void)value;
#else
    (void)*addr;
#endif
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int test_row_hammer(int my_cpu, int iterations)
{
    int ticks = 0;

    if (my_cpu == master_cpu) {
        display_test_pattern_name("row hammer");
    }

#if (ARCH_BITS == 64)
    const testword_t pattern_aggressor = UINT64_C(0x5555555555555555);
#else
    const testword_t pattern_aggressor = 0x55555555;
#endif
    const testword_t pattern_victim = ~pattern_aggressor;

    const size_t row_words = ROW_BYTES / sizeof(testword_t);
    const size_t triple_words = row_words * 3;

    for (int i = 0; i < vm_map_size; i++) {
        testword_t *chunk_start, *chunk_end;
        calculate_chunk(&chunk_start, &chunk_end, my_cpu, i, ROW_BYTES);

        uintptr_t aligned_start = round_up((uintptr_t)chunk_start, ROW_BYTES);
        uintptr_t aligned_end = round_down((uintptr_t)(chunk_end + 1), ROW_BYTES);

        if (aligned_end <= aligned_start) {
            SKIP_RANGE(1)
        }

        testword_t *start = (testword_t *)aligned_start;
        testword_t *end = (testword_t *)aligned_end - 1;

        if ((size_t)(end - start + 1) < triple_words) {
            SKIP_RANGE(1)
        }

        // Initialize rows with alternating aggressor/victim patterns.
        for (testword_t *row = start; row + triple_words <= end + 1; row += triple_words) {
            ticks++;
            if (my_cpu < 0) {
                continue;
            }
            test_addr[my_cpu] = (uintptr_t)row;

            for (size_t w = 0; w < row_words; w++) {
                write_word(row + w, pattern_aggressor);
                write_word(row + row_words + w, pattern_victim);
                write_word(row + 2 * row_words + w, pattern_aggressor);
            }

            do_tick(my_cpu);
            BAILOUT;
        }

        // Hammer the aggressor rows to induce flips in the victim row.
        for (int iter = 0; iter < iterations; iter++) {
            for (testword_t *row = start; row + triple_words <= end + 1; row += triple_words) {
                ticks++;
                if (my_cpu < 0) {
                    continue;
                }
                test_addr[my_cpu] = (uintptr_t)(row + row_words);

                testword_t *aggressor1 = row;
                testword_t *aggressor2 = row + 2 * row_words;

                for (int hammer = 0; hammer < HAMMER_READS; hammer++) {
                    hammer_read(aggressor1);
                    hammer_read(aggressor2);
                    flush_cache_line(aggressor1);
                    flush_cache_line(aggressor2);
                }

                do_tick(my_cpu);
                BAILOUT;
            }
        }

        flush_caches(my_cpu);

        // Verify the victim rows retained the expected pattern.
        for (testword_t *row = start; row + triple_words <= end + 1; row += triple_words) {
            ticks++;
            if (my_cpu < 0) {
                continue;
            }
            test_addr[my_cpu] = (uintptr_t)(row + row_words);

            testword_t *victim = row + row_words;
            for (size_t w = 0; w < row_words; w++) {
                testword_t actual = read_word(victim + w);
                if (unlikely(actual != pattern_victim)) {
                    data_error(victim + w, pattern_victim, actual, true);
                }
            }

            do_tick(my_cpu);
            BAILOUT;
        }
    }

    return ticks;
}
