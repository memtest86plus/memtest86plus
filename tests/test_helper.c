// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2021 Martin Whitaker.
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

#include "barrier.h"

#include "config.h"
#include "display.h"

#include "test_helper.h"

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

// We keep a separate LFSR for each CPU. Space them out by at least a cache line,
// otherwise performance suffers.

typedef struct {
    uint64_t    lfsr;
    uint64_t    pad[7];
} prsg_state_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static prsg_state_t prsg_state[MAX_VCPUS];

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static inline uint32_t prsg(int my_vcpu)
{
    // This implements a 64 bit linear feedback shift register with XNOR
    // feedback from taps 64, 63, 61, 60. It generates 32 new bits each
    // time the function is called. Because the feedback taps are all in
    // the upper 32 bits, we can generate the new bits in parallel.

    uint64_t lfsr = prsg_state[my_vcpu].lfsr;
    uint32_t feedback = ~((lfsr >> 32) ^ (lfsr >> 31) ^ (lfsr >> 29) ^ (lfsr >> 28));
    prsg_state[my_vcpu].lfsr = (lfsr << 32) | feedback;
    return feedback;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void random_seed(int my_vcpu, uint64_t seed)
{
    if (my_vcpu < 0) {
        return;
    }

    // Avoid the PRSG illegal state.
    if (~seed == 0) {
        seed = 0;
    }
    prsg_state[my_vcpu].lfsr = seed;
}

testword_t random(int my_vcpu)
{
    if (my_vcpu < 0) {
        return 0;
    }

    testword_t value = prsg(my_vcpu);
#if TESTWORD_WIDTH > 32
    value = value << 32 | prsg(my_vcpu);
#endif
    return value;
}

void calculate_chunk(testword_t **start, testword_t **end, int my_vcpu, int segment, size_t chunk_align)
{
    if (my_vcpu < 0) {
        my_vcpu = 0;
    }

    // If we are only running 1 CPU then test the whole segment.
    if (num_vcpus == 1) {
        *start = vm_map[segment].start;
        *end   = vm_map[segment].end;
    } else {
        uintptr_t segment_size = (vm_map[segment].end - vm_map[segment].start + 1) * sizeof(testword_t);
        uintptr_t chunk_size   = round_down(segment_size / num_vcpus, chunk_align);

        // Calculate chunk boundaries.
        *start = (testword_t *)((uintptr_t)vm_map[segment].start + chunk_size * my_vcpu);
        *end   = (testword_t *)((uintptr_t)(*start) + chunk_size) - 1;

        if (*end > vm_map[segment].end) {
            *end = vm_map[segment].end;
        }
    }
}

void flush_caches(int my_vcpu)
{
    if (my_vcpu >= 0) {
        barrier_wait(run_barrier);
        if (my_vcpu == master_vcpu) {
            cache_flush();
        }
        barrier_wait(run_barrier);
    }
}
