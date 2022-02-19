// SPDX-License-Identifier: GPL-2.0
#ifndef TEST_HELPER_H
#define TEST_HELPER_H
/**
 * \file
 *
 * Provides some common definitions and helper functions for the memory
 * tests.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stddef.h>
#include <stdint.h>

#include "test.h"

/**
 * Test word atomic read and write functions.
 */
#ifdef __x86_64__
#include "memrw64.h"
#define read_word   read64
#define write_word  write64
#else
#include "memrw32.h"
#define read_word   read32
#define write_word  write32
#endif

/**
 * A wrapper for guiding branch prediction.
 */
#define unlikely(x) __builtin_expect(!!(x), 0)

/**
 * The block size processed between each update of the progress bars and
 * spinners. This also affects how quickly the program will respond to the
 * keyboard.
 */
#define SPIN_SIZE (1 << 27)  // in testwords

/**
 * A macro to perform test bailout when requested.
 */
#define BAILOUT if (bail) return ticks

/**
 * Returns value rounded down to the nearest multiple of align_size.
 */
static inline uintptr_t round_down(uintptr_t value, size_t align_size)
{
    return value & ~(align_size - 1);
}

/**
 * Returns value rounded up to the nearest multiple of align_size.
 */
static inline uintptr_t round_up(uintptr_t value, size_t align_size)
{
    return (value + (align_size - 1)) & ~(align_size - 1);
}

/**
 * Seeds the psuedo-random number generator for my_cpu.
 */
void random_seed(int my_cpu, uint64_t seed);

/**
 * Returns a psuedo-random number for my_cpu. The sequence of numbers returned
 * is repeatable for a given starting seed. The sequence repeats after 2^64 - 1
 * numbers. Within that period, no number is repeated.
 */
testword_t random(int my_cpu);

/**
 * Calculates the start and end word address for the chunk of segment that is
 * to be tested by my_cpu. The chunk start will be aligned to a multiple of
 * chunk_align.
 */
void calculate_chunk(testword_t **start, testword_t **end, int my_cpu, int segment, size_t chunk_align);

/**
 * Flushes the CPU caches. If SMP is enabled, synchronises the threads before
 * and after issuing the cache flush instruction.
 */
void flush_caches(int my_cpu);

#endif // TEST_HELPER_H
