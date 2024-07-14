// SPDX-License-Identifier: GPL-2.0
#ifndef MEMRW_H
#define MEMRW_H
/**
 * \file
 *
 * Provides some 32/64-bit memory access functions. These stop the compiler
 * optimizing accesses which need to be ordered and atomic. Mostly used
 * for accessing memory-mapped hardware registers.
 *
 *//*
 * Copyright (C) 2021-2022 Martin Whitaker.
 * Copyright (C) 2024 Lionel Debroux.
 */

#include <stdint.h>

#define __MEMRW_SUFFIX_32BIT "l"
#define __MEMRW_SUFFIX_64BIT "q"
#define __MEMRW_READ_INSTRUCTIONS(bitwidth) "mov" __MEMRW_SUFFIX_##bitwidth##BIT " %1, %0"
#define __MEMRW_WRITE_INSTRUCTIONS(bitwidth) "mov" __MEMRW_SUFFIX_##bitwidth##BIT " %1, %0"
#define __MEMRW_FLUSH_INSTRUCTIONS(bitwidth) "mov" __MEMRW_SUFFIX_##bitwidth##BIT " %1, %0; mov" __MEMRW_SUFFIX_##bitwidth##BIT " %0, %1"

#define __MEMRW_READ_FUNC(bitwidth) \
static inline uint##bitwidth##_t read##bitwidth(const volatile uint##bitwidth##_t *ptr) \
{ \
    uint##bitwidth##_t val; \
    __asm__ __volatile__( \
        __MEMRW_READ_INSTRUCTIONS(bitwidth) \
        : "=r" (val) \
        : "m" (*ptr) \
        : "memory" \
    ); \
    return val; \
}

#define __MEMRW_WRITE_FUNC(bitwidth) \
static inline void write##bitwidth(const volatile uint##bitwidth##_t *ptr, uint##bitwidth##_t val) \
{ \
    __asm__ __volatile__( \
	__MEMRW_WRITE_INSTRUCTIONS(bitwidth) \
        : \
        : "m" (*ptr), \
          "r" (val) \
        : "memory" \
    ); \
}

#define __MEMRW_FLUSH_FUNC(bitwidth) \
static inline void flush##bitwidth(const volatile uint##bitwidth##_t *ptr, uint##bitwidth##_t val) \
{ \
    __asm__ __volatile__( \
	__MEMRW_FLUSH_INSTRUCTIONS(bitwidth) \
        : \
        : "m" (*ptr), \
          "r" (val) \
        : "memory" \
    ); \
}

/**
 * Reads and returns the value stored in the 32-bit memory location pointed to by ptr.
 */
__MEMRW_READ_FUNC(32)
/**
 * Reads and returns the value stored in the 64-bit memory location pointed to by ptr.
 */
__MEMRW_READ_FUNC(64)

/**
 * Writes val to the 32-bit memory location pointed to by ptr.
 */
__MEMRW_WRITE_FUNC(32)
/**
 * Writes val to the 64-bit memory location pointed to by ptr.
 */
__MEMRW_WRITE_FUNC(64)

/**
 * Writes val to the 32-bit memory location pointed to by ptr. Only returns when the write is complete.
 */
__MEMRW_FLUSH_FUNC(32)
/**
 * Writes val to the 64-bit memory location pointed to by ptr. Only returns when the write is complete.
 */
__MEMRW_FLUSH_FUNC(64)

#endif // MEMRW_H
