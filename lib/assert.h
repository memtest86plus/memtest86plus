// SPDX-License-Identifier: GPL-2.0
#ifndef ASSERT_H
#define ASSERT_H
/**
 * \file
 *
 * Provides a function to terminate the program if an unexpected and fatal
 * error is detected.
 *
 *//*
 * Copyright (C) 2022 Martin Whitaker.
 */

/*
 * Terminates the program (using a breakpoint exception) if expr is equal
 * to zero.
 */
static inline void assert(int expr)
{
    if (!expr) {
#if defined(__i386__) || defined(__x86_64__)
        __asm__ __volatile__ ("int $3");
#elif defined(__loongarch_lp64)
        __asm__ __volatile__ ("break 0");
#endif
    }
}

#endif // ASSERT_H
