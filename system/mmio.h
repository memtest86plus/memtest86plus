// SPDX-License-Identifier: GPL-2.0
#ifndef MMIO_H
#define MMIO_H
/**
 * \file
 *
 * Provides macro definitions for the MMIO instructions
 *
 * This file is not meant to be obfuscating: it's just complicated
 * to (a) handle it all in a way that makes gcc able to optimize it
 * as well as possible and (b) trying to avoid writing the same thing
 * over and over again with slight variations and possibly making a
 * mistake somewhere.
 */

#if defined(__loongarch_lp64)
static __inline uint8_t mmio_read8(uint8_t *addr)
{
    uint8_t val;

    __asm__ __volatile__ (
        "li.d $t0, 0x28\n\t"
        "csrwr $t0, 0x0\n\t"
        "ld.b %0, %1\n\t"
        "csrwr $t0, 0x0\n\t"
        : "=r" (val)
        : "m" (*addr)
        : "$t0", "memory"
    );
    return val;
}

static __inline uint16_t mmio_read16(uint16_t *addr)
{
    uint16_t val;

    __asm__ __volatile__ (
        "li.d $t0, 0x28\n\t"
        "csrwr $t0, 0x0\n\t"
        "ld.h %0, %1\n\t"
        "csrwr $t0, 0x0\n\t"
        : "=r" (val)
        : "m" (*addr)
        : "$t0", "memory"
    );
    return val;
}

static __inline uint32_t mmio_read32(uint32_t *addr)
{
    uint32_t val;

    __asm__ __volatile__ (
        "li.d $t0, 0x28\n\t"
        "csrwr $t0, 0x0\n\t"
        "ld.w %0, %1\n\t"
        "csrwr $t0, 0x0\n\t"
        : "=r" (val)
        : "m" (*addr)
        : "$t0", "memory"
    );
    return val;
}

static __inline uint64_t mmio_read64(uint64_t *addr)
{
    uint64_t val;

    __asm__ __volatile__ (
        "li.d $t0, 0x28\n\t"
        "csrwr $t0, 0x0\n\t"
        "ld.d %0, %1\n\t"
        "csrwr $t0, 0x0\n\t"
        : "=r" (val)
        : "m" (*addr)
        : "$t0", "memory"
    );
    return val;
}

static __inline void mmio_write8(uint8_t *addr, uint8_t val)
{
    __asm__ __volatile__ (
        "li.d $t0, 0x28\n\t"
        "csrwr $t0, 0x0\n\t"
        "st.b %z0, %1\n\t"
        "csrwr $t0, 0x0\n\t"
        :
        : "Jr" (val), "m" (*addr)
        : "$t0", "memory"
    );
}

static __inline void mmio_write16(uint16_t *addr, uint16_t val)
{
    __asm__ __volatile__ (
        "li.d $t0, 0x28\n\t"
        "csrwr $t0, 0x0\n\t"
        "st.h %z0, %1\n\t"
        "csrwr $t0, 0x0\n\t"
        :
        : "Jr" (val), "m" (*addr)
        : "$t0", "memory"
    );
}

static __inline void mmio_write32(uint32_t *addr, uint32_t val)
{
    __asm__ __volatile__ (
        "li.d $t0, 0x28\n\t"
        "csrwr $t0, 0x0\n\t"
        "st.w %z0, %1\n\t"
        "csrwr $t0, 0x0\n\t"
        :
        : "Jr" (val), "m" (*addr)
        : "$t0", "memory"
    );
}

static __inline void mmio_write64(uint64_t *addr, uint64_t val)
{
    __asm__ __volatile__ (
        "li.d $t0, 0x28\n\t"
        "csrwr $t0, 0x0\n\t"
        "st.d %z0, %1\n\t"
        "csrwr $t0, 0x0\n\t"
        :
        : "Jr" (val), "m" (*addr)
        : "$t0", "memory"
    );
}
#endif
#endif
