// SPDX-License-Identifier: GPL-2.0
#ifndef MSR_H
#define MSR_H
/**
 * \file
 *
 * Provides access to the CPU machine-specific registers.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#define MSR_PLATFORM_INFO               0xce

#define MSR_EBC_FREQUENCY_ID            0x2c

#define MSR_IA32_PLATFORM_ID            0x17
#define MSR_IA32_APIC_BASE              0x1b
#define MSR_IA32_EBL_CR_POWERON         0x2a
#define MSR_IA32_MCG_CTL                0x17b
#define MSR_IA32_PERF_STATUS            0x198
#define MSR_IA32_THERM_STATUS           0x19c
#define MSR_IA32_TEMPERATURE_TARGET     0x1a2

#define MSR_EFER                        0xc0000080

#define MSR_K7_HWCR                     0xc0010015
#define MSR_K7_VID_STATUS               0xc0010042

#define MSR_AMD64_NB_CFG                0xc001001f
#define MSR_AMD64_COFVID_STATUS         0xc0010071

#define rdmsr(msr, value1, value2)  \
    __asm__ __volatile__("rdmsr"    \
        : "=a" (value1),            \
          "=d" (value2)             \
        : "c"  (msr)                \
        : "edi"                     \
    )

#define wrmsr(msr, value1, value2)  \
    __asm__ __volatile__("wrmsr"    \
        : /* no outputs */          \
        : "c" (msr),                \
          "a" (value1),             \
          "d" (value2)              \
    )

#endif // MSR_H
