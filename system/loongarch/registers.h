/**
 * SPDX-License-Identifier: GPL-2.0
 *
 * \file
 *
 * Provides functions for reading SPD via I2C
 * Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.
 *
 */

#ifndef _REGISTERS_H_
#define _REGISTERS_H_

#ifdef __loongarch_lp64
#define RSIZE                 8           // 64 bit mode register size
#define GP_REG_CONTEXT_SIZE   32 * RSIZE  // General-purpose registers size
#define FP_REG_CONTEXT_SIZE   34 * RSIZE  // Floating-point registers size
#define CSR_REG_CONTEXT_SIZE  9  * RSIZE  // CSR registers size
#endif

//
// CSR definitions
//
#define LOONGARCH_CSR_CRMD       0x0
#define LOONGARCH_CSR_PRMD       0x1
#define LOONGARCH_CSR_EUEN       0x2
#define LOONGARCH_CSR_MISC       0x3
#define LOONGARCH_CSR_ECFG       0x4
#define LOONGARCH_CSR_ESTAT      0x5
#define LOONGARCH_CSR_ERA        0x6
#define LOONGARCH_CSR_BADV       0x7
#define LOONGARCH_CSR_BADI       0x8
#define LOONGARCH_CSR_EBASE      0xC     // Exception entry base address
#define LOONGARCH_CSR_CPUID      0x20    // CPU core ID
#define LOONGARCH_CSR_KS0        0x30
#define LOONGARCH_CSR_TLBREBASE  0x88    // TLB refill exception entry
#define LOONGARCH_CSR_DMWIN0     0x180   // Direct map win0: MEM & IF
#define LOONGARCH_CSR_DMWIN1     0x181   // Direct map win1: MEM & IF
#define LOONGARCH_CSR_DMWIN2     0x182   // Direct map win2: MEM
#define LOONGARCH_CSR_DMWIN3     0x183   // Direct map win3: MEM
#define LOONGARCH_CSR_PERFCTRL0  0x200   // Perf event 0 config
#define LOONGARCH_CSR_PERFCNTR0  0x201   // Perf event 0 count value
#define LOONGARCH_CSR_PERFCTRL1  0x202   // Perf event 1 config
#define LOONGARCH_CSR_PERFCNTR1  0x203   // Perf event 1 count value
#define LOONGARCH_CSR_PERFCTRL2  0x204   // Perf event 2 config
#define LOONGARCH_CSR_PERFCNTR2  0x205   // Perf event 2 count value
#define LOONGARCH_CSR_PERFCTRL3  0x206   // Perf event 3 config
#define LOONGARCH_CSR_PERFCNTR3  0x207   // Perf event 3 count value

#endif // REGISTERS_H
