// SPDX-License-Identifier: GPL-2.0
#ifndef ARM64_REGISTERS_H
#define ARM64_REGISTERS_H
/**
 * \file
 *
 * Provides access to the ARM64 (AArch64) system registers.
 *
 *//*
// Copyright (C) 2026 Sam Demeulemeester.
 */

#include <stdint.h>

#define read_sysreg(reg)                            \
    ({                                              \
        uint64_t value;                             \
        __asm__ __volatile__ ("mrs %0, " #reg       \
            : "=r" (value)                          \
        );                                          \
        value;                                      \
    })

#define write_sysreg(value, reg)                    \
    __asm__ __volatile__ ("msr " #reg ", %0"        \
        : /* no outputs */                          \
        : "r" ((uint64_t)(value))                   \
    )

// MIDR_EL1 fields

#define MIDR_IMPLEMENTER(midr)  (((midr) >> 24) & 0xFF)
#define MIDR_VARIANT(midr)      (((midr) >> 20) & 0xF)
#define MIDR_ARCHITECTURE(midr) (((midr) >> 16) & 0xF)
#define MIDR_PART_NUM(midr)     (((midr) >>  4) & 0xFFF)
#define MIDR_REVISION(midr)     (((midr) >>  0) & 0xF)

// MPIDR_EL1 affinity fields. This mask extract Aff3/2/1/0

#define MPIDR_AFFINITY_MASK     UINT64_C(0xFF00FFFFFF)

#endif // ARM64_REGISTERS_H
