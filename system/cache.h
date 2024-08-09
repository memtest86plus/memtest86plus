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
    if (!(__cpucfg(0x10) & (1 << 10))) {
        return; // No L3
    }
    uint64_t ways = (__cpucfg(0x14) & 0xFFFF) + 1;
    uint64_t sets = 1 << ((__cpucfg(0x14) >> 16) & 0xFF);
    uint64_t line_size = 1 << ((__cpucfg(0x14) >> 24) & 0x7F);
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
                cache_op(0xB, va);
                va++;
            }
            va -= ways;
            va += line_size;
        }
    }
#endif
}

#endif // CACHE_H
