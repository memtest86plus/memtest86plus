// SPDX-License-Identifier: GPL-2.0
#ifndef MEMRW_H
#define MEMRW_H
/**
 * \file
 *
 * Provides some 8/16/32/64-bit memory access functions. These stop the compiler
 * optimizing accesses which need to be ordered and atomic. Mostly used
 * for accessing memory-mapped hardware registers.
 *
 *//*
 * Copyright (C) 2021-2022 Martin Whitaker.
 * Copyright (C) 2024 Lionel Debroux.
 */

#include <stdint.h>

#if defined(__i386__) || defined(__x86_64__)

#define __MEMRW_SUFFIX_8BIT  "b"
#define __MEMRW_SUFFIX_16BIT "w"
#define __MEMRW_SUFFIX_32BIT "l"
#define __MEMRW_SUFFIX_64BIT "q"
#define __MEMRW_READ_INSTRUCTIONS(bitwidth) "mov" __MEMRW_SUFFIX_##bitwidth##BIT " %1, %0"
#define __MEMRW_WRITE_INSTRUCTIONS(bitwidth) "mov" __MEMRW_SUFFIX_##bitwidth##BIT " %1, %0"
#define __MEMRW_FLUSH_INSTRUCTIONS(bitwidth) "mov" __MEMRW_SUFFIX_##bitwidth##BIT " %1, %0; mov" __MEMRW_SUFFIX_##bitwidth##BIT " %0, %1"

#elif defined(__loongarch_lp64)

#define __MEMRW_SUFFIX_8BIT  "b"
#define __MEMRW_SUFFIX_16BIT "h"
#define __MEMRW_SUFFIX_32BIT "w"
#define __MEMRW_SUFFIX_64BIT "d"
#define __MEMRW_READ_INSTRUCTIONS(bitwidth) "ld." __MEMRW_SUFFIX_##bitwidth##BIT " %0, %1"
#define __MEMRW_WRITE_INSTRUCTIONS(bitwidth) "st." __MEMRW_SUFFIX_##bitwidth##BIT " %1, %0"
#define __MEMRW_FLUSH_INSTRUCTIONS(bitwidth) "st." __MEMRW_SUFFIX_##bitwidth##BIT " %1, %0; dbar 0"

#elif defined(__aarch64__)

#define __MEMRW_SUFFIX_8BIT  "b"
#define __MEMRW_SUFFIX_16BIT "h"
#define __MEMRW_SUFFIX_32BIT ""
#define __MEMRW_SUFFIX_64BIT ""
#define __MEMRW_REG_8BIT  "w"
#define __MEMRW_REG_16BIT "w"
#define __MEMRW_REG_32BIT "w"
#define __MEMRW_REG_64BIT "x"
#define __MEMRW_READ_INSTRUCTIONS(bitwidth) \
    "ldr" __MEMRW_SUFFIX_##bitwidth##BIT " %" __MEMRW_REG_##bitwidth##BIT "0, %1"
#define __MEMRW_WRITE_INSTRUCTIONS(bitwidth) \
    "str" __MEMRW_SUFFIX_##bitwidth##BIT " %" __MEMRW_REG_##bitwidth##BIT "1, %0"
#define __MEMRW_FLUSH_INSTRUCTIONS(bitwidth) \
    "str" __MEMRW_SUFFIX_##bitwidth##BIT " %" __MEMRW_REG_##bitwidth##BIT "1, %0; dsb sy"

#endif

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
 * Reads and returns the value stored in the 8-bit memory location pointed to by ptr.
 */
__MEMRW_READ_FUNC(8)
/**
 * Reads and returns the value stored in the 16-bit memory location pointed to by ptr.
 */
__MEMRW_READ_FUNC(16)
/**
 * Reads and returns the value stored in the 32-bit memory location pointed to by ptr.
 */
__MEMRW_READ_FUNC(32)
/**
 * Reads and returns the value stored in the 64-bit memory location pointed to by ptr.
 */
__MEMRW_READ_FUNC(64)

/**
 * Writes val to the 8-bit memory location pointed to by ptr.
 */
__MEMRW_WRITE_FUNC(8)
/**
 * Writes val to the 16-bit memory location pointed to by ptr.
 */
__MEMRW_WRITE_FUNC(16)
/**
 * Writes val to the 32-bit memory location pointed to by ptr.
 */
__MEMRW_WRITE_FUNC(32)
/**
 * Writes val to the 64-bit memory location pointed to by ptr.
 */
__MEMRW_WRITE_FUNC(64)

/**
 * Writes val to the 8-bit memory location pointed to by ptr. Only returns when the write is complete.
 */
__MEMRW_FLUSH_FUNC(8)
/**
 * Writes val to the 16-bit memory location pointed to by ptr. Only returns when the write is complete.
 */
__MEMRW_FLUSH_FUNC(16)
/**
 * Writes val to the 32-bit memory location pointed to by ptr. Only returns when the write is complete.
 */
__MEMRW_FLUSH_FUNC(32)
/**
 * Writes val to the 64-bit memory location pointed to by ptr. Only returns when the write is complete.
 */
__MEMRW_FLUSH_FUNC(64)

#if defined(__i386__) || defined(__x86_64__)
// The BDA helpers below dereference small constant addresses, which GCC's
// -Warray-bounds heuristic flags as likely null derefs. Suppress locally;
// the accesses are intentional reads/writes of the x86 BIOS Data Area.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
/**
 * Reads a 16-bit value from the x86 BIOS Data Area at the given linear address.
 */
static inline uint16_t bda_read16(uintptr_t address)
{
    return read16((const volatile uint16_t *)address);
}

/**
 * Writes a 16-bit value to the x86 BIOS Data Area at the given linear address.
 */
static inline void bda_write16(uintptr_t address, uint16_t value)
{
    write16((const volatile uint16_t *)address, value);
}
#pragma GCC diagnostic pop
#endif

#endif // MEMRW_H
