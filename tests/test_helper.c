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
#include "vmem.h"

#include "barrier.h"

#include "config.h"
#include "display.h"

#include "test_helper.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void calculate_chunk(testword_t **start, testword_t **end, int my_cpu, int segment, size_t chunk_align)
{
    if (my_cpu < 0) {
        my_cpu = 0;
    }

    // NUMA_PAR: the context's map only holds its own memory. Split each segment evenly over the
    // team, the last chunk taking the remainder.
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
        // NUMA_ON only: an inactive NUMA_PAR uses the plain path below.
        if (VMEM_MAX_CONTEXTS > 1 && numa_mode == NUMA_ON) {
            uint32_t proximity_domain_idx = smp_get_proximity_domain_idx(my_cpu);

            // Is this CPU in the same proximity domain as the current segment ?
            if (proximity_domain_idx == vm_map[context][segment].proximity_domain_idx) {
                uintptr_t segment_size = (vm_map[context][segment].end - vm_map[context][segment].start + 1) * sizeof(testword_t);
                uintptr_t chunk_size   = round_down(segment_size / used_cpus_in_proximity_domain[proximity_domain_idx], chunk_align);

                // Calculate chunk boundaries.
                *start = (testword_t *)((uintptr_t)vm_map[context][segment].start + chunk_size * chunk_index[my_cpu]);
                *end   = (testword_t *)((uintptr_t)(*start) + chunk_size) - 1;

                // The last CPU also takes the rounding remainder.
                if (*end > vm_map[context][segment].end
                 || chunk_index[my_cpu] == used_cpus_in_proximity_domain[proximity_domain_idx] - 1) {
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

            // Calculate chunk boundaries.
            *start = (testword_t *)((uintptr_t)vm_map[context][segment].start + chunk_size * chunk_index[my_cpu]);
            *end   = (testword_t *)((uintptr_t)(*start) + chunk_size) - 1;

            // The last CPU also takes the rounding remainder.
            if (*end > vm_map[context][segment].end || chunk_index[my_cpu] == num_active_cpus - 1) {
                *end = vm_map[context][segment].end;
            }
        }
    }
}

testword_t phys_addr_offset(void)
{
    int context = test_context_index();
#if (ARCH_BITS == 64)
    // All segments of the current window share one virtual-to-physical offset.
    return ((testword_t)vm_map[context][0].pm_base_addr << PAGE_SHIFT) - (uintptr_t)vm_map[context][0].start;
#else
    testword_t offset = (vm_map[context][0].pm_base_addr / VM_WINDOW_SIZE) * VM_WINDOW_SIZE;
    offset = (offset >= VM_PINNED_SIZE) ? offset - VM_PINNED_SIZE : 0;
    return offset / VM_WINDOW_SIZE;
#endif
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
        // In NUMA_PAR every team flushes its own socket's caches.
        int flush_cpu = numa_run_active ? test_context()->master_cpu_num : master_cpu;
        if (my_cpu == flush_cpu) {
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
