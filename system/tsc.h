// SPDX-License-Identifier: GPL-2.0
#ifndef TSC_H
#define TSC_H
/**
 * \file
 *
 * Provides access to the CPU timestamp counter.
 *
 *//*
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
#if defined(__i386__) || defined(__x86_64__)
static inline uint64_t get_tsc(void)
{
    uint32_t    tl;
    uint32_t    th;

    rdtsc(tl, th);
    return (uint64_t)th << 32 | (uint64_t)tl;
}
#else
static inline uint64_t get_tsc(void)
{
  uint64_t val = 0;

  __asm__ __volatile__(
    "csrrd %0, 0x201\n\t"
    : "=r"(val)
    :
    );

  return val;
}
#endif

#endif // TSC_H
