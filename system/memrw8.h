// SPDX-License-Identifier: GPL-2.0
#ifndef MEMRW8_H
#define MEMRW8_H
/**
 * \file
 *
 * Provides some 8-bit memory access functions. These stop the compiler
 * optimizing accesses which need to be ordered and atomic. Mostly used
 * for accessing memory-mapped hardware registers.
 *
 *//*
 * Copyright (C) 2021-2022 Martin Whitaker.
 */

#include <stdint.h>

/**
 * Reads and returns the value stored in the 8-bit memory location pointed
 * to by ptr.
 */
static inline uint8_t read8(const volatile uint8_t *ptr)
{
    uint8_t val;
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__(
        "movb %1, %0"
        : "=r" (val)
        : "m" (*ptr)
        : "memory"
    );
#elif defined(__loongarch_lp64)
    __asm__ __volatile__(
        "ld.b %0, %1"
        : "=r" (val)
        : "m" (*ptr)
        : "memory"
    );
#endif
    return val;
}

/**
 * Writes val to the 8-bit memory location pointed to by ptr.
 */
static inline void write8(const volatile uint8_t *ptr, uint8_t val)
{
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__(
        "movb %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#elif defined(__loongarch_lp64)
    __asm__ __volatile__(
        "st.b %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#endif
}

/**
 * Writes val to the 8-bit memory location pointed to by ptr. Reads it
 * back (and discards it) to ensure the write is complete.
 */
static inline void flush8(const volatile uint8_t *ptr, uint8_t val)
{
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__(
        "movb %1, %0\n"
        "movb %0, %1"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#elif defined(__loongarch_lp64)
    __asm__ __volatile__(
        "st.b %1, %0\n"
        "ld.b %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#endif
}

#endif // MEMRW8_H
