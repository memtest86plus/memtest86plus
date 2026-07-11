// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester.
//
// NEON kernels for the PRSG vector fill/check primitives. See
// tests/vec_prsg.h for the kernel contract and tests/vec_prsg.c for the
// dispatch wrappers and scalar fallbacks.
//
// The kernels are written in inline assembly, so the compiler needs no
// intrinsics headers. On a 64-bit build a vector block is VEC_LANES(4) x
// 64-bit = 256 bits = two 128-bit NEON registers (q0 = {lane0,lane1},
// q1 = {lane2,lane3}), matching the two-xmm SSE2 layout.
//
// Forward xorshift:  x ^= x << 13;  x ^= x >> 7;  x ^= x << 17;
// The backward step inverts each stage in reverse order by repeated squaring:
// y = x ^ (x << s) is undone by x = y; x ^= x << s; x ^= x << 2s; x ^= x << 4s;
// ... doubling the shift until it exceeds the word width.
//
// Stores use STNP (store pair, non-temporal hint): the closest ARM analogue to
// the x86 non-temporal stores, avoiding cache allocation on write-heavy fills.
// The hint is advisory; actual eviction to DRAM between sweeps is still owned by
// flush_caches() / cache_flush(), not by the store type. Each kernel ends on a
// DSB ISH so no store is left buffered (the analogue of the x86 sfence).

#if defined(__aarch64__)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vec_prsg.h"

// One forward xorshift step (x ^= x<<13; x ^= x>>7; x ^= x<<17) on the two
// 64-bit lanes in state register s, using t as a scratch register.
#define NEON_STEP_FWD(s, t)                              \
        "shl   " t ".2d, " s ".2d, #13            \n\t"  \
        "eor   " s ".16b, " s ".16b, " t ".16b    \n\t"  \
        "ushr  " t ".2d, " s ".2d, #7             \n\t"  \
        "eor   " s ".16b, " s ".16b, " t ".16b    \n\t"  \
        "shl   " t ".2d, " s ".2d, #17            \n\t"  \
        "eor   " s ".16b, " s ".16b, " t ".16b    \n\t"

// One repeated-squaring inverse stage: undoes x ^= x <op> sh.
#define NEON_UNDO(s, t, op, sh)                          \
        op "  " t ".2d, " s ".2d, #" #sh "        \n\t"  \
        "eor   " s ".16b, " s ".16b, " t ".16b    \n\t"

// One backward xorshift step: inverts <<17, >>7, <<13 in that order.
#define NEON_STEP_BACK(s, t)         \
        NEON_UNDO(s, t, "shl",  17)  \
        NEON_UNDO(s, t, "shl",  34)  \
        NEON_UNDO(s, t, "ushr",  7)  \
        NEON_UNDO(s, t, "ushr", 14)  \
        NEON_UNDO(s, t, "ushr", 28)  \
        NEON_UNDO(s, t, "ushr", 56)  \
        NEON_UNDO(s, t, "shl",  13)  \
        NEON_UNDO(s, t, "shl",  26)  \
        NEON_UNDO(s, t, "shl",  52)

void vec_fill_neon(vec_state_t *st, testword_t *p, size_t nblocks, bool splat)
{
    size_t i = 0;

    __asm__ __volatile__ (
        "ldp   q0, q1, [%[st]]                    \n\t"
        "0:                                       \n\t"
        "stnp  q0, q1, [%[p]]                     \n\t"
        "cbnz  %w[splat], 1f                      \n\t"
        NEON_STEP_FWD("v0", "v4")
        NEON_STEP_FWD("v1", "v4")
        "1:                                       \n\t"
        "add   %[p], %[p], #32                    \n\t"
        "add   %[i], %[i], #1                     \n\t"
        "cmp   %[i], %[n]                         \n\t"
        "b.lo  0b                                 \n\t"
        "stp   q0, q1, [%[st]]                    \n\t"
        "dsb   ish                                \n\t"
        : [p] "+r" (p), [i] "+r" (i)
        : [st] "r" (st), [n] "r" (nblocks), [splat] "r" ((uint32_t)splat)
        : "v0", "v1", "v4", "memory", "cc"
    );
}

size_t vec_scan_fwd_neon(vec_state_t *st, testword_t *p, size_t nblocks, bool splat)
{
    size_t done = 0;

    __asm__ __volatile__ (
        "ldp   q0, q1, [%[st]]                    \n\t"
        "0:                                       \n\t"
        "ldp   q2, q3, [%[p]]                     \n\t"
        "cmeq  v2.2d, v2.2d, v0.2d                \n\t"    // all-ones per matching lane
        "cmeq  v3.2d, v3.2d, v1.2d                \n\t"
        "and   v2.16b, v2.16b, v3.16b             \n\t"
        "uminv b5, v2.16b                         \n\t"    // 0xFF iff every lane matched
        "umov  w8, v5.b[0]                        \n\t"
        "cmp   w8, #0xff                          \n\t"
        "b.ne  2f                                 \n\t"
        "mvn   v2.16b, v0.16b                     \n\t"    // complement = ~state
        "mvn   v3.16b, v1.16b                     \n\t"
        "stnp  q2, q3, [%[p]]                     \n\t"
        "cbnz  %w[splat], 1f                      \n\t"
        NEON_STEP_FWD("v0", "v4")
        NEON_STEP_FWD("v1", "v4")
        "1:                                       \n\t"
        "add   %[p], %[p], #32                    \n\t"
        "add   %[done], %[done], #1               \n\t"
        "cmp   %[done], %[n]                      \n\t"
        "b.lo  0b                                 \n\t"
        "2:                                       \n\t"
        "stp   q0, q1, [%[st]]                    \n\t"
        "dsb   ish                                \n\t"
        : [p] "+r" (p), [done] "+r" (done)
        : [st] "r" (st), [n] "r" (nblocks), [splat] "r" ((uint32_t)splat)
        : "v0", "v1", "v2", "v3", "v4", "v5", "x8", "memory", "cc"
    );
    return done;
}

size_t vec_scan_rev_neon(vec_state_t *st, testword_t *q, size_t nblocks, bool splat)
{
    size_t done = 0;

    __asm__ __volatile__ (
        "ldp   q0, q1, [%[st]]                    \n\t"
        "0:                                       \n\t"
        "cbnz  %w[splat], 1f                      \n\t"
        NEON_STEP_BACK("v0", "v4")
        NEON_STEP_BACK("v1", "v4")
        "1:                                       \n\t"
        "mvn   v2.16b, v0.16b                     \n\t"    // expected = ~state
        "mvn   v3.16b, v1.16b                     \n\t"
        "ldp   q6, q7, [%[q]]                     \n\t"
        "cmeq  v6.2d, v6.2d, v2.2d                \n\t"
        "cmeq  v7.2d, v7.2d, v3.2d                \n\t"
        "and   v6.16b, v6.16b, v7.16b             \n\t"
        "uminv b5, v6.16b                         \n\t"    // 0xFF iff every lane matched
        "umov  w8, v5.b[0]                        \n\t"
        "cmp   w8, #0xff                          \n\t"
        "b.ne  2f                                 \n\t"
        "stnp  q0, q1, [%[q]]                     \n\t"    // restore the original pattern
        "sub   %[q], %[q], #32                    \n\t"
        "add   %[done], %[done], #1               \n\t"
        "cmp   %[done], %[n]                      \n\t"
        "b.lo  0b                                 \n\t"
        "2:                                       \n\t"
        "stp   q0, q1, [%[st]]                    \n\t"
        "dsb   ish                                \n\t"
        : [q] "+r" (q), [done] "+r" (done)
        : [st] "r" (st), [n] "r" (nblocks), [splat] "r" ((uint32_t)splat)
        : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "x8", "memory", "cc"
    );
    return done;
}

#endif // __aarch64__
