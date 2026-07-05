// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester.
//
// AVX2 kernels for the moving inversions RNG-sequence test. See
// tests/mov_inv_rng.h for the kernel contract and tests/mov_inv_rng.c for
// the test itself.
//
// The kernels are written in inline assembly, so the compiler needs no
// AVX support and no intrinsics headers are required. Each kernel issues
// vzeroupper itself before returning to legacy SSE code.
//
// Forward xorshift:  x ^= x << 13;  x ^= x >> 7;  x ^= x << 17;
// The backward step inverts each stage in reverse order, using the identity
// that y = x ^ (x << s) is undone by x = y ^ (y << s) ^ (y << 2s) ^ ...

#if defined(__x86_64__)

#include <stdbool.h>
#include <stddef.h>

#include "mov_inv_rng.h"

#define AVX2_STEP_FWD(s, t)                             \
        "vpsllq   $13, " s ", " t "            \n\t"    \
        "vpxor    " t ", " s ", " s "          \n\t"    \
        "vpsrlq   $7, " s ", " t "             \n\t"    \
        "vpxor    " t ", " s ", " s "          \n\t"    \
        "vpsllq   $17, " s ", " t "            \n\t"    \
        "vpxor    " t ", " s ", " s "          \n\t"

#define AVX2_UNDO_SHIFT(s, t, op, shift, terms)         \
        op "      $" #shift ", " s ", " t "    \n\t"    \
        "vpxor    " t ", " s ", " s "          \n\t"    \
        ".rept " #terms "                      \n\t"    \
        op "      $" #shift ", " t ", " t "    \n\t"    \
        "vpxor    " t ", " s ", " s "          \n\t"    \
        ".endr                                 \n\t"

#define AVX2_STEP_BACK(s, t)                            \
        AVX2_UNDO_SHIFT(s, t, "vpsllq", 17, 2)          \
        AVX2_UNDO_SHIFT(s, t, "vpsrlq", 7,  8)          \
        AVX2_UNDO_SHIFT(s, t, "vpsllq", 13, 3)

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
