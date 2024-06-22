// SPDX-License-Identifier: GPL-2.0
#ifndef MEMRW16_H
#define MEMRW16_H
/**
 * \file
 *
 * Provides some 16-bit memory access functions. These stop the compiler
 * optimizing accesses which need to be ordered and atomic. Mostly used
 * for accessing memory-mapped hardware registers.
 *
 *//*
 * Copyright (C) 2021-2022 Martin Whitaker.
 */

#include <stdint.h>

/**
 * Reads and returns the value stored in the 16-bit memory location pointed
 * to by ptr.
 */
static inline uint16_t read16(const volatile uint16_t *ptr)
{
    uint16_t val;
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__(
        "movw %1, %0"
        : "=r" (val)
        : "m" (*ptr)
        : "memory"
    );
#elif defined(__loongarch_lp64)
    __asm__ __volatile__(
        "ld.h %0, %1"
        : "=r" (val)
        : "m" (*ptr)
        : "memory"
    );
#endif
    return val;
}

/**
 * Writes val to the 16-bit memory location pointed to by ptr.
 */
static inline void write16(const volatile uint16_t *ptr, uint16_t val)
{
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__(
        "movw %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#elif defined(__loongarch_lp64)
    __asm__ __volatile__(
        "st.h %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#endif
}

/**
 * Writes val to the 16-bit memory location pointed to by ptr. Reads it
 * back (and discards it) to ensure the write is complete.
 */
static inline void flush16(const volatile uint16_t *ptr, uint16_t val)
{
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__(
        "movw %1, %0\n"
        "movw %0, %1"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#elif defined(__loongarch_lp64)
    __asm__ __volatile__(
        "st.h %1, %0\n"
        "ld.h %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#endif
}

#endif // MEMRW16_H
