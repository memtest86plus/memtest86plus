// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester.
//
// Bus/interface stress test with fine-grained write/read turnarounds.
//
// Each CPU splits its chunk of memory into two halves and alternates, in
// short bursts, between filling one half with a fresh pseudo-random sequence
// (non-temporal stores) and read-verifying the other half against the
// sequence written on the previous round. The two streams stay about half a
// chunk apart, so every burst switch forces a write-to-read turnaround and a
// row/bank conflict at the memory controller - the tightest timing corners
// of the memory interface. The burst length changes every round, and every
// other round inserts short idle gaps to provoke load steps in the module
// power delivery. This targets DDR5-era interface faults (link signal
// integrity, marginal XMP/EXPO training, PMIC voltage droop) that on-die
// ECC cannot correct; see doc/DDR5_STRESS_PROPOSAL.md.
//
// Coverage notes: errors are typically transient, so they are not used for
// BadRAM accumulation; the complements written behind the verifier and the
// final round's second-half fill are extra bus traffic that is never
// verified; without SIMD support (i586, LoongArch) the fills use cached
// scalar stores flushed between sweeps, which stress the interface less.

#include <stdbool.h>
#include <stdint.h>

#include "unistd.h"

#include "display.h"
#include "error.h"
#include "test.h"

#include "test_funcs.h"
#include "test_helper.h"

#include "vec_prsg.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// On duty-cycled rounds, insert an idle gap of BUS_OFF_US after roughly every
// BUS_ON_BYTES of per-CPU traffic, provoking load steps in the power delivery.
#define BUS_ON_BYTES    (4 << 20)
#define BUS_OFF_US      1500

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

// Non-linear mixer (SplitMix64 / fmix32), same rationale as fade_mix() in
// bit_fade.c: reproducible seeds with no TSC, identical on every CPU.
static inline testword_t bus_mix(testword_t x)
{
#if TESTWORD_WIDTH > 32
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
#else
    x ^= x >> 16;
    x *= 0x85ebca6b;
    x ^= x >> 13;
    x *= 0xc2b2ae35;
    x ^= x >> 16;
#endif
    return x;
}

// Returns the seed for one (iteration, segment, half) stream: a pure function
// of its inputs, so any CPU can regenerate any stream at any time.
static testword_t bus_seed(int iteration, int segment, int half)
{
#if TESTWORD_WIDTH > 32
    testword_t x = (1 + (testword_t)pass_num)  * UINT64_C(0x9e3779b97f4a7c15)
                 + (1 + (testword_t)iteration) * UINT64_C(0xbf58476d1ce4e5b9)
                 + (testword_t)(3 * segment + half + 1) * UINT64_C(0x94d049bb133111eb);
#else
    testword_t x = (1 + (testword_t)pass_num)  * 0x9e3779b9
                 + (1 + (testword_t)iteration) * 0xbf58476d
                 + (testword_t)(3 * segment + half + 1) * 0x94d049bb;
#endif
    return bus_mix(x);
}

// Alternates an NT-fill stream and a verify stream every burst_blocks vector
// blocks. str == NULL disables the verifier; sleep_period == 0 runs flat out.
static void burst_interleave(testword_t *pw, size_t nw, vec_state_t *stw,
                             testword_t *pr, size_t nr, vec_state_t *str,
                             size_t burst_blocks, size_t sleep_period)
{
    if (str == NULL) {
        nr = 0;
    }

    size_t trips = 0;
    while (nw > 0 || nr > 0) {
        // The kernels process at least one block, so never call them with 0.
        size_t kw = (nw < burst_blocks) ? nw : burst_blocks;
        if (kw > 0) {
            // The sfence ending each fill drains the write-combining buffers,
            // forcing the write-to-read turnaround at every burst switch.
            vec_fill(stw, pw, kw, false);
            pw += kw * VEC_LANES;
            nw -= kw;
        }
        size_t kr = (nr < burst_blocks) ? nr : burst_blocks;
        if (kr > 0) {
            vec_check_fwd(str, pr, kr, false, false);
            pr += kr * VEC_LANES;
            nr -= kr;
        }
        trips++;
        if (sleep_period > 0 && trips % sleep_period == 0) {
            usleep(BUS_OFF_US);
        }
    }
}

// One sweep over all segments. Sweep 1 fills H1 and verifies H2 against the
// previous iteration; sweep 2 fills H2 and verifies the H1 data just written.
static int do_sweep(int my_cpu, int iteration, bool second, size_t burst_blocks, size_t sleep_period)
{
    int ticks = 0;

    for (int j = 0; j < vm_map_size; j++) {
        testword_t *start, *end;
        calculate_chunk(&start, &end, my_cpu, j, VEC_BYTES);
        if (end < start) SKIP_RANGE(1)  // we need at least one word for this test

        // Split the chunk into an aligned vector body and stray edge words.
        testword_t *bstart = (testword_t *)round_up((uintptr_t)start, VEC_BYTES);
        size_t nwords = (bstart <= end) ? (size_t)(end - bstart) + 1 : 0;
        size_t nbody  = nwords - nwords % VEC_LANES;

        if (!second && my_cpu >= 0) {
            // Stray edge words: verify the previous round's constant, then
            // write this round's.
            testword_t epat_cur  = bus_seed(iteration, j, 2);
            testword_t epat_prev = bus_seed(iteration - 1, j, 2);
            for (testword_t *q = start; q < bstart && q <= end; q++) {
                testword_t actual = read_word(q);
                if (iteration > 0 && unlikely(actual != epat_prev)) {
                    data_error(q, epat_prev, actual, false);
                }
                write_word(q, epat_cur);
            }
            for (testword_t *q = bstart + nbody; q <= end; q++) {
                testword_t actual = read_word(q);
                if (iteration > 0 && unlikely(actual != epat_prev)) {
                    data_error(q, epat_prev, actual, false);
                }
                write_word(q, epat_cur);
            }
        }
        if (nbody == 0) SKIP_RANGE(1)

        size_t nblocks = nbody / VEC_LANES;
        size_t nb1 = nblocks / 2;
        size_t nb2 = nblocks - nb1;

        testword_t *h1 = bstart;
        testword_t *h2 = bstart + nb1 * VEC_LANES;

        // A fixed tick per segment per sweep keeps the dummy-run estimate
        // exact and matches SKIP_RANGE(1) on CPUs outside the NUMA domain.
        ticks++;
        if (my_cpu < 0) {
            continue;
        }
        test_addr[my_cpu] = (uintptr_t)bstart;

        vec_state_t st_w, st_r;
        if (!second) {
            seed_lanes(&st_w, bus_seed(iteration, j, 0), false);
            if (iteration > 0) {
                seed_lanes(&st_r, bus_seed(iteration - 1, j, 1), false);
                burst_interleave(h1, nb1, &st_w, h2, nb2, &st_r, burst_blocks, sleep_period);
            } else {
                // First round: nothing to verify yet, fill only.
                burst_interleave(h1, nb1, &st_w, NULL, 0, NULL, burst_blocks, sleep_period);
            }
        } else {
            seed_lanes(&st_w, bus_seed(iteration, j, 1), false);
            seed_lanes(&st_r, bus_seed(iteration, j, 0), false);
            burst_interleave(h2, nb2, &st_w, h1, nb1, &st_r, burst_blocks, sleep_period);
        }
        do_tick(my_cpu);
        BAILOUT;
    }

    return ticks;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int test_bus_stress(int my_cpu, int iteration)
{
    int ticks = 0;

    size_t burst_blocks = (size_t)8 << (iteration % 3);
    size_t sleep_period = (iteration & 1) ? BUS_ON_BYTES / (2 * burst_blocks * VEC_BYTES) : 0;

    if (my_cpu == master_cpu) {
        display_test_pattern_value(bus_seed(iteration, 0, 0));
    }

    ticks += do_sweep(my_cpu, iteration, false, burst_blocks, sleep_period);
    BAILOUT;
    // Every CPU flushes its own caches so scalar-tier fills reach DRAM before
    // the verify. Must stay between whole sweeps: it waits on run_barrier.
    flush_caches_all(my_cpu);

    ticks += do_sweep(my_cpu, iteration, true, burst_blocks, sleep_period);
    BAILOUT;
    flush_caches_all(my_cpu);

    return ticks;
}
