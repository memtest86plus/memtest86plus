// SPDX-License-Identifier: GPL-2.0
#ifndef SIMD_H
#define SIMD_H
/**
 * \file
 *
 * Provides detection of the SIMD extensions usable by the memory tests.
 *
 *//*
 * Copyright (C) 2004-2026 Sam Demeulemeester.
 */

#include <stddef.h>

/**
 * The SIMD tiers usable by the memory tests.
 */
typedef enum {
    SIMD_NONE,      // scalar only
    SIMD_SSE2,      // x86 128-bit
    SIMD_AVX2       // x86 256-bit
} simd_tier_t;

/**
 * The best SIMD tier usable on this platform. Initialised by simd_init().
 */
extern simd_tier_t simd_tier;

/**
 * Detects the best usable SIMD tier. Must be called after cpuid_init().
 */
void simd_init(void);

/**
 * Returns the display name of the selected SIMD tier, or NULL when only
 * scalar operations are available.
 */
static inline const char *simd_tier_name(void)
{
    switch (simd_tier) {
      case SIMD_AVX2:
        return "AVX2";
      case SIMD_SSE2:
        return "SSE2";
      default:
        return NULL;
    }
}

#endif // SIMD_H
