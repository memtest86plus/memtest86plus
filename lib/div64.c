// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Martin Whitaker.

#include <stdint.h>

// Implement the 64-bit division primitive. This is only needed for 32-bit
// builds. We don't use this for anything critical, so a floating-point
// approximation is good enough.

uint64_t __udivdi3(uint64_t num, uint64_t den)
{
    return (uint64_t)((double)num / (double)den);
}
