// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2026 Sam Demeulemeester
//
// Released under version 2 of the Gnu Public License.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "cpuinfo.h"
#include "hwquirks.h"
#include "pmem.h"
#include "tsc.h"
#include "vmem.h"

#include "display.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define BENCH_MIN_START_ADR     0x1000000               // 16MB
#define BENCH_ALIGN             64                      // cache-line alignment for src & dst
#define BENCH_DST_SKEW          0                       // break src & dst set-aliasing (for testing only)
#define MIN_RAM_WORKSET_BYTES   (8U * 1024U * 1024U)    // minimum total footprint for DRAM test (src+dst)

#ifdef __x86_64__
#define COPY_BLOCK_BYTES        128                     // SSE2 test works in 128B blocks
#else
#define COPY_BLOCK_BYTES        4                       // rep movsl works in dwords
#endif

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef enum {
    MEMBENCH_UNCACHED = 0,
    MEMBENCH_CACHED   = 1,
    MEMBENCH_NT_STORE = 2,
    MEMBENCH_REPMOV   = 3,
} membench_mode_t;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static inline uintptr_t align_up_ptr(uintptr_t v, uintptr_t a)
{
    return (v + (a - 1)) & ~(a - 1);
}

static inline size_t align_down_size(size_t v, size_t a)
{
    return v & ~(a - 1);
}

#ifdef __x86_64__

__attribute__((always_inline))
static inline void bench_copy_sse2_cached(uint8_t *dst, const uint8_t *src, size_t n)
{
    if (n == 0) return;

    const uint8_t *s = src;
    uint8_t       *d = dst;

    __asm__ __volatile__(
        "1:\n\t"
        "movdqa   0(%[s]), %%xmm0\n\t"
        "movdqa  16(%[s]), %%xmm1\n\t"
        "movdqa  32(%[s]), %%xmm2\n\t"
        "movdqa  48(%[s]), %%xmm3\n\t"
        "movdqa  64(%[s]), %%xmm4\n\t"
        "movdqa  80(%[s]), %%xmm5\n\t"
        "movdqa  96(%[s]), %%xmm6\n\t"
        "movdqa 112(%[s]), %%xmm7\n\t"

        "movdqa %%xmm0,   0(%[d])\n\t"
        "movdqa %%xmm1,  16(%[d])\n\t"
        "movdqa %%xmm2,  32(%[d])\n\t"
        "movdqa %%xmm3,  48(%[d])\n\t"
        "movdqa %%xmm4,  64(%[d])\n\t"
        "movdqa %%xmm5,  80(%[d])\n\t"
        "movdqa %%xmm6,  96(%[d])\n\t"
        "movdqa %%xmm7, 112(%[d])\n\t"

        "add $128, %[s]\n\t"
        "add $128, %[d]\n\t"
        "dec %[n]\n\t"
        "jnz 1b\n\t"
        : [s] "+r"(s), [d] "+r"(d), [n] "+r"(n)
        :
        : "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory"
    );

}

__attribute__((always_inline))
static inline void bench_copy_sse2_nt_store(uint8_t *dst, const uint8_t *src, size_t n)
{
    if (n == 0) return;
    const uint8_t *s = src;
    uint8_t       *d = dst;

    __asm__ __volatile__(
        "1:\n\t"
        "movdqa   0(%[s]), %%xmm0\n\t"
        "movdqa  16(%[s]), %%xmm1\n\t"
        "movdqa  32(%[s]), %%xmm2\n\t"
        "movdqa  48(%[s]), %%xmm3\n\t"
        "movdqa  64(%[s]), %%xmm4\n\t"
        "movdqa  80(%[s]), %%xmm5\n\t"
        "movdqa  96(%[s]), %%xmm6\n\t"
        "movdqa 112(%[s]), %%xmm7\n\t"

        "movntdq %%xmm0,   0(%[d])\n\t"
        "movntdq %%xmm1,  16(%[d])\n\t"
        "movntdq %%xmm2,  32(%[d])\n\t"
        "movntdq %%xmm3,  48(%[d])\n\t"
        "movntdq %%xmm4,  64(%[d])\n\t"
        "movntdq %%xmm5,  80(%[d])\n\t"
        "movntdq %%xmm6,  96(%[d])\n\t"
        "movntdq %%xmm7, 112(%[d])\n\t"

        "add $128, %[s]\n\t"
        "add $128, %[d]\n\t"
        "dec %[n]\n\t"
        "jnz 1b\n\t"
        : [s] "+r"(s), [d] "+r"(d), [n] "+r"(n)
        :
        : "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory"
    );
}

#endif

__attribute__((always_inline))
static inline void bench_sfence(void)
{
#ifdef __x86_64__
    __asm__ __volatile__("sfence" ::: "memory");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

__attribute__((always_inline))
static inline void rep_movsq_copy(const void *src, void *dst, size_t qwords)
{
    __asm__ __volatile__(
        "movq %0,%%rsi\n\t" \
        "movq %1,%%rdi\n\t" \
        "movq %2,%%rcx\n\t" \
        "cld\n\t" \
        "rep\n\t" \
        "movsq\n\t" \
        :: "g" (src), "g" (dst), "g" (qwords)
        : "rsi", "rdi", "rcx"
    );
}

__attribute__((always_inline))
static inline void rep_movsl_copy(const void *src, void *dst, size_t dwords)
{
    __asm__ __volatile__ (
        "movl %0,%%esi\n\t" \
        "movl %1,%%edi\n\t" \
        "movl %2,%%ecx\n\t" \
        "cld\n\t" \
        "rep\n\t" \
        "movsl\n\t" \
        :: "g" (src), "g" (dst), "g" (dwords)
        : "esi", "edi", "ecx"
    );
}

static uint64_t bench_copy_cycles(uint8_t *dst, const uint8_t *src, size_t bytes, int iter, membench_mode_t mode)
{
    if (iter <= 0 || bytes == 0) {
        return 0;
    }

#ifdef __x86_64__

    uint64_t start, end, overhead;

    if (mode == MEMBENCH_REPMOV) {

        size_t qwords = bytes / 8;

        // Measure overhead
        start = get_tsc();
        for (int i = 0; i < iter; i++) {
            rep_movsq_copy(src, dst, 0);
        }
        end = get_tsc();

        overhead = (end - start);

        // Prime the cache
        rep_movsq_copy(src, dst, qwords);

        start = get_tsc();
        for (int i = 0; i < iter; i++) {
            // Actual timed loop benchmark
            rep_movsq_copy(src, dst, qwords);
        }
        end = get_tsc();

        if ((end- start) > overhead) {
            return (end - start) - overhead;
        } else {
            return 0;
        }
     } else if (mode == MEMBENCH_NT_STORE) {

        size_t blocks = bytes / COPY_BLOCK_BYTES;
        if (blocks == 0) return 0;

        // Measure overhead
        start = get_tsc();
        for (int i = 0; i < iter; i++) {
            bench_copy_sse2_nt_store(dst, src, 0);
        }
        bench_sfence();
        end = get_tsc();

        overhead = (end - start);

        start = get_tsc();
        for (int i = 0; i < iter; i++) {
            // Actual timed loop benchmark
            bench_copy_sse2_nt_store(dst, src, blocks);
        }
        bench_sfence();
        end = get_tsc();

        if ((end- start) > overhead) {
            return (end - start) - overhead;
        } else {
            return 0;
        }


    } else {

        size_t blocks = bytes / COPY_BLOCK_BYTES;
        if (blocks == 0) return 0;

        // Measure overhead
        start = get_tsc();
        for (int i = 0; i < iter; i++) {
            bench_copy_sse2_cached(dst, src, 0);
        }
        end = get_tsc();

        overhead = (end - start);

        start = get_tsc();
        for (int i = 0; i < iter; i++) {
            // Actual timed loop benchmark
            bench_copy_sse2_cached(dst, src, blocks);
        }
        end = get_tsc();

        if ((end- start) > overhead) {
            return (end - start) - overhead;
        } else {
            return 0;
        }
    }
#else
    (void)mode;
    uint64_t start, end, overhead;

    // Measure overhead
    start = get_tsc();
    for (int i = 0; i < iter; i++) {
        rep_movsl_copy(src, dst, 0);
    }
    end = get_tsc();

    overhead = (end - start);

    size_t dwords = bytes / 4;

    // Prime the cache
    rep_movsl_copy(src, dst, dwords);

    start = get_tsc();
    for (int i = 0; i < iter; i++) {
        // Actual timed loop benchmark
        rep_movsl_copy(src, dst, dwords);
    }
    end = get_tsc();

    if ((end- start) > overhead) {
        return (end - start) - overhead;
    } else {
        return 0;
    }
#endif
}

static int choose_iterations(uint8_t *src, uint8_t *dst, size_t len, membench_mode_t mode, uint32_t min_time_ms)
{
    // Pick a small sample iteration count that is measurable for tiny buffers
    // and won't explode runtime for huge working sets
    int sample_iter = 1;
    if (len > 0) {
        // Aim for ~64KB of copy per buffer during the sample, clamp to [1...64]
        const size_t target_sample_bytes = 64 * 1024;
        sample_iter = (int)(target_sample_bytes / len);
        if (sample_iter < 1) sample_iter = 1;
        if (sample_iter > 64) sample_iter = 64;
    }

    // Warm up once (not timed) so the sample reflects real-world behavior
    bench_copy_cycles(dst, src, len, 1, mode);

    uint64_t sample_cycles = bench_copy_cycles(dst, src, len, sample_iter, mode);
    if (sample_cycles == 0) {
        return 0;
    }

    // If we don't have clks_per_msec, we're screwed. Fall back to a fixed iteration count
    if (clks_per_msec == 0) return 16;

    uint64_t min_cycles = (uint64_t)clks_per_msec * (uint64_t)min_time_ms;
    if (min_cycles == 0) return 16;

    // Compute cycles/iteration (rounded up)
    uint64_t cycles_per_iter = (sample_cycles + (uint64_t)sample_iter - 1) / (uint64_t)sample_iter;
    if (cycles_per_iter == 0) {
        cycles_per_iter = 1;
    }

    uint64_t iter64 = (min_cycles + cycles_per_iter - 1) / cycles_per_iter;

    // For large working sets (>= 8MB), a single pass should be long enough
    uint64_t min_iter = (len >= (8U * 1024U * 1024U)) ? 1 : 3;

    if (iter64 < min_iter) {
        iter64 = min_iter;
    }
    if (iter64 > 10000000ULL) {
        iter64 = 10000000ULL;
    }

    return (int)iter64;
}

static uint32_t memspeed(uint8_t *src, uint8_t *dst, size_t len, membench_mode_t mode, uint32_t min_time_ms)
{
    if (len == 0) {
        return 0;
    }

    // Size down so we do'nt have to deal with tails
    len = align_down_size(len, COPY_BLOCK_BYTES);
    if (len < COPY_BLOCK_BYTES) {
        return 0;
    }

    int iter = choose_iterations(src, dst, len, mode, min_time_ms);
    if (iter <= 0) {
        return 0;
    }

    // Second warm-up pass
    bench_copy_cycles(dst, src, len, 1, mode);

    uint64_t cycles = bench_copy_cycles(dst, src, len, iter, mode);
    if (cycles == 0) {
        return 0;
    }


    // Real user-visible traffic is 1 read + 1 write per byte
    uint64_t bytes_moved = (uint64_t)len * (uint64_t)iter * 2ULL;

    // Convert cycles -> ms using clks_per_msec (cycles per millisecond)
    uint64_t bw = (bytes_moved * (uint64_t)clks_per_msec) / cycles;

    if (bw > 0xFFFFFFFFULL) {
        bw = 0xFFFFFFFFULL;
    }

    return (uint32_t)bw;
}


static size_t choose_cache_len_bytes(size_t cache_bytes, size_t lower_cache_bytes, size_t max_workset_bytes)
{
    if (cache_bytes < (2 * COPY_BLOCK_BYTES)) {
        return 0;
    }

    // Conservative default: ~2/3 occupancy of the target cache (src+dst)
    size_t ws = (cache_bytes * 2) / 3;

    // Absolute cap for very large caches (keeps runtime sane)
    if (max_workset_bytes && ws > max_workset_bytes) {
        ws = max_workset_bytes;
    }

    // Try to stay well above the next-lower cache to avoid N-1 hits
    if (lower_cache_bytes) {
        size_t min_ws = lower_cache_bytes * 2;  // target >= 2x lower cache
        if (ws < min_ws) {
            // If we can't reach 2x without risking eviction from this cache,
            // push as high as we safely can (and deal with the reduced separation)
            size_t ws_max = (cache_bytes * 7) / 8;
            ws = (min_ws <= ws_max) ? min_ws : ws_max;
        }
    }

    // Leave headroom for code/data + artifacts
    size_t ws_max = (cache_bytes * 7) / 8;
    if (ws > ws_max) {
        ws = ws_max;
    }

    size_t len = ws / 2;
    len = align_down_size(len, COPY_BLOCK_BYTES);
    if (len < COPY_BLOCK_BYTES) {
        return 0;
    }

    return len;
}

static size_t choose_ram_len_bytes(size_t llc_bytes)
{
    // Working set (src+dst) must (greatly) exceed LLC to force DRAM traffic on repeated passes.
    size_t ws = llc_bytes ? (llc_bytes * 6) : MIN_RAM_WORKSET_BYTES;
    if (ws < MIN_RAM_WORKSET_BYTES) {
        ws = MIN_RAM_WORKSET_BYTES;
    }

    size_t len = ws / 2;
    len = align_down_size(len, COPY_BLOCK_BYTES);
    return len;
}

static uintptr_t find_bench_region(size_t bytes_needed)
{
    uintptr_t bench_start_adr = 0;

    // Locate enough free space for tests. We require the space to be mapped into
    // our virtual address space, which is limited to the first 2GB.
    for (int i = 0; i < pm_map_size && pm_map[i].start < VM_PINNED_SIZE; i++) {
        uintptr_t try_start = pm_map[i].start << PAGE_SHIFT;
        uintptr_t try_end   = try_start + bytes_needed;

        // No start address < BENCH_MIN_START_ADR
        if (try_start < BENCH_MIN_START_ADR) {
            if ((pm_map[i].end << PAGE_SHIFT) >= (BENCH_MIN_START_ADR + bytes_needed)) {
                try_start = BENCH_MIN_START_ADR;
                try_end   = try_start + bytes_needed;
            } else {
                continue;
            }
        }

        // Avoid the memory region where the program is currently located.
        if (try_start < (uintptr_t)_end && try_end > (uintptr_t)_start) {
            try_start = (uintptr_t)_end;
            try_end   = try_start + bytes_needed;
        }

        uintptr_t end_limit = (pm_map[i].end < VM_PINNED_SIZE ? pm_map[i].end : VM_PINNED_SIZE) << PAGE_SHIFT;
        if (try_end <= end_limit) {
            bench_start_adr = try_start;
            break;
        }
    }

    return bench_start_adr;
}

static void measure_memory_bandwidth(void)
{
    // Convert cache sizes from KB to B
    const size_t l1_bytes = (l1_cache > 0) ? ((size_t)l1_cache * 1024) : 0;
    const size_t l2_bytes = (l2_cache > 0) ? ((size_t)l2_cache * 1024) : 0;
    const size_t l3_bytes = (l3_cache > 0) ? ((size_t)l3_cache * 1024) : 0;

    // The last-level cache (LLC) present (L3 -> L2 -> L1)
    const size_t llc_bytes = l3_bytes ? l3_bytes : (l2_bytes ? l2_bytes : l1_bytes);
    if (llc_bytes == 0) {
        return;
    }

    // Choose working-set sizes. Caps are for the combined working set (src+dst footprint)
    const size_t max_l2_ws =  2 * 1024 * 1024;   //  2MB for L2 tests
    const size_t max_l3_ws = 16 * 1024 * 1024;   // 16MB for L3 tests

    size_t l1_len = 0, l2_len = 0, l3_len = 0;

    if (l1_bytes) {
        l1_len = choose_cache_len_bytes(l1_bytes, 0, 0);
    }
    if (l2_bytes) {
        l2_len = choose_cache_len_bytes(l2_bytes, l1_bytes, max_l2_ws);
    }
    if (l3_bytes) {
        l3_len = choose_cache_len_bytes(l3_bytes, l2_bytes ? l2_bytes : l1_bytes, max_l3_ws);
    }

    // For DRAM, pick a working set larger than LLC (but allow shrinking if physical memory is limited)
    size_t ram_len = choose_ram_len_bytes(llc_bytes);
    if (ram_len == 0) {
        return;
    }

    // Determine the maximum single-buffer length we must map
    size_t max_len = ram_len;
    if (l3_len > max_len) max_len = l3_len;
    if (l2_len > max_len) max_len = l2_len;
    if (l1_len > max_len) max_len = l1_len;

    // Worst-case bytes required for 2 buffers + alignment + skew
    size_t bytes_needed = (2 * max_len) + (2 * BENCH_ALIGN) + BENCH_DST_SKEW ;

    // Try to find a region. If we can't fit the RAM working set, shrink it until it fits
    uintptr_t bench_start_adr = 0;
    while (bench_start_adr == 0) {
        bench_start_adr = find_bench_region(bytes_needed);
        if (bench_start_adr != 0) {
            break;
        }

        // Shrink RAM buffer and retry.
        if (ram_len <= (256U * 1024U)) {
            return;
        }

        ram_len /= 2;
        ram_len = align_down_size(ram_len, COPY_BLOCK_BYTES);
        if (ram_len < COPY_BLOCK_BYTES) {
            return;
        }

        max_len = ram_len;
        if (l3_len > max_len) max_len = l3_len;
        if (l2_len > max_len) max_len = l2_len;
        if (l1_len > max_len) max_len = l1_len;

        bytes_needed = (2 * max_len) + BENCH_DST_SKEW + (2 * BENCH_ALIGN);
    }

    uint8_t *src = (uint8_t *)align_up_ptr(bench_start_adr, BENCH_ALIGN);
    uint8_t *dst = (uint8_t *)align_up_ptr((uintptr_t)src + max_len + BENCH_DST_SKEW, BENCH_ALIGN);

    // Measure L1/L2/L3 Cache bandwidths
    //
    // On x86_64, we use extremely fast rep movs for L1/L2 and SIMD instructions for L3/DRAM
    // On i586, we use rep movs everywhere.
    //
    // Each level is measured by copying a working set that fit within the target cache.
    if (l1_len) {
        l1_cache_speed = memspeed(src, dst, l1_len, MEMBENCH_REPMOV, 50);
    }
    if (l2_len) {
        l2_cache_speed = memspeed(src, dst, l2_len, MEMBENCH_REPMOV, 100);
    }
    if (l3_len) {
        l3_cache_speed = memspeed(src, dst, l3_len, MEMBENCH_CACHED, 150);
    }

    // --- Measure DRAM bandwidth ---
#ifdef __x86_64__
    // On x86_64, we use non-temporal stores to avoid measuring LLC writeback.
    ram_speed = memspeed(src, dst, ram_len, MEMBENCH_NT_STORE, 200);
#else
    ram_speed = memspeed(src, dst, ram_len, MEMBENCH_CACHED, 200);
#endif
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void membw_init(void)
{
    if (quirk.type & QUIRK_TYPE_MEM_SIZE) {
        quirk.process();
    }

    if(enable_bench) {
        measure_memory_bandwidth();
    }
}
