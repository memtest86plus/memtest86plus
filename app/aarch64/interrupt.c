// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester
//
// The C part of the ARM64 (AArch64) exception handler. The assembler part
// in boot/aarch64/startup.S saves the interrupted context in a trap_regs
// struct on the stack and calls interrupt(). We don't use interrupts on
// this architecture (cross-CPU wakeups use the event mechanism), so every
// exception that gets here is reported as fatal

#include <stdint.h>

#include "hwctrl.h"
#include "keyboard.h"
#include "screen.h"
#include "smp.h"

#include "display.h"

#include "interrupt.h"

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

// Match the context frame layout in boot/aarch64/startup.S.

struct trap_regs {
    uint64_t    x[31];
    uint64_t    sp;
    uint64_t    elr;
    uint64_t    spsr;
    uint64_t    esr;
    uint64_t    far;
    uint64_t    vec;
    uint64_t    pad;
};

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

static const char vector_type_name[][12] = {
    "Synchronous",
    "IRQ",
    "FIQ",
    "SError",
};

static const char vector_origin_name[][7] = {
    "EL1t",
    "EL1h",
    "EL0/64",
    "EL0/32",
};

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static const char *exception_class_name(uint64_t esr)
{
    switch ((esr >> 26) & 0x3F) {
      case 0x00: return "Unknown";
      case 0x0E: return "Illegal execution state";
      case 0x15: return "SVC";
      case 0x18: return "Sysreg trap";
      case 0x20: return "Instruction abort (lower EL)";
      case 0x21: return "Instruction abort";
      case 0x22: return "PC alignment fault";
      case 0x24: return "Data abort (lower EL)";
      case 0x25: return "Data abort";
      case 0x26: return "SP alignment fault";
      case 0x2F: return "SError";
      case 0x30:
      case 0x31: return "Breakpoint";
      case 0x32:
      case 0x33: return "Software step";
      case 0x34:
      case 0x35: return "Watchpoint";
      case 0x3C: return "BRK";
      default:   return "Other";
    }
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void interrupt(struct trap_regs *trap_regs)
{
    spin_lock(error_mutex);

    clear_message_area();

    display_pinned_message(0, 0, "Unexpected exception on CPU %i", smp_my_cpu_num());
    display_pinned_message(2, 0, "Type: %s %s (%s)", vector_type_name[trap_regs->vec & 0x3],
                                                     vector_origin_name[(trap_regs->vec >> 2) & 0x3],
                                                     exception_class_name(trap_regs->esr));
    display_pinned_message(3, 0, "  PC: %016x", (uintptr_t)trap_regs->elr);
    display_pinned_message(4, 0, " FAR: %016x", (uintptr_t)trap_regs->far);
    display_pinned_message(5, 0, " ESR: %016x", (uintptr_t)trap_regs->esr);
    display_pinned_message(6, 0, "SPSR: %016x", (uintptr_t)trap_regs->spsr);

    display_pinned_message(3, 25,  "LR: %016x", (uintptr_t)trap_regs->x[30]);
    display_pinned_message(4, 25,  "SP: %016x", (uintptr_t)trap_regs->sp);
    display_pinned_message(5, 25,  "X0: %016x", (uintptr_t)trap_regs->x[0]);
    display_pinned_message(6, 25,  "X1: %016x", (uintptr_t)trap_regs->x[1]);
    display_pinned_message(7, 25,  "X2: %016x", (uintptr_t)trap_regs->x[2]);
    display_pinned_message(8, 25,  "X3: %016x", (uintptr_t)trap_regs->x[3]);
    display_pinned_message(9, 25,  "X4: %016x", (uintptr_t)trap_regs->x[4]);
    display_pinned_message(10, 25, "X5: %016x", (uintptr_t)trap_regs->x[5]);
    display_pinned_message(11, 25, "X6: %016x", (uintptr_t)trap_regs->x[6]);
    display_pinned_message(12, 25, "X7: %016x", (uintptr_t)trap_regs->x[7]);
    display_pinned_message(13, 25, "X8: %016x", (uintptr_t)trap_regs->x[8]);

    display_pinned_message(0, 50, "Stack:");
    for (int i = 0; i < 12; i++) {
        uintptr_t addr = trap_regs->sp + sizeof(uint64_t)*(11 - i);
        uint64_t data = *(uint64_t *)addr;
        display_pinned_message(1 + i, 50, "%012x %016x", addr, (uintptr_t)data);
    }

    clear_screen_region(ROW_FOOTER, 0, ROW_FOOTER, SCREEN_WIDTH - 1);
    prints(ROW_FOOTER, 0, "Press any key to reboot...");

    while (get_key() == 0) { }
    reboot();
}
