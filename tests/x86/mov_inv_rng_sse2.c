// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester.
//
// SSE2 kernels for the moving inversions RNG-sequence test. See
// tests/mov_inv_rng.h for the kernel contract and tests/mov_inv_rng.c for
// the test itself.
//
// The kernels are written in inline assembly, so the compiler needs no
// SIMD support and no intrinsics headers are required.
//
// Forward xorshift:  x ^= x << 13;  x ^= x >> 7;  x ^= x << 17;
// The backward step inverts each stage in reverse order, using the identity
// that y = x ^ (x << s) is undone by x = y ^ (y << s) ^ (y << 2s) ^ ...

#if defined(__x86_64__)

#include <stdbool.h>
#include <stddef.h>

#include "mov_inv_rng.h"

#define SSE2_STEP_FWD(s, t)                     \
        "movdqa   " s ", " t "         \n\t"    \
        "psllq    $13, " t "           \n\t"    \
        "pxor     " t ", " s "         \n\t"    \
        "movdqa   " s ", " t "         \n\t"    \
        "psrlq    $7, " t "            \n\t"    \
        "pxor     " t ", " s "         \n\t"    \
        "movdqa   " s ", " t "         \n\t"    \
        "psllq    $17, " t "           \n\t"    \
        "pxor     " t ", " s "         \n\t"

#define SSE2_UNDO_SHIFT(s, t, op, shift, terms) \
        "movdqa   " s ", " t "         \n\t"    \
        ".rept " #terms "              \n\t"    \
        op "      $" #shift ", " t "   \n\t"    \
        "pxor     " t ", " s "         \n\t"    \
        ".endr                         \n\t"

#define SSE2_STEP_BACK(s, t)                    \
        SSE2_UNDO_SHIFT(s, t, "psllq", 17, 3)   \
        SSE2_UNDO_SHIFT(s, t, "psrlq", 7,  9)   \
        SSE2_UNDO_SHIFT(s, t, "psllq", 13, 4)

void vec_fill_sse2(vec_state_t *st, testword_t *p, size_t nblocks, bool splat)
{
    size_t i = 0;

    __asm__ __volatile__ (
        "movdqu    0(%[st]), %%xmm0    \n\t"
        "movdqu   16(%[st]), %%xmm1    \n\t"
        "0:                            \n\t"
        "movntdq  %%xmm0,  0(%[p])     \n\t"
        "movntdq  %%xmm1, 16(%[p])     \n\t"
        "testl    %[splat], %[splat]   \n\t"
        "jnz      1f                   \n\t"
        SSE2_STEP_FWD("%%xmm0", "%%xmm4")
        SSE2_STEP_FWD("%%xmm1", "%%xmm4")
        "1:                            \n\t"
        "addq     $32, %[p]            \n\t"
        "incq     %[i]                 \n\t"
        "cmpq     %[n], %[i]           \n\t"
        "jb       0b                   \n\t"
        "movdqu   %%xmm0,  0(%[st])    \n\t"
        "movdqu   %%xmm1, 16(%[st])    \n\t"
        "sfence                        \n\t"
        : [p] "+r" (p), [i] "+r" (i)
        : [st] "r" (st), [n] "r" (nblocks), [splat] "r" ((int)splat)
        : "xmm0", "xmm1", "xmm4", "memory", "cc"
    );
}

size_t vec_scan_fwd_sse2(vec_state_t *st, testword_t *p, size_t nblocks, bool splat)
{
    size_t done = 0;

    __asm__ __volatile__ (
        "movdqu    0(%[st]), %%xmm0    \n\t"
        "movdqu   16(%[st]), %%xmm1    \n\t"
        "pcmpeqd  %%xmm5, %%xmm5       \n\t"    // all ones
        "0:                            \n\t"
        "movdqa    0(%[p]), %%xmm2     \n\t"
        "movdqa   16(%[p]), %%xmm3     \n\t"
        "pcmpeqd  %%xmm0, %%xmm2       \n\t"
        "pcmpeqd  %%xmm1, %%xmm3       \n\t"
        "pand     %%xmm3, %%xmm2       \n\t"
        "pmovmskb %%xmm2, %%eax        \n\t"
        "cmpl     $0xFFFF, %%eax       \n\t"
        "jne      2f                   \n\t"
        "movdqa   %%xmm0, %%xmm2       \n\t"
        "movdqa   %%xmm1, %%xmm3       \n\t"
        "pxor     %%xmm5, %%xmm2       \n\t"
        "pxor     %%xmm5, %%xmm3       \n\t"
        "movntdq  %%xmm2,  0(%[p])     \n\t"
        "movntdq  %%xmm3, 16(%[p])     \n\t"
        "testl    %[splat], %[splat]   \n\t"
        "jnz      1f                   \n\t"
        SSE2_STEP_FWD("%%xmm0", "%%xmm4")
        SSE2_STEP_FWD("%%xmm1", "%%xmm4")
        "1:                            \n\t"
        "addq     $32, %[p]            \n\t"
        "incq     %[done]              \n\t"
        "cmpq     %[n], %[done]        \n\t"
        "jb       0b                   \n\t"
        "2:                            \n\t"
        "movdqu   %%xmm0,  0(%[st])    \n\t"
        "movdqu   %%xmm1, 16(%[st])    \n\t"
        "sfence                        \n\t"
        : [p] "+r" (p), [done] "+r" (done)
        : [st] "r" (st), [n] "r" (nblocks), [splat] "r" ((int)splat)
        : "rax", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "memory", "cc"
    );
    return done;
}

size_t vec_scan_rev_sse2(vec_state_t *st, testword_t *q, size_t nblocks, bool splat)
{
    size_t done = 0;

    __asm__ __volatile__ (
        "movdqu    0(%[st]), %%xmm0    \n\t"
        "movdqu   16(%[st]), %%xmm1    \n\t"
        "pcmpeqd  %%xmm5, %%xmm5       \n\t"    // all ones
        "0:                            \n\t"
        "testl    %[splat], %[splat]   \n\t"
        "jnz      1f                   \n\t"
        SSE2_STEP_BACK("%%xmm0", "%%xmm4")
        SSE2_STEP_BACK("%%xmm1", "%%xmm4")
        "1:                            \n\t"
        "movdqa   %%xmm0, %%xmm2       \n\t"
        "movdqa   %%xmm1, %%xmm3       \n\t"
        "pxor     %%xmm5, %%xmm2       \n\t"    // expected = complement
        "pxor     %%xmm5, %%xmm3       \n\t"
        "pcmpeqd   0(%[q]), %%xmm2     \n\t"
        "pcmpeqd  16(%[q]), %%xmm3     \n\t"
        "pand     %%xmm3, %%xmm2       \n\t"
        "pmovmskb %%xmm2, %%eax        \n\t"
        "cmpl     $0xFFFF, %%eax       \n\t"
        "jne      2f                   \n\t"
        "movntdq  %%xmm0,  0(%[q])     \n\t"    // restore the pattern
        "movntdq  %%xmm1, 16(%[q])     \n\t"
        "subq     $32, %[q]            \n\t"
        "incq     %[done]              \n\t"
        "cmpq     %[n], %[done]        \n\t"
        "jb       0b                   \n\t"
        "2:                            \n\t"
        "movdqu   %%xmm0,  0(%[st])    \n\t"
        "movdqu   %%xmm1, 16(%[st])    \n\t"
        "sfence                        \n\t"
        : [q] "+r" (q), [done] "+r" (done)
        : [st] "r" (st), [n] "r" (nblocks), [splat] "r" ((int)splat)
        : "rax", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "memory", "cc"
    );
    return done;
}

#endif // __x86_64__
