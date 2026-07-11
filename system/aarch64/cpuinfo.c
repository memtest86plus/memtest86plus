// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2026 Sam Demeulemeester
//
// ARM64 (AArch64) CPU information.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpuid.h"
#include "tsc.h"

#include "boot.h"
#include "config.h"
#include "pmem.h"
#include "memsize.h"

#include "cpuinfo.h"

#include "registers.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// RAM may start well above address 0 on this architecture, so the minimum
// benchmark address is an offset from the base of RAM, not an absolute adr.
#define BENCH_MIN_START_OFFSET 0x10000000   // 256MB

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

const char  *cpu_model = "";    // static init required: creates the reloc record that rebases it

int         l1_cache = 0;
int         l2_cache = 0;
int         l3_cache = 0;

uint32_t    l1_cache_speed  = 0;
uint32_t    l2_cache_speed  = 0;
uint32_t    l3_cache_speed  = 0;
uint32_t    ram_speed = 0;

uint32_t    clks_per_msec = 0;
uint32_t    cpu_clk_mhz = 0;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static void determine_cache_size(void)
{
    uint64_t clidr = read_sysreg(clidr_el1);

    bool has_ccidx = ((read_sysreg(id_aa64mmfr2_el1) >> 20) & 0xF) != 0;

    for (int level = 1; level <= 3; level++) {
        int cache_type = (clidr >> (3 * (level - 1))) & 0x7;
        if (cache_type == 0) {
            break;
        }

        // Select the data or unified cache at this level
        write_sysreg((level - 1) << 1, csselr_el1);
        __asm__ __volatile__ ("isb");
        uint64_t ccsidr = read_sysreg(ccsidr_el1);

        uint64_t line_size, ways, sets;
        line_size = UINT64_C(1) << ((ccsidr & 0x7) + 4);
        if (has_ccidx) {
            ways = ((ccsidr >>  3) & 0x1FFFFF) + 1;
            sets = ((ccsidr >> 32) & 0xFFFFFF) + 1;
        } else {
            ways = ((ccsidr >>  3) & 0x3FF) + 1;
            sets = ((ccsidr >> 13) & 0x7FFF) + 1;
        }

        int size = (line_size * ways * sets) / 1024;
        switch (level) {
          case 1:
            l1_cache = size;
            break;
          case 2:
            l2_cache = size;
            break;
          case 3:
            l3_cache = size;
            break;
        }
    }
}

// Copy wlen * 64 bytes from src to dst. With wlen == 0, just executes the
// loop entry/exit overhead.
static inline void copy_block(uintptr_t src, uintptr_t dst, uintptr_t wlen)
{
    __asm__ __volatile__ (
        "mov    x9, %0\n\t"
        "mov    x10, %1\n\t"
        "mov    x11, %2\n\t"
        "2:\n\t"
        "cbz    x11, 1f\n\t"
        "ldp    x12, x13, [x9]\n\t"
        "ldp    x14, x15, [x9, #16]\n\t"
        "stp    x12, x13, [x10]\n\t"
        "stp    x14, x15, [x10, #16]\n\t"
        "ldp    x12, x13, [x9, #32]\n\t"
        "ldp    x14, x15, [x9, #48]\n\t"
        "stp    x12, x13, [x10, #32]\n\t"
        "stp    x14, x15, [x10, #48]\n\t"
        "add    x9, x9, #64\n\t"
        "add    x10, x10, #64\n\t"
        "sub    x11, x11, #1\n\t"
        "b      2b\n\t"
        "1:\n\t"
        :: "r" (src), "r" (dst), "r" (wlen)
        : "x9", "x10", "x11", "x12", "x13", "x14", "x15", "memory"
    );
}

static void pmu_cycle_counter_enable(void)
{
    write_sysreg(read_sysreg(pmcr_el0) | 0x5, pmcr_el0);    // enable, reset cycle counter
    write_sysreg(UINT64_C(1) << 31, pmcntenset_el0);        // enable the cycle counter
    write_sysreg(0, pmccfiltr_el0);                         // count cycles at EL1
    __asm__ __volatile__ ("isb");
}

static void pmu_cycle_counter_disable(void)
{
    write_sysreg(read_sysreg(pmcr_el0) & ~UINT64_C(1), pmcr_el0);
}

static uint32_t memspeed(uintptr_t src, uint32_t len, int iter)
{
    uintptr_t dst;
    uintptr_t wlen;
    uint64_t start_time, end_time, run_time_clk, overhead;
    int i;

    // get_tsc() counts generic timer ticks (not CPU clocks, unlike
    // clks_per_msec), so express time in generic timer ticks throughout.
    uint32_t ticks_per_msec = read_sysreg(cntfrq_el0) / 1000;
    if (ticks_per_msec == 0) {
        return 0;
    }

    dst = src + len;

    wlen = len / 64;
    // Get number of clock cycles due to overhead
    start_time = get_tsc();
    for (i = 0; i < iter; i++) {
        copy_block(src, dst, 0);
    }
    end_time = get_tsc();

    overhead = (end_time - start_time);

    // Prime the cache
    copy_block(src, dst, wlen);

    // Copy these bytes
    uint64_t start_cycles = read_sysreg(pmccntr_el0);
    start_time = get_tsc();
    for (i = 0; i < iter; i++) {
        copy_block(src, dst, wlen);
    }
    end_time = get_tsc();
    uint64_t cycles = read_sysreg(pmccntr_el0) - start_cycles;
    if ((end_time - start_time) > overhead) {
        run_time_clk = (end_time - start_time) - overhead;
    } else {
        return 0;
    }

    // Suppress results when cycles roughly match timer ticks,
    // indicating Snapdragon X core clock clamped near 19.2MHz under load
    if (cpu_clk_mhz > 0 && cycles < 4 * run_time_clk) {
        return 0;
    }

    run_time_clk = (((uint64_t)len * iter) / (double)run_time_clk) * ticks_per_msec * 2;

    return run_time_clk;
}

static void measure_memory_bandwidth(void)
{
    uintptr_t bench_start_adr = 0;
    size_t mem_test_len;

    if (l3_cache) {
        mem_test_len = 4*l3_cache*1024;
    } else if (l2_cache) {
        mem_test_len = 4*l2_cache*1024;
    } else {
        return; // If we're not able to detect L2, don't start benchmark
    }

    uintptr_t bench_min_adr = (pm_map[0].start << PAGE_SHIFT) + BENCH_MIN_START_OFFSET;

    // Locate enough free space for testing.
    for (int i = 0; i < pm_map_size; i++) {
        uintptr_t try_start = pm_map[i].start << PAGE_SHIFT;
        uintptr_t try_end   = try_start + mem_test_len * 2;

        // No start address below bench_min_adr
        if (try_start < bench_min_adr) {
            if ((pm_map[i].end << PAGE_SHIFT) >= (bench_min_adr + mem_test_len * 2)) {
                try_start = bench_min_adr;
                try_end   = bench_min_adr + mem_test_len * 2;
            } else {
                continue;
            }
        }

        // Avoid the memory region where the program is currently located.
        if (try_start < (uintptr_t)_end && try_end > (uintptr_t)_start) {
            try_start = (uintptr_t)_end;
            try_end   = try_start + mem_test_len * 2;
        }

        uintptr_t end_limit = pm_map[i].end << PAGE_SHIFT;
        if (try_end <= end_limit) {
            bench_start_adr = try_start;
            break;
        }
    }

    if (bench_start_adr == 0) {
        return;
    }

    pmu_cycle_counter_enable();

    // Measure L1 BW using 1/3rd of the total L1 cache size
    if (l1_cache) {
        l1_cache_speed = memspeed(bench_start_adr, (l1_cache/3)*1024, 50);
    }

    // Measure L2 BW using half the L2 cache size
    if (l2_cache) {
        l2_cache_speed = memspeed(bench_start_adr, l2_cache/2*1024, 50);
    }

    // Measure L3 BW using half the L3 cache size
    if (l3_cache) {
        l3_cache_speed = memspeed(bench_start_adr, l3_cache/2*1024, 50);
    }

    // Measure RAM BW
    ram_speed = memspeed(bench_start_adr, mem_test_len, 25);

#if defined(BENCH_DEBUG)
    bench_diag_collect(bench_start_adr, mem_test_len);
#endif

    pmu_cycle_counter_disable();
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void cpuinfo_init(void)
{
    determine_cache_size();

    cpu_model = cpuid_info.brand_id.str;
}

void membw_init(void)
{
    if (enable_bench) {
        measure_memory_bandwidth();
    }
}
