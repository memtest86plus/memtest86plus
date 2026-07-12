// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2026 Sam Demeulemeester
//
// LASX kernels for the PRSG vector fill/check primitives. See
// tests/vec_prsg.h for the kernel contract and tests/vec_prsg.c for the
// dispatch wrappers and scalar fallbacks.
//
// The kernels are written in inline assembly, so the compiler needs no
// LASX support and no intrinsics headers are required (the assembler
// accepts the xv* mnemonics unconditionally since binutils 2.41). On a
// 64-bit build a vector block is VEC_LANES(4) x 64-bit = 256 bits = one
// LASX register, matching the one-ymm AVX2 layout.
//
// Forward xorshift:  x ^= x << 13;  x ^= x >> 7;  x ^= x << 17;
// The backward step inverts each stage in reverse order by repeated squaring:
// y = x ^ (x << s) is undone by x = y; x ^= x << s; x ^= x << 2s; x ^= x << 4s;
// ... doubling the shift until it exceeds the word width.
//
// LoongArch has no non-temporal store, so plain xvst is used; actual
// eviction to DRAM between sweeps is still owned by flush_caches() /
// cache_flush(), not by the store type. Each kernel ends on a dbar 0 so
// no store is left buffered (the analogue of the x86 sfence).
//
// The lane states are accessed with xvld/xvst from an 8-byte-aligned
// struct: unaligned vector access (CPUCFG1.UAL) is required and verified
// by simd_init() before this tier is selected.
//
// The exception wrapper spills the FP registers with fst.d, which leaves
// the upper bits of the aliased $xr registers undefined. This is safe only
// because the sole runtime interrupt is the barrier wakeup IPI, delivered
// while a CPU is parked in the halt idle loop where no vector state is
// live: the lane states are carried in memory across all calls into C.

#if defined(__loongarch_lp64)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vec_prsg.h"

// The kernels assume the compiler holds no values in the vector registers,
// so the clobber lists only need to name the aliased $f registers.
#if defined(__loongarch_sx) || defined(__loongarch_asx)
#error "vec_prsg_lasx.c must be compiled without -mlsx/-mlasx"
#endif

// One forward xorshift step (x ^= x<<13; x ^= x>>7; x ^= x<<17) on the four
// 64-bit lanes in state register s, using t as a scratch register.
#define LASX_STEP_FWD(s, t)                             \
        "xvslli.d " t ", " s ", 13             \n\t"    \
        "xvxor.v  " s ", " s ", " t "          \n\t"    \
        "xvsrli.d " t ", " s ", 7              \n\t"    \
        "xvxor.v  " s ", " s ", " t "          \n\t"    \
        "xvslli.d " t ", " s ", 17             \n\t"    \
        "xvxor.v  " s ", " s ", " t "          \n\t"

// One repeated-squaring inverse stage: undoes x ^= x <op> shift.
#define LASX_UNDO_STEP(s, t, op, shift)                 \
        op "  " t ", " s ", " #shift "         \n\t"    \
        "xvxor.v  " s ", " s ", " t "          \n\t"

// One backward xorshift step: inverts <<17, >>7, <<13 in that order.
#define LASX_STEP_BACK(s, t)                        \
        LASX_UNDO_STEP(s, t, "xvslli.d", 17)        \
        LASX_UNDO_STEP(s, t, "xvslli.d", 34)        \
        LASX_UNDO_STEP(s, t, "xvsrli.d", 7)         \
        LASX_UNDO_STEP(s, t, "xvsrli.d", 14)        \
        LASX_UNDO_STEP(s, t, "xvsrli.d", 28)        \
        LASX_UNDO_STEP(s, t, "xvsrli.d", 56)        \
        LASX_UNDO_STEP(s, t, "xvslli.d", 13)        \
        LASX_UNDO_STEP(s, t, "xvslli.d", 26)        \
        LASX_UNDO_STEP(s, t, "xvslli.d", 52)

void vec_fill_lasx(vec_state_t *st, testword_t *p, size_t nblocks, bool splat)
{
    size_t i = 0;

    __asm__ __volatile__ (
        "xvld     $xr0, %[st], 0       \n\t"
        "0:                            \n\t"
        "xvst     $xr0, %[p], 0        \n\t"
        "bnez     %[splat], 1f         \n\t"
        LASX_STEP_FWD("$xr0", "$xr1")
        "1:                            \n\t"
        "addi.d   %[p], %[p], 32       \n\t"
        "addi.d   %[i], %[i], 1        \n\t"
        "bltu     %[i], %[n], 0b       \n\t"
        "xvst     $xr0, %[st], 0       \n\t"
        "dbar     0                    \n\t"
        : [p] "+r" (p), [i] "+r" (i)
        : [st] "r" (st), [n] "r" (nblocks), [splat] "r" ((uint32_t)splat)
        : "$f0", "$f1", "memory"
    );
}

size_t vec_scan_fwd_lasx(vec_state_t *st, testword_t *p, size_t nblocks, bool splat)
{
    size_t done = 0;

    __asm__ __volatile__ (
        "xvld     $xr0, %[st], 0           \n\t"
        "0:                                \n\t"
        "xvld     $xr1, %[p], 0            \n\t"
        "xvseq.d  $xr2, $xr1, $xr0         \n\t"    // all-ones per matching lane
        "xvsetanyeqz.d $fcc0, $xr2         \n\t"    // set iff any lane mismatched
        "bcnez    $fcc0, 2f                \n\t"
        "xvnor.v  $xr1, $xr0, $xr0         \n\t"    // complement
        "xvst     $xr1, %[p], 0            \n\t"
        "bnez     %[splat], 1f             \n\t"
        LASX_STEP_FWD("$xr0", "$xr1")
        "1:                                \n\t"
        "addi.d   %[p], %[p], 32           \n\t"
        "addi.d   %[done], %[done], 1      \n\t"
        "bltu     %[done], %[n], 0b        \n\t"
        "2:                                \n\t"
        "xvst     $xr0, %[st], 0           \n\t"
        "dbar     0                        \n\t"
        : [p] "+r" (p), [done] "+r" (done)
        : [st] "r" (st), [n] "r" (nblocks), [splat] "r" ((uint32_t)splat)
        : "$f0", "$f1", "$f2", "$fcc0", "memory"
    );
    return done;
}

size_t vec_scan_rev_lasx(vec_state_t *st, testword_t *q, size_t nblocks, bool splat)
{
    size_t done = 0;

    __asm__ __volatile__ (
        "xvld     $xr0, %[st], 0           \n\t"
        "0:                                \n\t"
        "bnez     %[splat], 1f             \n\t"
        LASX_STEP_BACK("$xr0", "$xr1")
        "1:                                \n\t"
        "xvld     $xr1, %[q], 0            \n\t"
        "xvnor.v  $xr2, $xr0, $xr0         \n\t"    // expected = complement
        "xvseq.d  $xr2, $xr1, $xr2         \n\t"
        "xvsetanyeqz.d $fcc0, $xr2         \n\t"    // set iff any lane mismatched
        "bcnez    $fcc0, 2f                \n\t"
        "xvst     $xr0, %[q], 0            \n\t"    // restore the pattern
        "addi.d   %[q], %[q], -32          \n\t"
        "addi.d   %[done], %[done], 1      \n\t"
        "bltu     %[done], %[n], 0b        \n\t"
        "2:                                \n\t"
        "xvst     $xr0, %[st], 0           \n\t"
        "dbar     0                        \n\t"
        : [q] "+r" (q), [done] "+r" (done)
        : [st] "r" (st), [n] "r" (nblocks), [splat] "r" ((uint32_t)splat)
        : "$f0", "$f1", "$f2", "$fcc0", "memory"
    );
    return done;
}

#endif // __loongarch_lp64
