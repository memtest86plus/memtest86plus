// SPDX-License-Identifier: GPL-2.0
#ifndef MEMRW32_H
#define MEMRW32_H
/**
 * \file
 *
 * Provides some 32-bit memory access functions. These stop the compiler
 * optimizing accesses which need to be ordered and atomic. Mostly used
 * for accessing memory-mapped hardware registers.
 *
 *//*
 * Copyright (C) 2021-2022 Martin Whitaker.
 */

#include <stdint.h>

/**
 * Reads and returns the value stored in the 32-bit memory location pointed
 * to by ptr.
 */
static inline uint32_t read32(const volatile uint32_t *ptr)
{
    uint32_t val;
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__(
        "movl %1, %0"
        : "=r" (val)
        : "m" (*ptr)
        : "memory"
    );
#elif defined(__loongarch_lp64)
    __asm__ __volatile__(
        "ld.w %0, %1"
        : "=r" (val)
        : "m" (*ptr)
        : "memory"
    );
#endif
    return val;
}

/**
 * Writes val to the 32-bit memory location pointed to by ptr.
 */
static inline void write32(const volatile uint32_t *ptr, uint32_t val)
{
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__(
        "movl %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#elif defined(__loongarch_lp64)
    __asm__ __volatile__(
        "st.w %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#endif
}

/**
 * Writes val to the 32-bit memory location pointed to by ptr. Reads it
 * back (and discards it) to ensure the write is complete.
 */
static inline void flush32(const volatile uint32_t *ptr, uint32_t val)
{
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__(
        "movl %1, %0\n"
        "movl %0, %1"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#elif defined(__loongarch_lp64)
    __asm__ __volatile__(
        "st.w %1, %0\n"
        "ld.w %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#endif
}

#endif // MEMRW32_H
