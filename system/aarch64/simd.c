// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester.

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
    // TODO: add an SIMD_NEON tier and NEON/SVE detection to accelerate the
    // vector PRSG kernels. See doc/PLAN_AARCH64_NEON_VEC_PRSG.md.
}
