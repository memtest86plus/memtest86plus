// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2004-2023 Sam Demeulemeester
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.
//
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <larchintrin.h>

#include "cpuid.h"
#include "tsc.h"

#include "boot.h"
#include "config.h"
#include "pmem.h"
#include "vmem.h"
#include "memctrl.h"
#include "memsize.h"
#include "hwquirks.h"

#include "cpuinfo.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define BENCH_MIN_START_ADR 0x10000000   // 256MB

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

const char  *cpu_model = NULL;

int         l1_cache = 0;
int         l2_cache = 0;
int         l3_cache = 0;

uint32_t    l1_cache_speed  = 0;
uint32_t    l2_cache_speed  = 0;
uint32_t    l3_cache_speed  = 0;
uint32_t    ram_speed = 0;

uint32_t    clks_per_msec = 0;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static void determine_cache_size()
{
  uint32_t cache_info = 0;
  uint32_t cache_present = 0;
  uint32_t ways = 0;
  uint32_t index = 0;
  uint32_t line_size = 0;

  // Get the persentence of caches
  cache_present = __cpucfg(0x10);

  // L1 D cache persent
  if (cache_present & (0x1 << 0)) {
    cache_info = __cpucfg(0x12);
    ways = (cache_info & 0xFFFF) + 1;
    index = 1 << ((cache_info >> 16) & 0xFF);
    line_size = 1 << ((cache_info >> 24) & 0x7F);
    l1_cache = (ways * index * line_size) / 1024;
  }

  // L2 cache persent
  if (cache_present & (0x1 << 3)) {
    cache_info = __cpucfg(0x13);
    ways = (cache_info & 0xFFFF) + 1;
    index = 1 << ((cache_info >> 16) & 0xFF);
    line_size = 1 << ((cache_info >> 24) & 0x7F);
    l2_cache = (ways * index * line_size) / 1024;
  }

  // L3 cache persent
  if (cache_present & (0x1 << 10)) {
    cache_info = __cpucfg(0x14);
    ways = (cache_info & 0xFFFF) + 1;
    index = 1 << ((cache_info >> 16) & 0xFF);
    line_size = 1 << ((cache_info >> 24) & 0x7F);
    l3_cache = (ways * index * line_size) / 1024;
  }
}

static void determine_imc(void)
{
  // Check Loongson IMC
  if (cpuid_info.vendor_id.str[0] == 'L') {
    imc.family = IMC_LSLA;
    switch (cpuid_info.brand_id.str[0x12])
    {
      case '5':
        imc.family = IMC_LA464;
        break;
      case '6':
        imc.family = IMC_LA664;
        break;
      default:
        break;
    }
  }
}

static void determine_cpu_model(void)
{
    cpu_model = cpuid_info.brand_id.str;
    determine_imc();
    return;
}

static uint32_t memspeed(uintptr_t src, uint32_t len, int iter)
{
    uintptr_t dst;
    uintptr_t wlen;
    uint64_t start_time, end_time, run_time_clk, overhead;
    int i;

    dst = src + len;

    wlen = len / 8;
    // Get number of clock cycles due to overhead
    start_time = get_tsc();
    for (i = 0; i < iter; i++) {
      __asm__ __volatile__ (
          "move   $t0, %0\n\t"        \
          "move   $t1, %1\n\t"        \
          "move   $t2, %2\n\t"        \
          "2:\n\t"                    \
          "beqz   $t2, 1f\n\t"        \
          "addi.d $t0, $t0, 8\n\t"    \
          "addi.d $t1, $t1, 8\n\t"    \
          "addi.d $t2, $t2, -1\n\t"   \
          "b      2b\n\t"             \
          "1:\n\t"                    \
          :: "r" (src), "r" (dst), "r" (0)
          : "$t0", "$t1", "$t2"
      );
    }
    end_time = get_tsc();

    overhead = (end_time - start_time);

    // Prime the cache
    __asm__ __volatile__ (
        "move   $t0, %0\n\t"        \
        "move   $t1, %1\n\t"        \
        "move   $t2, %2\n\t"        \
        "2:\n\t"                    \
        "beqz   $t2, 1f\n\t"        \
        "ld.d   $t3, $t0, 0x0\n\t"       \
        "st.d   $t3, $t1, 0x0\n\t"       \
        "addi.d $t0, $t0, 8\n\t"    \
        "addi.d $t1, $t1, 8\n\t"    \
        "addi.d $t2, $t2, -1\n\t"   \
        "b      2b\n\t"             \
        "1:\n\t"                    \
        :: "r" (src), "r" (dst), "r" (wlen)
        : "$t0", "$t1", "$t2", "$t3"
    );

    // Write these bytes
    start_time = get_tsc();
    for (i = 0; i < iter; i++) {
      __asm__ __volatile__ (
          "move   $t0, %0\n\t"        \
          "move   $t1, %1\n\t"        \
          "move   $t2, %2\n\t"        \
          "2:\n\t"                    \
          "beqz   $t2, 1f\n\t"        \
          "ld.d   $t3, $t0, 0x0\n\t"       \
          "st.d   $t3, $t1, 0x0\n\t"       \
          "addi.d $t0, $t0, 8\n\t"    \
          "addi.d $t1, $t1, 8\n\t"    \
          "addi.d $t2, $t2, -1\n\t"   \
          "b      2b\n\t"             \
          "1:\n\t"                    \
          :: "r" (src), "r" (dst), "r" (wlen)
          : "$t0", "$t1", "$t2", "$t3"
      );
    }
    end_time = get_tsc();
    if ((end_time - start_time) > overhead) {
        run_time_clk = (end_time - start_time) - overhead;
    } else {
        return 0;
    }

    run_time_clk = ((len * iter) / (double)run_time_clk) * clks_per_msec * 2;

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

    // Locate enough free space under the 4G address space for testing.
    for (int i = 0; i < pm_map_size && pm_map[i].start < PAGE_C(4,GB); i++) {
        uintptr_t try_start = pm_map[i].start << PAGE_SHIFT;
        uintptr_t try_end   = try_start + mem_test_len * 2;

        // No start address below BENCH_MIN_START_ADR
        if (try_start < BENCH_MIN_START_ADR) {
            if ((pm_map[i].end << PAGE_SHIFT) >= (BENCH_MIN_START_ADR + mem_test_len * 2)) {
                try_start = BENCH_MIN_START_ADR;
                try_end   = BENCH_MIN_START_ADR + mem_test_len * 2;
            } else {
                continue;
            }
        }

        // Avoid the memory region where the program is currently located.
        if (try_start < (uintptr_t)_end && try_end > (uintptr_t)_start) {
            try_start = (uintptr_t)_end;
            try_end   = try_start + mem_test_len * 2;
        }

        uintptr_t end_limit = (pm_map[i].end < PAGE_C(4,GB) ? pm_map[i].end : PAGE_C(4,GB)) << PAGE_SHIFT;
        if (try_end <= end_limit) {
            bench_start_adr = try_start;
            break;
        }
    }

    if (bench_start_adr == 0) {
        return;
    }

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
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void cpuinfo_init(void)
{
    // Get cache sizes for most AMD and Intel CPUs. Exceptions for old
    // CPUs are handled in determine_cpu_model().
    determine_cache_size();

    determine_cpu_model();
}

void membw_init(void)
{
    if(enable_bench) {
        measure_memory_bandwidth();
    }
}
