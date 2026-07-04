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
#if defined(__loongarch_lp64)
#include "cpuinfo.h"
// LoongArch GCC builtin function
#include <larchintrin.h>
#endif
#if defined(__aarch64__)
#include "cpuinfo.h"
#endif

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
#elif defined(__loongarch_lp64)
static inline uint64_t get_tsc(void)
{
    uint64_t val = 0;
    static uint64_t stable_count_freq = 0;

    if (!stable_count_freq) {
      stable_count_freq = ((__cpucfg(0x4) * (__cpucfg(0x5) & 0xFFFF)) / ((__cpucfg(0x5) >> 16) & 0xFFFF)) / 1000; // KHz
    }

    __asm__ __volatile__(
      "rdtime.d %0, $zero\n\t"
      : "=r"(val)
      :
      );

    return (val * (clks_per_msec / stable_count_freq));
}
#elif defined(__aarch64__)
static inline uint64_t get_tsc(void)
{
    uint64_t count;
    static uint64_t timer_freq_khz = 0;

    if (!timer_freq_khz) {
        uint64_t freq;
        __asm__ __volatile__ ("mrs %0, cntfrq_el0" : "=r" (freq));
        timer_freq_khz = freq / 1000;
        if (!timer_freq_khz) {
            timer_freq_khz = 1;
        }
    }

    // Scale the generic timer count to CPU clock cycles, so that intervals
    // measured with get_tsc() can be converted to time using clks_per_msec.
    __asm__ __volatile__ (
        "isb                    \n\t"
        "mrs %0, cntvct_el0     \n\t"
        : "=r" (count)
    );

    return count * (clks_per_msec / timer_freq_khz);
}
#endif

#endif // TSC_H
