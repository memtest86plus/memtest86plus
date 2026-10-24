#!/usr/bin/env python3
"""Check current NUMA_PAR calculate_chunk partitions without accessing RAM."""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]

from c_extract import extract_function  # noqa: E402


PREAMBLE = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

typedef uintptr_t testword_t;
#define VMEM_MAX_CONTEXTS 2
#define MAX_CPUS 1024
#define SPIN_SIZE (1 << 27)
#define NUMA_ON 1
typedef struct {
    testword_t *start, *end;
    uint32_t proximity_domain_idx;
} vm_map_t;
typedef struct { unsigned active_cpu_count; } test_context_t;
static vm_map_t vm_map[2][1];
static test_context_t test_contexts[2];
static bool numa_run_active = true;
static int numa_mode, num_active_cpus;
static bool numa_boot_cpu_subset[MAX_CPUS] __attribute__((unused));
static unsigned test_team_chunk_index[MAX_CPUS], chunk_index[MAX_CPUS];
static unsigned used_cpus_in_proximity_domain[64];
static uintptr_t round_down(uintptr_t value, size_t align)
{ return value & ~(align - 1); }
static uintptr_t round_up(uintptr_t value, size_t align)
{ return (value + align - 1) & ~(align - 1); }
static int test_context_index(void) { return 0; }
static uint32_t smp_get_proximity_domain_idx(int cpu) { (void)cpu; return 0; }
"""

MAIN = r"""
static void check_partition(uintptr_t base, uintptr_t size, unsigned cpus,
                            size_t alignment)
{
    vm_map[0][0] = (vm_map_t){ .start = (testword_t *)base,
                              .end = (testword_t *)(base + size - sizeof(testword_t)) };
    test_contexts[0].active_cpu_count = cpus;
    uintptr_t next = base;
    for (unsigned cpu = 0; cpu < cpus; ++cpu) {
        testword_t *start, *end;
        test_team_chunk_index[cpu] = cpu;
        calculate_chunk(&start, &end, cpu, 0, alignment);
        // Inclusive end plus one word is the next boundary, also for empty
        // chunks. These checks establish complete coverage with no overlap.
        assert((uintptr_t)start == next);
        assert((uintptr_t)end + sizeof(testword_t) >= next);
        assert((uintptr_t)end < base + size);
        next = (uintptr_t)end + sizeof(testword_t);
    }
    assert(next == base + size);
}

int main(void)
{
    unsigned cases = 0;
    for (unsigned pages = 1; pages <= 32; ++pages) {
        for (unsigned cpus = 1; cpus <= MAX_CPUS; ++cpus) {
            for (size_t alignment = 8; alignment <= 128; alignment *= 4) {
                check_partition(0x10000000, pages * 4096, cpus, alignment);
                ++cases;
            }
        }
    }
    uintptr_t block = (uintptr_t)SPIN_SIZE * sizeof(testword_t);
    for (unsigned cpus = 1; cpus <= MAX_CPUS; ++cpus) {
        for (size_t alignment = 8; alignment <= 128; alignment *= 4) {
            check_partition(0x80000000, block, cpus, alignment);
            ++cases;
        }
    }
    assert(cases == 101376);
    // The USB low-memory guard removes 0x500 bytes from the first page.
    for (unsigned cpus = 1; cpus <= MAX_CPUS; ++cpus) {
        for (size_t alignment = 8; alignment <= 128; alignment *= 4) {
            check_partition(0x500, 4096 - 0x500, cpus, alignment);
        }
    }
    printf("NUMA_PAR calculate_chunk: %u partition cases and 3072 USB-guard"
           " cases passed\n", cases);
    return 0;
}
"""


def main():
    helper = (ROOT / "tests/test_helper.c").read_text()
    functions = "\n".join(
        extract_function(helper, name)
        for name in ("equalize_block_counts", "calculate_chunk")
    )
    with tempfile.TemporaryDirectory(prefix="numa-chunks-") as directory:
        source = Path(directory) / "regression.c"
        binary = Path(directory) / "regression"
        source.write_text(PREAMBLE + "\n" + functions + "\n" + MAIN)
        compiler = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(
            compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(source), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    main()
