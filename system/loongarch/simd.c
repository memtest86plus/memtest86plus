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
    // TODO: detect LSX/LASX support and enable it in the CSR EUEN register.
}
