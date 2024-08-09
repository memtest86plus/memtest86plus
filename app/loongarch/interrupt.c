// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.
//

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

#include <stdint.h>
#include "hwctrl.h"
#include "screen.h"
#include "keyboard.h"
#include "smp.h"
#include "display.h"

#include <larchintrin.h>

#define INT_SIP0   0
#define INT_SIP1   1
#define INT_IP0    2
#define INT_IP1    3
#define INT_IP2    4
#define INT_IP3    5
#define INT_IP4    6
#define INT_IP5    7
#define INT_IP6    8
#define INT_IP7    9
#define INT_PMC    10
#define INT_TIMER  11
#define INT_IPI    12


#define EXC_INT   0
#define EXC_PIL   1
#define EXC_PIS   2
#define EXC_PIF   3
#define EXC_PME   4
#define EXC_PNR   5
#define EXC_PNX   6
#define EXC_PPI   7
#define EXC_ADE   8
#define EXC_ALE   9
#define EXC_BCE   10
#define EXC_SYS   11
#define EXC_BRK   12
#define EXC_INE   13
#define EXC_IPE   14
#define EXC_FPD   15
#define EXC_SXD   16
#define EXC_ASXD  17
#define EXC_FPE   18

#define OP_IDLE   0x06488000
#define OP_BGE    0x64000000


#ifdef __loongarch_lp64
typedef uint64_t    reg_t;
#define CSR_REG_DIGITS "16"
#define GP_REG_DIGITS  "16"
#define ADR_DIGITS     "12"
#else
typedef uint32_t    reg_t;
#define CSR_REG_DIGITS "8"
#define GP_REG_DIGITS  "8"
#define ADR_DIGITS     "8"
#endif

static const char  *exception_code[]    = {
  "#INT - Interrupt(CSR.ECFG.VS=0)",
  "#PIL - Page invalid exception for Load option",
  "#PIS - Page invalid exception for Store operation",
  "#PIF - Page invalid exception for Fetch operation",
  "#PME - Page modification exception",
  "#PNR - Page non-readable exception",
  "#PNX - Page non-executable exception",
  "#PPI - Page privilege level illegal exception",
  "#ADE - Address error exception",
  "#ALE - Address alignment fault exception",
  "#BCE - Bound check exception",
  "#SYS - System call exception",
  "#BRK - Breakpoint exception",
  "#INE - Instruction non-defined exception",
  "#IPE - Instruction privilege error exception",
  "#FPD - Floating-point instruction disable exception",
  "#SXD - 128-bit vector (SIMD instructions) expansion instruction disable exception",
  "#ASXD - 256-bit vector (Advanced SIMD instructions) expansion instruction disable exception",
  "#FPE - Floating-Point error exception",
  "#TBR - TLB refill exception"
};


struct system_context {
    //
    // GP
    //
    reg_t r0;
    reg_t r1;
    reg_t r2;
    reg_t r3;
    reg_t r4;
    reg_t r5;
    reg_t r6;
    reg_t r7;
    reg_t r8;
    reg_t r9;
    reg_t r10;
    reg_t r11;
    reg_t r12;
    reg_t r13;
    reg_t r14;
    reg_t r15;
    reg_t r16;
    reg_t r17;
    reg_t r18;
    reg_t r19;
    reg_t r20;
    reg_t r21;
    reg_t r22;
    reg_t r23;
    reg_t r24;
    reg_t r25;
    reg_t r26;
    reg_t r27;
    reg_t r28;
    reg_t r29;
    reg_t r30;
    reg_t r31;

    //
    // CSR
    //
    reg_t crmd;
    reg_t prmd;
    reg_t euen;
    reg_t misc;
    reg_t ecfg;
    reg_t estat;
    reg_t era;
    reg_t badv;
    reg_t badi;
};

void interrupt(struct system_context *system_context)
{
    uint8_t ecode;

    if (system_context->estat & (1 << INT_IPI)) {
        uint32_t *pc = (uint32_t *)system_context->era;

        //
        // Clean the mailbox 0 and 3
        //
        __iocsrwr_d(0x0, 0x1020);
        __iocsrwr_d(0x0, 0x1038);

        //
        // Clean IPI
        //
        __iocsrwr_w(__iocsrrd_w(0x1000), 0x100c);
        __asm__ __volatile__ ("dbar 0");

        if ((pc[-1] & ~0x7FFF) == OP_IDLE) {
            // Assume this is a barrier wakeup signal sent via IPI.
            system_context->era += 4;
            return;
        }

        // Catch the rare case that a core will fail to reach the IDLE instruction before
        // its wakeup signal arrives. The barrier code contains an atomic decrement, a BLT
        // instruction, and a IDLE instruction. The atomic decrement must have completed if
        // another core has reached the point of sending the wakeup signals, so we should
        // find the IDLE opcode either at pc[0] or at pc[1]. If we find it, adjust the ERA
        // to point to the following instruction.
        if ((pc[0] & ~0x7FFF) == OP_IDLE) {
            system_context->era += 8;
            return;
        }
        if ((pc[0] & ~0x3FFFFFF) == OP_BGE && (pc[1] & ~0x7FFF) == OP_IDLE) {
            system_context->era += 12;
            return;
        }
        return;
    }

    ecode = (system_context->estat >> 16) & 0x3F;

    spin_lock(error_mutex);

    clear_message_area();

    display_pinned_message(0, 0, "Unexpected interrupt on CPU %i", smp_my_cpu_num());
    if (__csrrd_w(0x8A) & 0x1) {
        display_pinned_message(2, 0, "Type: %s", exception_code[19]);
    } else if (ecode < 19) {
        display_pinned_message(2, 0, "Type: %s", exception_code[ecode]);
    } else {
        display_pinned_message(2, 0, "Type: %i", ecode);
    }
    display_pinned_message(3, 0, " BADV: %0" CSR_REG_DIGITS "x", (uintptr_t)system_context->badv);
    display_pinned_message(4, 0, " BADI: %0" CSR_REG_DIGITS "x", (uintptr_t)system_context->badi);
    display_pinned_message(5, 0, "  ERA: %0" CSR_REG_DIGITS "x", (uintptr_t)system_context->era);
    display_pinned_message(6, 0, " EUEN: %0" CSR_REG_DIGITS "x", (uintptr_t)system_context->euen);
    display_pinned_message(7, 0, " ECFG: %0" CSR_REG_DIGITS "x", (uintptr_t)system_context->ecfg);
    display_pinned_message(8, 0, "ESTAT: %0" CSR_REG_DIGITS "x", (uintptr_t)system_context->estat);

    display_pinned_message(3, 25, "A0: %0" GP_REG_DIGITS "x", (uintptr_t)system_context->r4);
    display_pinned_message(4, 25, "A1: %0" GP_REG_DIGITS "x", (uintptr_t)system_context->r5);
    display_pinned_message(5, 25, "A2: %0" GP_REG_DIGITS "x", (uintptr_t)system_context->r6);
    display_pinned_message(6, 25, "A3: %0" GP_REG_DIGITS "x", (uintptr_t)system_context->r7);
    display_pinned_message(7, 25, "A4: %0" GP_REG_DIGITS "x", (uintptr_t)system_context->r8);
    display_pinned_message(8, 25, "A5: %0" GP_REG_DIGITS "x", (uintptr_t)system_context->r9);
    display_pinned_message(9, 25, "RA: %0" GP_REG_DIGITS "x", (uintptr_t)system_context->r1);
    display_pinned_message(10, 25, "SP: %0" GP_REG_DIGITS "x", (uintptr_t)system_context->r3);
    display_pinned_message(11, 25, "T0: %0" GP_REG_DIGITS "x", (uintptr_t)system_context->r12);
    display_pinned_message(12, 25, "T1: %0" GP_REG_DIGITS "x", (uintptr_t)system_context->r13);
    display_pinned_message(13, 25, "T2: %0" GP_REG_DIGITS "x", (uintptr_t)system_context->r14);

    display_pinned_message(0, 50, "Stack:");
    for (int i = 0; i < 12; i++) {
        uintptr_t addr = system_context->r3 + sizeof(reg_t)*(11 - i);
        reg_t data = *(reg_t *)addr;
        display_pinned_message(1 + i, 50, "%0" ADR_DIGITS "x %0" GP_REG_DIGITS "x", addr, (uintptr_t)data);
    }

    clear_screen_region(ROW_FOOTER, 0, ROW_FOOTER, SCREEN_WIDTH - 1);
    prints(ROW_FOOTER, 0, "Press any key to reboot...");

    while (get_key() == 0) { }
    reboot();
}
