// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from extract of memtest86+ lib.c:
//
// lib.c - MemTest-86  Version 3.4
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdint.h>

#include "cpuid.h"
#include "hwctrl.h"
#include "keyboard.h"
#include "screen.h"
#include "smp.h"

#include "error.h"
#include "display.h"

#include "interrupt.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define INT_DIVBY0     0
#define INT_RSV        1
#define INT_NMI        2
#define INT_BRKPOINT   3
#define INT_OVERFLOW   4
#define INT_BOUND      5
#define INT_UNDEFOP    6
#define INT_DEVNA      7
#define INT_DOUBLEFLT  8
#define INT_FPUSEGOVR  9
#define INT_INVDTSS    10
#define INT_SEGFLT     11
#define INT_STKSEGFLT  12
#define INT_GPF        13
#define INT_PAGEFLT    14
#define INT_RESERVED1  15
#define INT_X87FPE     16
#define INT_ALIGNCHK   17
#define INT_MCHCHK     18
#define INT_SIMDFPE    19

#define OPCODE_HLT      0xF4
#define OPCODE_JE       0x74
#define OPCODE_RDMSR    0x320F
#define OPCODE_WRMSR    0x300F

#ifdef __x86_64__
#define REG_PREFIX  "r"
#define REG_DIGITS  "16"
#define ADR_DIGITS  "12"
#else
#define REG_PREFIX  "e"
#define REG_DIGITS  "8"
#define ADR_DIGITS  "8"
#endif

static const char codes[][13] = {
    "Divide by 0",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bounds",
    "Invalid Op",
    "No FPU",
    "Double fault",
    "Seg overrun",
    "Invalid TSS",
    "Seg fault",
    "Stack fault",
    "Gen prot.",
    "Page fault",
    "Reserved",
    "FPU error",
    "Alignment",
    "Machine chk",
    "SIMD FPE"
};

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

#ifdef __x86_64__
typedef uint64_t    reg_t;
#else
typedef uint32_t    reg_t;
#endif

struct trap_regs {
    reg_t   ds;
    reg_t   es;
    reg_t   ss;
    reg_t   ax;
    reg_t   bx;
    reg_t   cx;
    reg_t   dx;
    reg_t   di;
    reg_t   si;
#ifndef __x86_64__
    reg_t   reserved1;
    reg_t   reserved2;
    reg_t   sp;
#endif
    reg_t   bp;
    reg_t   vect;
    reg_t   code;
    reg_t   ip;
    reg_t   cs;
    reg_t   flags;
#ifdef __x86_64__
    reg_t   sp;
#endif
};

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void interrupt(struct trap_regs *trap_regs)
{
    // Get the page fault address.
    uintptr_t address = 0;
    uintptr_t cr0;
    uintptr_t cr3;
    uintptr_t cr4;
    if (trap_regs->vect == INT_PAGEFLT) {
        __asm__(
            "mov %%cr2, %0"
            :"=r" (address)
        );
    }

    __asm__(
        "mov %%cr0, %0; mov %%cr3, %1; mov %%cr4, %2"
        : "=r" (cr0), "=r" (cr3), "=r" (cr4)
    );

    if (trap_regs->vect == INT_NMI) {
        uint8_t *pc = (uint8_t *)trap_regs->ip;
        if (pc[-1] == OPCODE_HLT) {
            // Assume this is a barrier wakeup signal sent via IPI.
            return;
        }
        // Catch the rare case that a core will fail to reach the HLT instruction before
        // its wakeup signal arrives. The barrier code contains an atomic decrement, a JE
        // instruction (two bytes), and a HLT instruction (one byte). The atomic decrement
        // must have completed if another core has reached the point of sending the wakeup
        // signals, so we should find the HLT opcode either at pc[0] or at pc[2]. If we find
        // it, adjust the interrupt return address to point to the following instruction.
        if (pc[0] == OPCODE_HLT || (pc[0] == OPCODE_JE && pc[2] == OPCODE_HLT)) {
            uintptr_t *return_addr;
            if (cpuid_info.flags.lm == 1) {
                return_addr = (uintptr_t *)(trap_regs->sp - 40);
            } else {
                return_addr = (uintptr_t *)(trap_regs->sp - 12);
            }
            if (pc[2] == OPCODE_HLT) {
                *return_addr += 3;
            } else {
                *return_addr += 1;
            }
            return;
        }
#if REPORT_PARITY_ERRORS
        parity_error();
        return;
#endif
    }

    // Catch GPF following a RDMSR instruction (usually from a non-existent msr)
    // and allow the program to continue. A cleaner way to do this would be to
    // use an exception table similar to the linux kernel, but it's probably
    // overkill for Memtest86+. Set a return value of 0 and leave a small mark
    // on top-right corner to indicate something went wrong at some point.
    if (trap_regs->vect == INT_GPF) {
        uint16_t *pc = (uint16_t *)trap_regs->ip;
        if (pc[0] == OPCODE_RDMSR) {
            uintptr_t *return_addr;
            if (cpuid_info.flags.lm == 1) {
                return_addr = (uintptr_t *)(trap_regs->sp - 40);
            } else {
                return_addr = (uintptr_t *)(trap_regs->sp - 12);
            }
            *return_addr += 2;
            trap_regs->ax = 0;
            trap_regs->dx = 0;
            display_msr_failed_flag();
            return;
        }
    }

    spin_lock(error_mutex);

    clear_message_area();

    display_pinned_message(0, 0, "Unexpected interrupt on CPU %i", smp_my_cpu_num());
    if (trap_regs->vect <= 19) {
        display_pinned_message(2, 0, "Type: %s", codes[trap_regs->vect]);
    } else {
        display_pinned_message(2, 0, "Type: %i", trap_regs->vect);
    }
    display_pinned_message(3, 0, "  IP: %0" REG_DIGITS "x", (uintptr_t)trap_regs->ip);
    display_pinned_message(4, 0, "  CS: %0" REG_DIGITS "x", (uintptr_t)trap_regs->cs);
    display_pinned_message(5, 0, "Flag: %0" REG_DIGITS "x", (uintptr_t)trap_regs->flags);
    display_pinned_message(6, 0, "Code: %0" REG_DIGITS "x", (uintptr_t)trap_regs->code);
    display_pinned_message(7, 0, "  DS: %0" REG_DIGITS "x", (uintptr_t)trap_regs->ds);
    display_pinned_message(8, 0, "  ES: %0" REG_DIGITS "x", (uintptr_t)trap_regs->es);
    display_pinned_message(9, 0, "  SS: %0" REG_DIGITS "x", (uintptr_t)trap_regs->ss);
    if (trap_regs->vect == 14) {
        display_pinned_message(9, 0, " Addr: %0" REG_DIGITS "x", address);
    }

    display_pinned_message(2, 25, REG_PREFIX "ax: %0" REG_DIGITS "x", (uintptr_t)trap_regs->ax);
    display_pinned_message(3, 25, REG_PREFIX "bx: %0" REG_DIGITS "x", (uintptr_t)trap_regs->bx);
    display_pinned_message(4, 25, REG_PREFIX "cx: %0" REG_DIGITS "x", (uintptr_t)trap_regs->cx);
    display_pinned_message(5, 25, REG_PREFIX "dx: %0" REG_DIGITS "x", (uintptr_t)trap_regs->dx);
    display_pinned_message(6, 25, REG_PREFIX "di: %0" REG_DIGITS "x", (uintptr_t)trap_regs->di);
    display_pinned_message(7, 25, REG_PREFIX "si: %0" REG_DIGITS "x", (uintptr_t)trap_regs->si);
    display_pinned_message(8, 25, REG_PREFIX "bp: %0" REG_DIGITS "x", (uintptr_t)trap_regs->bp);
    display_pinned_message(9, 25, REG_PREFIX "sp: %0" REG_DIGITS "x", (uintptr_t)trap_regs->sp);

    display_pinned_message(10, 0, " CR0: %0" REG_DIGITS "x", cr0);
    display_pinned_message(11, 0, " CR3: %0" REG_DIGITS "x", cr3);
    display_pinned_message(11, 25, "CR4: %0" REG_DIGITS "x", cr4);

    display_pinned_message(0, 50, "Stack:");
    for (int i = 0; i < 12; i++) {
        uintptr_t addr = trap_regs->sp + sizeof(reg_t)*(11 - i);
        reg_t data = *(reg_t *)addr;
        display_pinned_message(1 + i, 50, "%0" ADR_DIGITS "x %0" REG_DIGITS "x", addr, (uintptr_t)data);
    }

    display_pinned_message(13, 0, "CS:IP:");
    uint8_t *pp = (uint8_t *)((uintptr_t)trap_regs->ip);
    for (int i = 0; i < 12; i++) {
        display_pinned_message(13, 7 + 3*i, "%02x", (uintptr_t)pp[i]);
    }

    clear_screen_region(ROW_FOOTER, 0, ROW_FOOTER, SCREEN_WIDTH - 1);
    prints(ROW_FOOTER, 0, "Press any key to reboot...");

    while (get_key() == 0) { }
    reboot();
}
