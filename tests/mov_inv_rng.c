// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2026 Sam Demeulemeester.
//
// Moving inversions test with a pseudo-random sequence.
//
// Memory is filled with a vector-wide pseudo-random sequence (each vector
// lane runs an independent xorshift stream), then checked and complemented
// walking up, then checked and restored walking down, in classic moving
// inversions order. Every fourth round broadcasts a single random value to
// all lanes, preserving the uniform-background fault model of the classic
// single-pattern moving inversions test.
//
// On x86_64 the fill and check loops use AVX2 (256-bit) or SSE2 (128-bit)
// kernels with non-temporal stores, selected at runtime from the SIMD tier
// detected by simd_init(). Non-temporal stores bypass the cache and avoid
// read-for-ownership traffic, roughly doubling the write-side stress on the
// memory bus. The kernels, their scalar fallbacks and the tier dispatch
// live in the vec_prsg module (tests/vec_prsg.c, tests/x86/vec_prsg_*.c).
//
// The check passes regenerate the sequence instead of storing it: xorshift
// is an invertible linear map, so the descending pass simply steps the lane
// states backwards.
//
// Note: chunks are aligned to the vector size, so up to (num_cpus * vector
// size - word size) bytes per segment are not covered by this test (the same
// trade-off as the block move test). Stray words at unaligned segment edges
// are covered with a scalar round constant.

#include <stdbool.h>
#include <stdint.h>

#include "cpuid.h"
#include "tsc.h"

#include "display.h"
#include "error.h"
#include "test.h"

#include "test_funcs.h"
#include "test_helper.h"

#include "vec_prsg.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int test_mov_inv_rng(int my_cpu, bool splat_round)
{
    int ticks = 0;

    testword_t seed;
    if (cpuid_info.flags.rdtsc) {
        seed = get_tsc();
    } else {
        seed = 1 + pass_num;
    }
    seed *= 0x12345678;

    vec_state_t st;
    seed_lanes(&st, seed, splat_round);

    // Round constant used for stray words at unaligned segment edges.
    testword_t epat = st.lane[0];

    if (my_cpu == master_cpu) {
        display_test_pattern_value(st.lane[0]);
    }

    // Initialize memory with the pseudo-random sequence.
    for (int j = 0; j < vm_map_size; j++) {
        testword_t *start, *end;
        calculate_chunk(&start, &end, my_cpu, j, VEC_BYTES);
        if (end < start) SKIP_RANGE(1)  // we need at least one word for this test

        // Split the chunk into an aligned vector body and stray edge words.
        testword_t *bstart = (testword_t *)round_up((uintptr_t)start, VEC_BYTES);
        size_t nwords = (bstart <= end) ? (size_t)(end - bstart) + 1 : 0;
        size_t nbody  = nwords - nwords % VEC_LANES;

        if (my_cpu >= 0) {
            for (testword_t *q = start; q < bstart && q <= end; q++) {
                write_word(q, epat);
            }
            for (testword_t *q = bstart + nbody; q <= end; q++) {
                write_word(q, epat);
            }
        }
        if (nbody == 0) SKIP_RANGE(1)

        testword_t *bend = bstart + (nbody - 1);

        testword_t *p  = bstart;
        testword_t *pe = bstart;

        bool at_end = false;
        do {
            // take care to avoid pointer overflow
            if ((bend - pe) >= SPIN_SIZE) {
                pe += SPIN_SIZE - 1;
            } else {
                at_end = true;
                pe = bend;
            }
            ticks++;
            if (my_cpu < 0) {
                continue;
            }
            test_addr[my_cpu] = (uintptr_t)p;
            vec_fill(&st, p, ((size_t)(pe - p) + 1) / VEC_LANES, splat_round);
            p = pe + 1;
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    // Check for the pattern and write the complement for each memory location,
    // walking up. The sequence is regenerated from the seed.
    flush_caches(my_cpu);

    seed_lanes(&st, seed, splat_round);

    for (int j = 0; j < vm_map_size; j++) {
        testword_t *start, *end;
        calculate_chunk(&start, &end, my_cpu, j, VEC_BYTES);
        if (end < start) SKIP_RANGE(1)  // we need at least one word for this test

        testword_t *bstart = (testword_t *)round_up((uintptr_t)start, VEC_BYTES);
        size_t nwords = (bstart <= end) ? (size_t)(end - bstart) + 1 : 0;
        size_t nbody  = nwords - nwords % VEC_LANES;

        if (my_cpu >= 0) {
            for (testword_t *q = start; q < bstart && q <= end; q++) {
                testword_t actual = read_word(q);
                if (unlikely(actual != epat)) {
                    data_error(q, epat, actual, true);
                }
                write_word(q, ~epat);
            }
            for (testword_t *q = bstart + nbody; q <= end; q++) {
                testword_t actual = read_word(q);
                if (unlikely(actual != epat)) {
                    data_error(q, epat, actual, true);
                }
                write_word(q, ~epat);
            }
        }
        if (nbody == 0) SKIP_RANGE(1)

        testword_t *bend = bstart + (nbody - 1);

        testword_t *p  = bstart;
        testword_t *pe = bstart;

        bool at_end = false;
        do {
            // take care to avoid pointer overflow
            if ((bend - pe) >= SPIN_SIZE) {
                pe += SPIN_SIZE - 1;
            } else {
                at_end = true;
                pe = bend;
            }
            ticks++;
            if (my_cpu < 0) {
                continue;
            }
            test_addr[my_cpu] = (uintptr_t)p;
            vec_check_fwd(&st, p, ((size_t)(pe - p) + 1) / VEC_LANES, splat_round, true);
            p = pe + 1;
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_end && ++pe); // advance pe to next start point
    }

    // Check for the complement and restore the pattern for each memory
    // location, walking down. The lane states are stepped backwards, so no
    // stored sequence is needed.
    flush_caches(my_cpu);

    for (int j = vm_map_size - 1; j >= 0; j--) {
        testword_t *start, *end;
        calculate_chunk(&start, &end, my_cpu, j, VEC_BYTES);
        if (end < start) SKIP_RANGE(1)  // we need at least one word for this test

        testword_t *bstart = (testword_t *)round_up((uintptr_t)start, VEC_BYTES);
        size_t nwords = (bstart <= end) ? (size_t)(end - bstart) + 1 : 0;
        size_t nbody  = nwords - nwords % VEC_LANES;

        if (my_cpu >= 0) {
            for (testword_t *q = bstart + nbody; q <= end; q++) {
                testword_t actual = read_word(q);
                if (unlikely(actual != (testword_t)~epat)) {
                    data_error(q, ~epat, actual, true);
                }
                write_word(q, epat);
            }
        }
        if (nbody == 0) {
            if (my_cpu >= 0) {
                for (testword_t *q = start; q < bstart && q <= end; q++) {
                    testword_t actual = read_word(q);
                    if (unlikely(actual != (testword_t)~epat)) {
                        data_error(q, ~epat, actual, true);
                    }
                    write_word(q, epat);
                }
            }
            SKIP_RANGE(1)
        }

        testword_t *bend = bstart + (nbody - 1);

        testword_t *p  = bend;
        testword_t *ps = bend;

        bool at_start = false;
        do {
            // take care to avoid pointer underflow
            if ((ps - bstart) >= SPIN_SIZE) {
                ps -= SPIN_SIZE - 1;
            } else {
                at_start = true;
                ps = bstart;
            }
            ticks++;
            if (my_cpu < 0) {
                continue;
            }
            test_addr[my_cpu] = (uintptr_t)ps;
            vec_check_rev(&st, ps, ((size_t)(p - ps) + 1) / VEC_LANES, splat_round, true);
            p = ps - 1;
            do_tick(my_cpu);
            BAILOUT;
        } while (!at_start && --ps); // advance ps to next start point

        if (my_cpu >= 0) {
            for (testword_t *q = start; q < bstart && q <= end; q++) {
                testword_t actual = read_word(q);
                if (unlikely(actual != (testword_t)~epat)) {
                    data_error(q, ~epat, actual, true);
                }
                write_word(q, epat);
            }
        }
    }

    return ticks;
}
