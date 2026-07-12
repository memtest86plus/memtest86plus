// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester.

#include <larchintrin.h>

#include "registers.h"

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
    // The LASX kernels need FP+LSX+LASX implemented (CPUCFG2) and enabled
    // (EUEN, set per-CPU by the startup code), plus unaligned vector access
    // (CPUCFG1.UAL) for the lane-state loads.
    if ((__cpucfg(2) & 0xC0) != 0xC0 || (__cpucfg(1) & (1 << 20)) == 0) {
        return;
    }
    if ((__csrrd_w(LOONGARCH_CSR_EUEN) & 0x7) == 0x7) {
        simd_tier = SIMD_LASX;
    }
}
