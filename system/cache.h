// SPDX-License-Identifier: GPL-2.0
#ifndef CACHE_H
#define CACHE_H
/**
 * \file
 *
 * Provides functions to enable, disable, and flush the CPU caches.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#ifdef __loongarch_lp64
#include <larchintrin.h>
#include "string.h"
#define cache_op(op,addr)     \
    __asm__ __volatile__(     \
    "cacop %0, %1\n"          \
    :                         \
    : "i" (op), "ZC" (*(unsigned char *)(addr)))
static inline void cache_flush(void);
#endif

/**
 * Disable the CPU caches.
 */
static inline void cache_off(void)
{
#if defined(__x86_64__)
    __asm__ __volatile__ ("\t"
        "movq   %%cr0, %%rax        \n\t"
        "orl    $0x40000000, %%eax  \n\t"  /* Set CD */
        "movq   %%rax, %%cr0        \n\t"
        "wbinvd                     \n"
        : /* no outputs */
        : /* no inputs */
        : "rax", "memory"
    );
#elif defined(__i386__)
    __asm__ __volatile__ ("\t"
        "movl   %%cr0, %%eax        \n\t"
        "orl    $0x40000000, %%eax  \n\t"  /* Set CD */
        "movl   %%eax, %%cr0        \n\t"
        "wbinvd                     \n"
        : /* no outputs */
        : /* no inputs */
        : "eax", "memory"
    );
#elif defined(__loongarch_lp64)
    cache_flush();
    __csrxchg_d(0, 3 << 4, 0x181);
#endif
}

/**
 * Enable the CPU caches.
 */
static inline void cache_on(void)
{
#if defined(__x86_64__)
    __asm__ __volatile__ ("\t"
        "movq   %%cr0, %%rax        \n\t"
        "andl   $0x9fffffff, %%eax  \n\t" /* Clear CD and NW */
        "movq   %%rax, %%cr0        \n"
        : /* no outputs */
        : /* no inputs */
        : "rax", "memory"
    );
#elif defined(__i386__)
    __asm__ __volatile__ ("\t"
        "movl   %%cr0, %%eax        \n\t"
        "andl   $0x9fffffff, %%eax  \n\t" /* Clear CD and NW */
        "movl   %%eax, %%cr0        \n"
        : /* no outputs */
        : /* no inputs */
        : "eax", "memory"
    );
#elif defined(__loongarch_lp64)
    cache_flush();
    __csrxchg_d(1 << 4, 3 << 4, 0x181);
#endif
}

/**
 * Flush the CPU caches.
 */
static inline void cache_flush(void)
{
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__ ("\t"
        "wbinvd\n"
        : /* no outputs */
        : /* no inputs */
        : "memory"
    );
#elif defined (__loongarch_lp64)
    uint64_t cache_present, cache_info_reg;
    /*detect_max_cache_level*/
    if (__cpucfg(0x10) & (1 << 10)) {
	cache_present = 3; //L3 unified cache
    } else if (__cpucfg(0x10) & (1 << 3)) {
	cache_present = 2; //L2 unified cache
    } else if (__cpucfg(0x10) & (1 << 0)) {
	cache_present = 1; //L1 data cache
    } else {
	return; //No Cache present
    }
    cache_info_reg = 0x11 + cache_present; //cache last leaf

    uint64_t ways = (__cpucfg(cache_info_reg) & 0xFFFF) + 1;
    uint64_t sets = 1 << ((__cpucfg(cache_info_reg) >> 16) & 0xFF);
    uint64_t line_size = 1 << ((__cpucfg(cache_info_reg) >> 24) & 0x7F);
    uint64_t va, i, j;
    uint64_t cpu_module[1];
    va = 0;

    cpu_module[0] = (uint64_t)__iocsrrd_d(0x20);
    if (strstr((const char *)cpu_module, "3A6000")) {
        uint8_t old_sc_cfg;
        old_sc_cfg = __iocsrrd_b(0x280);
        __iocsrwr_b(0x1, 0x280);
        for (i = 0; i < (ways * 3); i++) {
            for (j = 0; j < sets; j++) {
                *(volatile uint32_t *)va;
                va += line_size;
            }
        }
        __iocsrwr_b(old_sc_cfg, 0x280);
    } else {
        for (i = 0; i < sets; i++) {
            for (j = 0; j < ways; j++) {
		switch (cache_present) {
		    case 1:
		        cache_op(0x9,va); //Flush L1
			break;
		    case 2:
		        cache_op(0xA,va); //Flush L2
			break;
		    case 3:
		        cache_op(0xB,va); //Flush L3
			break;
		}
                va++;
            }
            va -= ways;
            va += line_size;
        }
    }
#endif
}

/**
 * Flushes the single cache line containing addr from all levels of the CPU
 * cache hierarchy, so the next access to that line reaches DRAM.
 *
 * Only defined on x86, where CLFLUSH is part of the SSE2 baseline. Used by the
 * rowhammer test (test #11) to force a DRAM row activation on every aggressor
 * access. On 32-bit x86 the caller must first confirm CLFLUSH is supported
 * (cpuid_info.flags.cflush); on x86-64 it is always present. The faster,
 * unordered CLFLUSHOPT variant is selected separately inside the x86-64 hammer
 * engine (tests/x86/rowhammer_x86.c) when the CPU supports it.
 */
#if defined(__i386__) || defined(__x86_64__)
static inline void cache_line_flush(const volatile void *addr)
{
    __asm__ __volatile__ (
        "clflush %0"
        : "+m" (*(volatile char *)addr)
        : /* no inputs */
        : "memory"
    );
}
#endif

/**
 * A full memory barrier ordering all prior loads and stores before any that
 * follow. On x86 this is MFENCE (SSE2 baseline), so only execute it once SSE2
 * is known to be present; on LoongArch it is a full data barrier.
 */
static inline void mem_barrier(void)
{
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__ ("mfence" : : : "memory");
#elif defined(__loongarch_lp64)
    __asm__ __volatile__ ("dbar 0" : : : "memory");
#endif
}

/**
 * A load fence used to bracket rdtsc timing so reads are not reordered across
 * the measurement. On x86 this is LFENCE (SSE2 baseline).
 */
static inline void load_fence(void)
{
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__ ("lfence" : : : "memory");
#elif defined(__loongarch_lp64)
    __asm__ __volatile__ ("dbar 0" : : : "memory");
#endif
}

#endif // CACHE_H
