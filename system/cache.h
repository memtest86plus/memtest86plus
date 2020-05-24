// SPDX-License-Identifier: GPL-2.0
#ifndef CACHE_H
#define CACHE_H
/*
 * Provides functions to enable/disable the CPU caches.
 *
 * Copyright (C) 2020 Martin Whitaker.
 */

/*
 * Disable the CPU caches.
 */
static inline void cache_off(void)
{
#ifdef __x86_64__
    __asm__ __volatile__ ("\t"
        "movq   %%cr0, %%rax        \n\t"
        "orl    $0x40000000, %%eax  \n\t"  /* Set CD */
        "movq   %%rax, %%cr0        \n\t"
        "wbinvd                     \n"
        : /* no outputs */
        : /* no inputs */
        : "rax"
    );
#else
    __asm__ __volatile__ ("\t"
        "movl   %%cr0, %%eax        \n\t"
        "orl    $0x40000000, %%eax  \n\t"  /* Set CD */
        "movl   %%eax, %%cr0        \n\t"
        "wbinvd                     \n"
        : /* no outputs */
        : /* no inputs */
        : "eax"
    );
#endif
}

/*
 * Enable the CPU caches.
 */
static inline void cache_on(void)
{
#ifdef __x86_64__
    __asm__ __volatile__ ("\t"
        "movq   %%cr0, %%rax        \n\t"
        "andl   $0x9fffffff, %%eax  \n\t" /* Clear CD and NW */ 
        "movq   %%rax, %%cr0        \n"
        : /* no outputs */
        : /* no inputs */
        : "rax"
    );
#else
    __asm__ __volatile__ ("\t"
        "movl   %%cr0, %%eax        \n\t"
        "andl   $0x9fffffff, %%eax  \n\t" /* Clear CD and NW */ 
        "movl   %%eax, %%cr0        \n"
        : /* no outputs */
        : /* no inputs */
        : "eax"
    );
#endif
}

#endif // CACHE_H
