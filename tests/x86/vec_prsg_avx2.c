// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester.
//
// AVX2 kernels for the PRSG vector fill/check primitives. See
// tests/vec_prsg.h for the kernel contract and tests/vec_prsg.c for the
// dispatch wrappers and scalar fallbacks.
//
// The kernels are written in inline assembly, so the compiler needs no
// AVX support and no intrinsics headers are required. Each kernel issues
// vzeroupper itself before returning to legacy SSE code.
//
// Forward xorshift:  x ^= x << 13;  x ^= x >> 7;  x ^= x << 17;
// The backward step inverts each stage in reverse order by repeated squaring:
// y = x ^ (x << s) is undone by x = y; x ^= x << s; x ^= x << 2s; x ^= x << 4s;
// ... doubling the shift until it exceeds the word width.

#if defined(__x86_64__)

#include <stdbool.h>
#include <stddef.h>

#include "vec_prsg.h"

// The kernels end with vzeroupper, which zeroes the upper half of ALL ymm
// registers, not just those in the clobber lists. This is only safe because
// the compiler cannot hold values there when AVX code generation is disabled.
#ifdef __AVX__
#error "vec_prsg_avx2.c must be compiled without -mavx*"
#endif

#define AVX2_STEP_FWD(s, t)                             \
        "vpsllq   $13, " s ", " t "            \n\t"    \
        "vpxor    " t ", " s ", " s "          \n\t"    \
        "vpsrlq   $7, " s ", " t "             \n\t"    \
        "vpxor    " t ", " s ", " s "          \n\t"    \
        "vpsllq   $17, " s ", " t "            \n\t"    \
        "vpxor    " t ", " s ", " s "          \n\t"

#define AVX2_UNDO_STEP(s, t, op, shift)                 \
        op "      $" #shift ", " s ", " t "    \n\t"    \
        "vpxor    " t ", " s ", " s "          \n\t"

#define AVX2_STEP_BACK(s, t)                            \
        AVX2_UNDO_STEP(s, t, "vpsllq", 17)              \
        AVX2_UNDO_STEP(s, t, "vpsllq", 34)              \
        AVX2_UNDO_STEP(s, t, "vpsrlq", 7)               \
        AVX2_UNDO_STEP(s, t, "vpsrlq", 14)              \
        AVX2_UNDO_STEP(s, t, "vpsrlq", 28)              \
        AVX2_UNDO_STEP(s, t, "vpsrlq", 56)              \
        AVX2_UNDO_STEP(s, t, "vpsllq", 13)              \
        AVX2_UNDO_STEP(s, t, "vpsllq", 26)              \
        AVX2_UNDO_STEP(s, t, "vpsllq", 52)

void vec_fill_avx2(vec_state_t *st, testword_t *p, size_t nblocks, bool splat)
{
    size_t i = 0;

    __asm__ __volatile__ (
        "vmovdqu  (%[st]), %%ymm0      \n\t"
        "0:                            \n\t"
        "vmovntdq %%ymm0, (%[p])       \n\t"
        "testl    %[splat], %[splat]   \n\t"
        "jnz      1f                   \n\t"
        AVX2_STEP_FWD("%%ymm0", "%%ymm1")
        "1:                            \n\t"
        "addq     $32, %[p]            \n\t"
        "incq     %[i]                 \n\t"
        "cmpq     %[n], %[i]           \n\t"
        "jb       0b                   \n\t"
        "vmovdqu  %%ymm0, (%[st])      \n\t"
        "vzeroupper                    \n\t"
        "sfence                        \n\t"
        : [p] "+r" (p), [i] "+r" (i)
        : [st] "r" (st), [n] "r" (nblocks), [splat] "r" ((int)splat)
        : "xmm0", "xmm1", "memory", "cc"
    );
}

size_t vec_scan_fwd_avx2(vec_state_t *st, testword_t *p, size_t nblocks, bool splat)
{
    size_t done = 0;

    __asm__ __volatile__ (
        "vmovdqu  (%[st]), %%ymm0          \n\t"
        "vpcmpeqd %%ymm3, %%ymm3, %%ymm3   \n\t"    // all ones
        "0:                                \n\t"
        "vmovdqa  (%[p]), %%ymm1           \n\t"
        "vpcmpeqq %%ymm0, %%ymm1, %%ymm2   \n\t"
        "vpmovmskb %%ymm2, %%eax           \n\t"
        "cmpl     $-1, %%eax               \n\t"
        "jne      2f                       \n\t"
        "vpxor    %%ymm3, %%ymm0, %%ymm1   \n\t"    // complement
        "vmovntdq %%ymm1, (%[p])           \n\t"
        "testl    %[splat], %[splat]       \n\t"
        "jnz      1f                       \n\t"
        AVX2_STEP_FWD("%%ymm0", "%%ymm1")
        "1:                                \n\t"
        "addq     $32, %[p]                \n\t"
        "incq     %[done]                  \n\t"
        "cmpq     %[n], %[done]            \n\t"
        "jb       0b                       \n\t"
        "2:                                \n\t"
        "vmovdqu  %%ymm0, (%[st])          \n\t"
        "vzeroupper                        \n\t"
        "sfence                            \n\t"
        : [p] "+r" (p), [done] "+r" (done)
        : [st] "r" (st), [n] "r" (nblocks), [splat] "r" ((int)splat)
        : "rax", "xmm0", "xmm1", "xmm2", "xmm3", "memory", "cc"
    );
    return done;
}

size_t vec_scan_rev_avx2(vec_state_t *st, testword_t *q, size_t nblocks, bool splat)
{
    size_t done = 0;

    __asm__ __volatile__ (
        "vmovdqu  (%[st]), %%ymm0          \n\t"
        "vpcmpeqd %%ymm3, %%ymm3, %%ymm3   \n\t"    // all ones
        "0:                                \n\t"
        "testl    %[splat], %[splat]       \n\t"
        "jnz      1f                       \n\t"
        AVX2_STEP_BACK("%%ymm0", "%%ymm1")
        "1:                                \n\t"
        "vmovdqa  (%[q]), %%ymm1           \n\t"
        "vpxor    %%ymm3, %%ymm0, %%ymm2   \n\t"    // expected = complement
        "vpcmpeqq %%ymm2, %%ymm1, %%ymm2   \n\t"
        "vpmovmskb %%ymm2, %%eax           \n\t"
        "cmpl     $-1, %%eax               \n\t"
        "jne      2f                       \n\t"
        "vmovntdq %%ymm0, (%[q])           \n\t"    // restore the pattern
        "subq     $32, %[q]                \n\t"
        "incq     %[done]                  \n\t"
        "cmpq     %[n], %[done]            \n\t"
        "jb       0b                       \n\t"
        "2:                                \n\t"
        "vmovdqu  %%ymm0, (%[st])          \n\t"
        "vzeroupper                        \n\t"
        "sfence                            \n\t"
        : [q] "+r" (q), [done] "+r" (done)
        : [st] "r" (st), [n] "r" (nblocks), [splat] "r" ((int)splat)
        : "rax", "xmm0", "xmm1", "xmm2", "xmm3", "memory", "cc"
    );
    return done;
}

#endif // __x86_64__
