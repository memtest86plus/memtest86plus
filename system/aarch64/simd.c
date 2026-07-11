// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester.

#include <stdint.h>

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
    // Advanced SIMD (NEON) is architecturally mandatory on ARMv8-A, but honour
    // the ID register: ID_AA64PFR0_EL1.AdvSIMD == 0xF means "not implemented".
    // The ID registers are readable at EL1, where memtest runs.
    uint64_t pfr0 = read_sysreg(id_aa64pfr0_el1);
    uint64_t advsimd = (pfr0 >> 20) & 0xF;

    // SVE (bits [35:32]) is detected here only to reserve a future tier; no SVE
    // kernels exist yet, so we never select SIMD_SVE.
    simd_tier = (advsimd == 0xF) ? SIMD_NONE : SIMD_NEON;
}
