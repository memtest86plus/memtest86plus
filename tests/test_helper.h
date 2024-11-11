// SPDX-License-Identifier: GPL-2.0
#ifndef TEST_HELPER_H
#define TEST_HELPER_H
/**
 * \file
 *
 * Provides some common definitions and helper functions for the memory
 * tests.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stddef.h>
#include <stdint.h>

#include "test.h"

/**
 * Test word atomic read and write functions.
 */
#include "memrw.h"
#if (ARCH_BITS == 64)
#define read_word   read64
#define write_word  write64
#else
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
 * A macro to skip the current range without disturbing waits on barriers and creating a deadlock.
 */
#define SKIP_RANGE(num_ticks) { if (my_cpu >= 0) { for (int iter = 0; iter < num_ticks; iter++) { do_tick(my_cpu); BAILOUT; } } continue; }

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
 * Returns the next word in a pseudo-random sequence where state was the
 * previous word in that sequence.
 */
static inline testword_t prsg(testword_t state)
{
    // This uses the algorithms described at https://en.wikipedia.org/wiki/Xorshift
#if (ARCH_BITS == 64)
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
#else
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
#endif
    return state;
}

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
