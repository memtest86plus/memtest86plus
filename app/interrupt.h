// SPDX-License-Identifier: GPL-2.0
#ifndef INTERRUPT_H
#define INTERRUPT_H
/**
 * \file
 *
 * Provides the interrupt handler.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

struct trap_regs;

/**
 * Handles an interrupt.
 */
void interrupt(struct trap_regs *trap_regs);

#endif // INTERRUPT_H
