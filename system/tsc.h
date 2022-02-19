// SPDX-License-Identifier: GPL-2.0
#ifndef TSC_H
#define TSC_H
/**
 * \file
 *
 * Provides access to the CPU timestamp counter.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdint.h>

#define rdtsc(low, high)            \
    __asm__ __volatile__("rdtsc"    \
        : "=a" (low),               \
          "=d" (high)               \
    )

#define rdtscl(low)                 \
    __asm__ __volatile__("rdtsc"    \
        : "=a" (low)                \
        : /* no inputs */           \
        : "edx"                     \
    )

/**
 * Reads and returns the timestamp counter value.
 */
static inline uint64_t get_tsc(void)
{
    uint32_t    tl;
    uint32_t    th;

    rdtsc(tl, th);
    return (uint64_t)th << 32 | (uint64_t)tl;
}

#endif // TSC_H
