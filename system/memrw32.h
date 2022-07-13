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
    __asm__ __volatile__(
        "movl %1, %0"
        : "=r" (val)
        : "m" (*ptr)
        : "memory"
    );
    return val;
}

/**
 * Writes val to the 32-bit memory location pointed to by ptr.
 */
static inline void write32(const volatile uint32_t *ptr, uint32_t val)
{
    __asm__ __volatile__(
        "movl %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
}

/**
 * Writes val to the 32-bit memory location pointed to by ptr, using non-temporal hint.
 */
static inline void write32_nt(const volatile uint32_t *ptr, uint32_t val)
{
    __asm__ __volatile__(
        "movntil %1, %0"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
}

/**
 * Writes val to the 32-bit memory location pointed to by ptr. Reads it
 * back (and discards it) to ensure the write is complete.
 */
static inline void flush32(const volatile uint32_t *ptr, uint32_t val)
{
    __asm__ __volatile__(
        "movl %1, %0\n"
        "movl %0, %1"
        :
        : "m" (*ptr),
          "r" (val)
        : "memory"
    );
}

#endif // MEMRW32_H
