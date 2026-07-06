// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester.
//
// x86-64 hammer engine for the rowhammer test (test #11). See tests/rowhammer.h
// for the API contract and tests/rowhammer.c for the orchestration.
//
// The engine issues DRAM row activations by reading each aggressor line and
// then evicting it, so the next read misses the cache and re-activates the row.
// CLFLUSHOPT (unordered, higher throughput) is used when the CPU reports it in
// CPUID.(EAX=7,ECX=0):EBX bit 23; otherwise plain CLFLUSH (SSE2 baseline, always
// present on x86-64). A single MFENCE bounds each burst rather than serialising
// every access, which keeps the activation cadence tight.

#if defined(__x86_64__)

#include <stdbool.h>
#include <stdint.h>

#include "cache.h"
#include "cpuid.h"
#include "tsc.h"

#include "test.h"

#include "rowhammer.h"

//------------------------------------------------------------------------------
// Private state
//------------------------------------------------------------------------------

static bool rh_use_opt = false;     // CLFLUSHOPT available?

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void rh_engine_init(void)
{
    uint32_t a, b, c, d;

    rh_use_opt = false;
    if (cpuid_info.max_cpuid >= 7) {
        cpuid(7, 0, &a, &b, &c, &d);
        rh_use_opt = (b >> 23) & 1;     // CLFLUSHOPT
    }
}

bool rh_engine_precise(void)
{
    // CLFLUSH is part of the SSE2 baseline, so the precise engine is always
    // available on x86-64.
    return true;
}

void rh_hammer_seq(void *const *seq, uint32_t seq_len, uint64_t reps)
{
    if (seq_len == 0 || reps == 0) {
        return;
    }

    // Two near-identical loops selected once, so the hot path has no per-access
    // branch. Each iteration: load the next aggressor pointer, read it (row
    // activation), flush the line (force re-activation next time).
    if (rh_use_opt) {
        __asm__ __volatile__ (
            "1:                              \n\t"
            "   xorq    %%r8, %%r8           \n\t"
            "2:                              \n\t"
            "   movq    (%[seq],%%r8,8), %%r9\n\t"
            "   movl    (%%r9), %%eax        \n\t"
            "   clflushopt (%%r9)            \n\t"
            "   incq    %%r8                 \n\t"
            "   cmpq    %[len], %%r8         \n\t"
            "   jb      2b                   \n\t"
            "   decq    %[reps]              \n\t"
            "   jnz     1b                   \n\t"
            "   mfence                       \n\t"
            : [reps] "+r" (reps)
            : [seq] "r" (seq), [len] "r" ((uint64_t)seq_len)
            : "rax", "r8", "r9", "cc", "memory"
        );
    } else {
        __asm__ __volatile__ (
            "1:                              \n\t"
            "   xorq    %%r8, %%r8           \n\t"
            "2:                              \n\t"
            "   movq    (%[seq],%%r8,8), %%r9\n\t"
            "   movl    (%%r9), %%eax        \n\t"
            "   clflush (%%r9)               \n\t"
            "   incq    %%r8                 \n\t"
            "   cmpq    %[len], %%r8         \n\t"
            "   jb      2b                   \n\t"
            "   decq    %[reps]              \n\t"
            "   jnz     1b                   \n\t"
            "   mfence                       \n\t"
            : [reps] "+r" (reps)
            : [seq] "r" (seq), [len] "r" ((uint64_t)seq_len)
            : "rax", "r8", "r9", "cc", "memory"
        );
    }
}

uint64_t rh_probe_cycles(volatile void *addr)
{
    uint64_t t0, t1;

    mem_barrier();
    load_fence();
    t0 = get_tsc();
    load_fence();

    __asm__ __volatile__ ("movl (%0), %%eax" : : "r" (addr) : "eax", "memory");
    if (rh_use_opt) {
        __asm__ __volatile__ ("clflushopt %0" : "+m" (*(volatile char *)addr) : : "memory");
    } else {
        __asm__ __volatile__ ("clflush %0"    : "+m" (*(volatile char *)addr) : : "memory");
    }

    load_fence();
    t1 = get_tsc();
    load_fence();

    return t1 - t0;
}

#endif // __x86_64__
