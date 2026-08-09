// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Partly derived from an extract of memtest86+ test.c:
//
// MemTest86+ V5 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.canardpc.com - http://www.memtest.org
// Thanks to Passmark for calculate_chunk() and various comments !
// ----------------------------------------------------
// test.c - MemTest-86  Version 3.4
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdint.h>

#include "cache.h"
#include "smp.h"

#include "barrier.h"

#include "config.h"
#include "display.h"

#include "test_helper.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

// The tests call do_tick() once per SPIN_SIZE block, so every CPU must
// cover the same number of blocks: a last chunk pushed across a block
// boundary would execute one extra do_tick()/barrier generation and hang
// the team at the phase boundary. Rebalance the chunk size so that the
// chunk and the last chunk stay in the same block bucket ((K-1)·B, K·B],
// with K = ceil(segment_size / (n·B)).
static uintptr_t equalize_block_counts(uintptr_t segment_size, uintptr_t chunk_size, int num_chunks, size_t chunk_align)
{
    uint64_t n = (uint64_t)num_chunks;
    uint64_t B = (uint64_t)SPIN_SIZE * sizeof(testword_t);
    uint64_t seg = (uint64_t)segment_size;
    uint64_t c = (uint64_t)chunk_size;
    uint64_t last = seg - (n - 1) * c;

    // The tests tick once per block, i.e. ceil(size / B) times; a last chunk
    // pushed across a block boundary would execute one extra barrier
    // generation. Compare the ceil buckets, not the floor ones: a chunk of
    // exactly k·B bytes with a positive remainder is the case to fix.
    if ((last + B - 1) / B == (c + B - 1) / B) {
        return chunk_size;
    }

    // The chunk and the last chunk fall into different block buckets; find
    // a chunk size in the shared bucket, closest to the balanced seg/n.
    uint64_t K = (seg + n * B - 1) / (n * B);
    uint64_t lo = (K - 1) * B + 1;
    uint64_t hi = K * B;
    if (seg >= K * B) {
        uint64_t r_lo = (seg - K * B + n - 2) / (n - 1);
        if (r_lo > lo) lo = r_lo;
    }
    uint64_t r_hi = (seg - (K - 1) * B - 1) / (n - 1);
    if (r_hi < hi) hi = r_hi;
    lo = round_up((uintptr_t)lo, chunk_align);
    hi = round_down((uintptr_t)hi, chunk_align);

    if (lo <= hi) {
        uint64_t target = (uint64_t)round_down((uintptr_t)(seg / n), chunk_align);
        if (target < lo) target = lo;
        if (target > hi) target = hi;
        return (uintptr_t)target;
    }

    // The segment leaves a sub-alignment remainder that cannot be
    // equalized; keep the plain split. The residual skew is one extra tick
    // and requires the chunk to be an exact multiple of a 1 GiB block,
    // i.e. it is astronomically rare (and pre-existing in legacy mode).
    return chunk_size;
}

void calculate_chunk(testword_t **start, testword_t **end, int my_cpu, int segment, size_t chunk_align)
{
    if (my_cpu < 0) {
        my_cpu = 0;
    }

    // NUMA_PAR: the per-context map already contains only memory owned by
    // this team, so there is no per-segment proximity-domain rejection; the
    // dense per-context chunk index splits each segment evenly. The chunks
    // are contiguous, complete and non-overlapping; the last chunk absorbs
    // the remainder and is never empty, so every mapped block is tested
    // exactly once.
    if (numa_run_active) {
        int context = test_context_index();
        test_context_t *ctx = &test_contexts[context];
        testword_t *seg_start = vm_map[context][segment].start;
        testword_t *seg_end   = vm_map[context][segment].end;

        if (ctx->active_cpu_count == 1) {
            *start = seg_start;
            *end   = seg_end;
        } else {
            uintptr_t segment_size = (seg_end - seg_start + 1) * sizeof(testword_t);
            uintptr_t chunk_size = round_down(segment_size / ctx->active_cpu_count, chunk_align);
            chunk_size = equalize_block_counts(segment_size, chunk_size, ctx->active_cpu_count, chunk_align);
            unsigned int chunk = test_team_chunk_index[my_cpu];
            *start = (testword_t *)((uintptr_t)seg_start + chunk_size * chunk);
            *end   = (chunk == ctx->active_cpu_count - 1) ? seg_end : (testword_t *)((uintptr_t)*start + chunk_size) - 1;
        }
        return;
    }

    int context = test_context_index();

    // If we are only running 1 CPU then test the whole segment.
    if (num_active_cpus == 1) {
        *start = vm_map[context][segment].start;
        *end   = vm_map[context][segment].end;
    } else {
        // Legacy NUMA_ON chunking only; a requested-but-inactive NUMA_PAR
        // uses the topology-agnostic path below.
        if (VMEM_MAX_CONTEXTS > 1 && numa_mode == NUMA_ON) {
            uint32_t proximity_domain_idx = smp_get_proximity_domain_idx(my_cpu);

            // Is this CPU in the same proximity domain as the current segment ?
            if (proximity_domain_idx == vm_map[context][segment].proximity_domain_idx) {
                uintptr_t segment_size = (vm_map[context][segment].end - vm_map[context][segment].start + 1) * sizeof(testword_t);
                uintptr_t chunk_size   = round_down(segment_size / used_cpus_in_proximity_domain[proximity_domain_idx], chunk_align);
                chunk_size = equalize_block_counts(segment_size, chunk_size, used_cpus_in_proximity_domain[proximity_domain_idx], chunk_align);

                // Calculate chunk boundaries.
                *start = (testword_t *)((uintptr_t)vm_map[context][segment].start + chunk_size * chunk_index[my_cpu]);
                *end   = (testword_t *)((uintptr_t)(*start) + chunk_size) - 1;

                if (*end > vm_map[context][segment].end) {
                    *end = vm_map[context][segment].end;
                }
            } else {
                // Nope.
                *start = (testword_t *)1;
                *end = (testword_t *)0;
            }
        } else {
            uintptr_t segment_size = (vm_map[context][segment].end - vm_map[context][segment].start + 1) * sizeof(testword_t);
            uintptr_t chunk_size   = round_down(segment_size / num_active_cpus, chunk_align);
            chunk_size = equalize_block_counts(segment_size, chunk_size, num_active_cpus, chunk_align);

            // Calculate chunk boundaries.
            *start = (testword_t *)((uintptr_t)vm_map[context][segment].start + chunk_size * chunk_index[my_cpu]);
            *end   = (testword_t *)((uintptr_t)(*start) + chunk_size) - 1;

            if (*end > vm_map[context][segment].end) {
                *end = vm_map[context][segment].end;
            }
        }
    }
}

void flush_caches(int my_cpu)
{
    if (my_cpu >= 0) {
        bool use_spin_wait = (power_save < POWER_SAVE_HIGH);
        if (use_spin_wait) {
            barrier_spin_wait(test_run_barrier());
        } else {
            barrier_halt_wait(test_run_barrier());
        }
        if (my_cpu == master_cpu) {
            cache_flush();
        }
        if (use_spin_wait) {
            barrier_spin_wait(test_run_barrier());
        } else {
            barrier_halt_wait(test_run_barrier());
        }
    }
}

void flush_caches_all(int my_cpu)
{
    if (my_cpu >= 0) {
        bool use_spin_wait = (power_save < POWER_SAVE_HIGH);
        if (use_spin_wait) {
            barrier_spin_wait(test_run_barrier());
        } else {
            barrier_halt_wait(test_run_barrier());
        }
        cache_flush();
        if (use_spin_wait) {
            barrier_spin_wait(test_run_barrier());
        } else {
            barrier_halt_wait(test_run_barrier());
        }
    }
}
