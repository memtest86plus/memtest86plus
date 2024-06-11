// SPDX-License-Identifier: GPL-2.0
#ifndef MEMRW64_H
#define MEMRW64_H
/**
 * \file
 *
 * Provides some 64-bit memory access functions. These stop the compiler
 * optimizing accesses which need to be ordered and atomic. Mostly used
 * for accessing memory-mapped hardware registers.
 *
 *//*
 * Copyright (C) 2021-2022 Martin Whitaker.
 */

#include <stdint.h>

/**
 * Reads and returns the value stored in the 64-bit memory location pointed
 * to by ptr.
 */
static inline uint64_t read64(const volatile uint64_t *ptr)
{
    uint64_t val;
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__(
        "movq %1, %0"
        : "=r" (val)
        : "m" (*ptr)
        : "memory"
    );
#elif defined(__loongarch_lp64)
    __asm__ __volatile__(
        "ld.d %0, %1"
        : "=r" (val)
        : "m" (*ptr)
        : "memory"
    );
#endif
    return val;
}

/**
 * Writes val to the 64-bit memory location pointed to by ptr.
 */
static inline void write64(const volatile uint64_t *ptr, uint64_t val)
{
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__(
        "movq %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#elif defined(__loongarch_lp64)
    __asm__ __volatile__(
        "st.d %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#endif
}

/**
 * Writes val to the 64-bit memory location pointed to by ptr. Reads it
 * back (and discards it) to ensure the write is complete.
 */
static inline void flush64(const volatile uint64_t *ptr, uint64_t val)
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
        "st.d %1, %0\n"
        "ld.d %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
#endif
}

#endif // MEMRW64_H
