// SPDX-License-Identifier: GPL-2.0
#ifndef MOV_INV_RNG_H
#define MOV_INV_RNG_H
/**
 * \file
 *
 * Provides the interface between the moving inversions RNG-sequence test
 * and its architecture-specific SIMD kernels.
 *
 *//*
 * Copyright (C) 2004-2026 Sam Demeulemeester.
 */

#include <stdbool.h>
#include <stddef.h>

#include "test.h"

/**
 * Number of test words processed as one vector block. This matches the AVX2
 * register width on 64-bit builds; the SSE2 and scalar kernels process the
 * same block layout in smaller steps, so each tier remains self-consistent.
 */
#define VEC_LANES   4

#define VEC_BYTES   (VEC_LANES * sizeof(testword_t))

/**
 * The per-lane xorshift states of the current vector block. Carried in
 * memory between kernel invocations, so no SIMD state is live across calls
 * to data_error(), do_tick(), or the thread barriers.
 */
typedef struct {
    testword_t lane[VEC_LANES];
} vec_state_t;

#if defined(__x86_64__)
/*
 * SIMD kernels (see tests/x86/mov_inv_rng_*.c). All kernels start from the
 * lane states in *st, leave the updated states back in *st, and end with an
 * sfence so no non-temporal write is left buffered.
 *
 * The fill kernels write nblocks blocks ascending from p, stepping the lane
 * states forwards after each block (unless splat).
 *
 * The scan kernels stop at the first mismatching block and return the number
 * of blocks completed (== nblocks if all matched), leaving the lane states
 * positioned at the failing block so the caller can report and fix it up:
 * - scan_fwd checks nblocks blocks ascending from p against the lane states,
 *   writing the complement and stepping forwards after each matching block.
 * - scan_rev checks nblocks blocks descending from q (which points to the
 *   highest block), stepping the lane states backwards before each block,
 *   comparing with the complement and restoring the original pattern.
 */
void   vec_fill_sse2(vec_state_t *st, testword_t *p, size_t nblocks, bool splat);
size_t vec_scan_fwd_sse2(vec_state_t *st, testword_t *p, size_t nblocks, bool splat);
size_t vec_scan_rev_sse2(vec_state_t *st, testword_t *q, size_t nblocks, bool splat);

void   vec_fill_avx2(vec_state_t *st, testword_t *p, size_t nblocks, bool splat);
size_t vec_scan_fwd_avx2(vec_state_t *st, testword_t *p, size_t nblocks, bool splat);
size_t vec_scan_rev_avx2(vec_state_t *st, testword_t *q, size_t nblocks, bool splat);
#endif

#endif // MOV_INV_RNG_H
