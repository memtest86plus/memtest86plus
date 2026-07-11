// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester.

#include <stdint.h>

#include "cpuid.h"

#include "simd.h"

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

simd_tier_t simd_tier = SIMD_NONE;

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void simd_init(void)
{
#if defined(__x86_64__)
    // SSE2 is part of the x86_64 baseline and is enabled during startup.
    simd_tier = SIMD_SSE2;

    // AVX2 additionally requires the startup code to have enabled AVX state
    // saving. Confirm via XCR0 that both XMM and YMM state are enabled.
    if (cpuid_info.flags.osxsave && cpuid_info.flags.avx && cpuid_info.flags.avx2) {
        uint32_t xcr0_lo, xcr0_hi;
        __asm__ __volatile__ ("xgetbv" : "=a" (xcr0_lo), "=d" (xcr0_hi) : "c" (0));
        if ((xcr0_lo & 0x6) == 0x6) {
            simd_tier = SIMD_AVX2;
        }
    }
#endif
}
