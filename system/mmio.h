// SPDX-License-Identifier: GPL-2.0
#ifndef MMIO_H
#define MMIO_H
/**
 * \file
 *
 * Provides some macro definitions for the 8/16/32/64-bit MMIO access.
 * These stop the compiler optimizing accesses which need to be ordered and
 * atomic. Only used for accessing memory-mapped hardware registers, IO and
 * memory spaces.
 *//*
 * Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.
 *
 */

#include <stdint.h>

#if defined(__loongarch_lp64)

#define __MMIORW_SUFFIX_8BIT  "b"
#define __MMIORW_SUFFIX_16BIT "h"
#define __MMIORW_SUFFIX_32BIT "w"
#define __MMIORW_SUFFIX_64BIT "d"

#define __MMIORW_READ_INSTRUCTIONS(bitwidth) \
"li.d $t0, 0x28; csrwr $t0, 0x0;" "ld." __MMIORW_SUFFIX_##bitwidth##BIT " %0, %1; csrwr $t0, 0x0"

#define __MMIORW_WRITE_INSTRUCTIONS(bitwidth) \
"li.d $t0, 0x28; csrwr $t0, 0x0;" "st." __MMIORW_SUFFIX_##bitwidth##BIT " %1, %0; csrwr $t0, 0x0"

#define __MMIORW_READ_WRITE_CLOBBER "$t0", "memory"

#endif

#define __MMIORW_READ_FUNC(bitwidth) \
static inline uint##bitwidth##_t mmio_read##bitwidth(const volatile uint##bitwidth##_t *ptr) \
{ \
    uint##bitwidth##_t val; \
    __asm__ __volatile__ ( \
        __MMIORW_READ_INSTRUCTIONS(bitwidth) \
        : "=r" (val) \
        : "m" (*ptr) \
        : __MMIORW_READ_WRITE_CLOBBER \
    ); \
    return val; \
}

#define __MMIORW_WRITE_FUNC(bitwidth) \
static inline void mmio_write##bitwidth(const volatile uint##bitwidth##_t *ptr, uint##bitwidth##_t val) \
{ \
    __asm__ __volatile__ ( \
        __MMIORW_WRITE_INSTRUCTIONS(bitwidth) \
        : \
        :  "m" (*ptr), \
           "r" (val) \
        : __MMIORW_READ_WRITE_CLOBBER \
    ); \
}

/**
 * Reads and returns the value stored in the 8-bit memory IO location pointed to by ptr.
 */
__MMIORW_READ_FUNC(8)
/**
 * Reads and returns the value stored in the 16-bit memory IO location pointed to by ptr.
 */
__MMIORW_READ_FUNC(16)
/**
 * Reads and returns the value stored in the 32-bit memory IO location pointed to by ptr.
 */
__MMIORW_READ_FUNC(32)
/**
 * Reads and returns the value stored in the 64-bit memory IO location pointed to by ptr.
 */
__MMIORW_READ_FUNC(64)

/**
 * Writes val to the 8-bit memory IO location pointed to by ptr.
 */
__MMIORW_WRITE_FUNC(8)
/**
 * Writes val to the 16-bit memory IO location pointed to by ptr.
 */
__MMIORW_WRITE_FUNC(16)
/**
 * Writes val to the 32-bit memory IO location pointed to by ptr.
 */
__MMIORW_WRITE_FUNC(32)
/**
 * Writes val to the 64-bit memory IO location pointed to by ptr.
 */
__MMIORW_WRITE_FUNC(64)

#endif // MMIO_H
