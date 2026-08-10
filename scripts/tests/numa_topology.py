#!/usr/bin/env python3
"""Run host regressions against the current LoongArch NUMA mapping source.

No LoongArch CPU or cross compiler is needed: the tested functions only do
address arithmetic. Their bodies are extracted from system/smp.c and
system/loongarch/vmem.c on every run. The harness never dereferences its
synthetic physical addresses.

    python3 scripts/tests/numa_topology.py --sanitize
"""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from c_extract import extract_function as _extract_function


def extract_function(path, name, optional=False):
    # Keep the original file/line in compiler diagnostics for the synthetic
    # translation unit.
    return _extract_function(
        path.read_text(), name, path=path, optional=optional, line_directive=True
    )


PREAMBLE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __loongarch_lp64 1
#define VMEM_MAX_CONTEXTS 16
#define PAGE_SHIFT 12
#define PAGE_SIZE (1ULL << PAGE_SHIFT)
#define VM_PINNED_SIZE ((2ULL << 30) >> PAGE_SHIFT)
#define NUMA_NODE_OFFSET 44
#define CAPACITY 128
_Static_assert(sizeof(uintptr_t) == 8, "NUMA host regressions require a 64-bit host");
typedef struct { uint32_t proximity_domain_idx; uint64_t start, end; } memory_affinity_t;
typedef struct { uintptr_t start, end; } pm_map_t;
static memory_affinity_t memory_affinity_ranges[CAPACITY];
static pm_map_t pm_map[CAPACITY];
static int num_memory_affinity_ranges, num_proximity_domains, pm_map_size;
static bool map_numa_memory_range;
static uint8_t highest_map_bit;
bool smp_numa_transform_valid(void);
'''

HARNESS = r'''
#line 1 "numa_topology_harness.c"
static int failures;
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)
#define GIB (1ULL << 30)
#define NODE(n) ((uint64_t)(n) << NUMA_NODE_OFFSET)

static void reset_maps(void)
{
    memset(memory_affinity_ranges, 0, sizeof(memory_affinity_ranges));
    memset(pm_map, 0, sizeof(pm_map));
    num_memory_affinity_ranges = num_proximity_domains = pm_map_size = 0;
    map_numa_memory_range = false;
    highest_map_bit = 0;
}

static void add_pm(uint64_t start, uint64_t end)
{
    assert(pm_map_size < CAPACITY && start % PAGE_SIZE == 0 && end % PAGE_SIZE == 0);
    pm_map[pm_map_size++] = (pm_map_t){start >> PAGE_SHIFT, end >> PAGE_SHIFT};
}

static void add_affinity(uint32_t domain, uint64_t start, uint64_t end)
{
    assert(num_memory_affinity_ranges < CAPACITY);
    memory_affinity_ranges[num_memory_affinity_ranges++] = (memory_affinity_t){domain, start, end};
    if ((int)domain >= num_proximity_domains) num_proximity_domains = domain + 1;
}

static void check_segment(int segment, uint64_t original_start, uint64_t original_end, uint32_t domain)
{
    uintptr_t start = pm_map[segment].start, end = pm_map[segment].end;
    CHECK(end - start == (original_end - original_start) / PAGE_SIZE);
    uintptr_t samples[] = {start, start + (end - start) / 2, end - 1};
    for (unsigned int i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        uintptr_t physical = original_start + (samples[i] - start) * PAGE_SIZE;
        CHECK((uintptr_t)first_word_mapping(samples[i], 0) == physical);
        // Host UBSan treats pointer arithmetic from synthetic physical zero
        // as null-pointer arithmetic; the freestanding target permits it.
        if (physical != 0) CHECK((uintptr_t)last_word_mapping(samples[i], sizeof(uintptr_t), 0) == physical + PAGE_SIZE - sizeof(uintptr_t));
        CHECK(page_of(first_word_mapping(samples[i], 0), 0) == physical / PAGE_SIZE);
    }
    uint32_t owner = UINT32_MAX;
    uint64_t narrowed_start = 0, narrowed_end = 0;
    CHECK(smp_narrow_to_proximity_domain(start << PAGE_SHIFT, end << PAGE_SHIFT,
                                        &owner, &narrowed_start, &narrowed_end));
    CHECK(owner == domain);
    CHECK(narrowed_start == start << PAGE_SHIFT && narrowed_end == end << PAGE_SHIFT);
}

static void reversed_srat_order(void)
{
    reset_maps();
    add_pm(0, GIB);
    add_pm(NODE(1), NODE(1) + GIB);
    // Domain ordinal zero is deliberately the encoded physical node one.
    add_affinity(0, NODE(1), NODE(1) + GIB);
    add_affinity(1, 0, GIB);
    check_if_needs_to_map();
    CHECK(map_numa_memory_range && highest_map_bit == 31);
    CHECK(sort_and_validate_memory_affinity_ranges());
    CHECK(memory_affinity_ranges[0].proximity_domain_idx == 1);
    CHECK(memory_affinity_ranges[1].proximity_domain_idx == 0);
    check_segment(0, 0, GIB, 1);
    check_segment(1, NODE(1), NODE(1) + GIB, 0);
    CHECK(smp_numa_transform_valid());
}

static void largest_domain_zero(void)
{
    reset_maps();
    add_pm(0, GIB / 4);
    add_pm(8 * GIB, 9 * GIB);
    add_pm(NODE(1), NODE(1) + GIB);
    add_affinity(0, 8 * GIB, 9 * GIB);
    add_affinity(1, NODE(1), NODE(1) + GIB);
    add_affinity(0, 0, GIB / 4);
    check_if_needs_to_map();
    CHECK(map_numa_memory_range && highest_map_bit == 34);
    CHECK(sort_and_validate_memory_affinity_ranges());
    check_segment(0, 0, GIB / 4, 0);
    // This was incorrectly decoded as physical node four (64 TiB).
    check_segment(1, 8 * GIB, 9 * GIB, 0);
    check_segment(2, NODE(1), NODE(1) + GIB, 1);
    CHECK(smp_numa_transform_valid());
}

static void pm_map_participates_in_sizing(void)
{
    reset_maps();
    add_pm(0, GIB / 4);
    add_pm(8 * GIB, 9 * GIB); // Available RAM absent from the partial SRAT.
    add_pm(NODE(1), NODE(1) + GIB);
    add_affinity(0, 0, GIB / 4);
    add_affinity(1, NODE(1), NODE(1) + GIB);
    check_if_needs_to_map();
    CHECK(map_numa_memory_range && highest_map_bit == 34);
    CHECK((uintptr_t)first_word_mapping(pm_map[1].start, 0) == 8 * GIB);
    CHECK((uintptr_t)first_word_mapping(pm_map[2].start, 0) == NODE(1));
    CHECK(smp_numa_transform_valid());
}

static void full_node_end_boundary(void)
{
    reset_maps();
    add_pm(NODE(15), NODE(16));
    add_affinity(0, NODE(15), NODE(16));
    check_if_needs_to_map();
    CHECK(map_numa_memory_range && highest_map_bit == NUMA_NODE_OFFSET);
    CHECK(sort_and_validate_memory_affinity_ranges());
    check_segment(0, NODE(15), NODE(16), 0);
    CHECK(smp_numa_transform_valid());
}

static void expect_identity(void)
{
    memory_affinity_t old_affinity[CAPACITY];
    pm_map_t old_pm[CAPACITY];
    memcpy(old_affinity, memory_affinity_ranges, sizeof(old_affinity));
    memcpy(old_pm, pm_map, sizeof(old_pm));
    check_if_needs_to_map();
    CHECK(!map_numa_memory_range && highest_map_bit == 0);
    CHECK(memcmp(old_affinity, memory_affinity_ranges, sizeof(old_affinity)) == 0);
    CHECK(memcmp(old_pm, pm_map, sizeof(old_pm)) == 0);
    CHECK(smp_numa_transform_valid());
    CHECK(vmem_prepare_execution_contexts(VMEM_MAX_CONTEXTS));
    for (int i = 0; i < pm_map_size; i++) {
        CHECK((uintptr_t)first_word_mapping(pm_map[i].start, 0) == pm_map[i].start << PAGE_SHIFT);
    }
}

static void crossing_node_keeps_identity(void)
{
    reset_maps();
    add_pm(0, PAGE_SIZE);
    add_pm(NODE(1) - PAGE_SIZE, NODE(1) + PAGE_SIZE);
    add_affinity(0, 0, PAGE_SIZE);
    add_affinity(1, NODE(1) - PAGE_SIZE, NODE(1) + PAGE_SIZE);
    expect_identity();
}

static void late_affinity_failure_is_atomic(void)
{
    reset_maps();
    add_pm(0, PAGE_SIZE);
    add_pm(NODE(1), NODE(1) + PAGE_SIZE);
    add_affinity(1, NODE(1), NODE(1) + PAGE_SIZE);
    // In particular, domain zero must not skip the boundary validation.
    add_affinity(0, NODE(2) - PAGE_SIZE, NODE(2) + PAGE_SIZE);
    expect_identity();
}

static void wider_local_address_keeps_identity(void)
{
    reset_maps();
    const uint64_t physical = (1ULL << 48) | NODE(1);
    add_pm(physical, physical + PAGE_SIZE);
    add_affinity(0, physical, physical + PAGE_SIZE);
    expect_identity();
}

static void same_domain_overlaps_normalize(void)
{
    reset_maps();
    add_pm(0, PAGE_SIZE);
    add_pm(NODE(1), NODE(1) + 3 * PAGE_SIZE);
    add_affinity(1, NODE(1) + PAGE_SIZE, NODE(1) + 3 * PAGE_SIZE);
    add_affinity(0, 0, PAGE_SIZE);
    add_affinity(1, NODE(1), NODE(1) + 2 * PAGE_SIZE);
    check_if_needs_to_map();
    CHECK(map_numa_memory_range);
    CHECK(sort_and_validate_memory_affinity_ranges());
    CHECK(num_memory_affinity_ranges == 2);
    check_segment(0, 0, PAGE_SIZE, 0);
    check_segment(1, NODE(1), NODE(1) + 3 * PAGE_SIZE, 1);
    CHECK(smp_numa_transform_valid());
}

static void same_domain_distinct_nodes_stay_separate(void)
{
    reset_maps();
    add_pm(0, 2 * GIB);
    add_pm(NODE(1), NODE(1) + 2 * GIB);
    add_affinity(0, NODE(1), NODE(1) + 2 * GIB);
    add_affinity(0, 0, 2 * GIB);
    check_if_needs_to_map();
    CHECK(map_numa_memory_range && highest_map_bit == 31);
    CHECK(sort_and_validate_memory_affinity_ranges());
    CHECK(num_memory_affinity_ranges == 2);
    check_segment(0, 0, 2 * GIB, 0);
    check_segment(1, NODE(1), NODE(1) + 2 * GIB, 0);
    CHECK(smp_numa_transform_valid());
}

static void cross_domain_overlap_keeps_identity(void)
{
    reset_maps();
    add_pm(0, 3 * PAGE_SIZE);
    add_affinity(0, 0, 2 * PAGE_SIZE);
    add_affinity(1, PAGE_SIZE, 3 * PAGE_SIZE);
    expect_identity();
    CHECK(!sort_and_validate_memory_affinity_ranges());
}

static void post_transform_validation(void)
{
    reset_maps();
    for (int node = 0; node < 3; node++) add_pm(NODE(node), NODE(node) + 2 * PAGE_SIZE);
    for (int node = 2; node >= 0; node--) add_affinity(2 - node, NODE(node), NODE(node) + 2 * PAGE_SIZE);
    check_if_needs_to_map();
    CHECK(map_numa_memory_range && highest_map_bit == 31);
    CHECK(sort_and_validate_memory_affinity_ranges());
    CHECK(smp_numa_transform_valid());
    CHECK(vmem_prepare_execution_contexts(2));
    CHECK(vmem_prepare_execution_contexts(VMEM_MAX_CONTEXTS));
    CHECK(!vmem_prepare_execution_contexts(0));
    CHECK(!vmem_prepare_execution_contexts(VMEM_MAX_CONTEXTS + 1));
    memory_affinity_t affinity_snapshot[CAPACITY];
    pm_map_t pm_snapshot[CAPACITY];
    memcpy(affinity_snapshot, memory_affinity_ranges, sizeof(affinity_snapshot));
    memcpy(pm_snapshot, pm_map, sizeof(pm_snapshot));
    check_if_needs_to_map(); // Never compact an already canonical map again.
    CHECK(memcmp(affinity_snapshot, memory_affinity_ranges, sizeof(affinity_snapshot)) == 0);
    CHECK(memcmp(pm_snapshot, pm_map, sizeof(pm_snapshot)) == 0);
    pm_map[1].end = pm_map[1].start; // A pinned allocation consumes a segment.
    CHECK(smp_numa_transform_valid());
    memcpy(pm_map, pm_snapshot, sizeof(pm_snapshot));
    memory_affinity_ranges[1].end = (2ULL << highest_map_bit) + 1; // Crosses its canonical node boundary.
    CHECK(!smp_numa_transform_valid());
    CHECK(!vmem_prepare_execution_contexts(2));
    memcpy(memory_affinity_ranges, affinity_snapshot, sizeof(affinity_snapshot));
    pm_map[1].start = pm_map[0].start; // Overlapping canonical physical map.
    CHECK(!smp_numa_transform_valid());
}

static void mapping_is_injective_and_preserves_indexes(void)
{
    const unsigned int widths[] = {12, 13, 20, 30, 33, 43, 44};
    for (unsigned int width_index = 0; width_index < sizeof(widths) / sizeof(widths[0]); width_index++) {
        unsigned int width = widths[width_index];
        uint64_t local_limit = 1ULL << width;
        reset_maps();
        uint64_t originals[16];
        for (int node = 0; node < 16; node++) {
            // Including the final page forces the selected width exactly.
            uint64_t local = (node % 2 == 0) ? local_limit - PAGE_SIZE : 0;
            originals[node] = NODE(node) + local;
            add_pm(originals[node], originals[node] + PAGE_SIZE);
        }
        // Reverse memory encounter order and permute unrelated domain IDs.
        for (int node = 15; node >= 0; node--) {
            add_affinity((node * 7) % 16, originals[node], originals[node] + PAGE_SIZE);
        }
        check_if_needs_to_map();
        CHECK(map_numa_memory_range && highest_map_bit == (width < 31 ? 31 : width));
        CHECK(pm_map[1].start >= VM_PINNED_SIZE);
        CHECK(sort_and_validate_memory_affinity_ranges());
        CHECK(smp_numa_transform_valid());
        uintptr_t decoded[16];
        for (int node = 0; node < 16; node++) {
            // Index node is also the heap's saved segment identity: no sorting
            // of pm_map may exchange it with another physical segment.
            check_segment(node, originals[node], originals[node] + PAGE_SIZE, (node * 7) % 16);
            decoded[node] = (uintptr_t)first_word_mapping(pm_map[node].start, 0);
            for (int previous = 0; previous < node; previous++) CHECK(decoded[previous] != decoded[node]);
            if (node > 0) CHECK(pm_map[node - 1].end <= pm_map[node].start);
        }
    }
}

int main(void)
{
    struct { const char *name; void (*run)(void); } cases[] = {
        {"reversed SRAT order", reversed_srat_order},
        {"largest domain zero", largest_domain_zero},
        {"physical map sizing", pm_map_participates_in_sizing},
        {"half-open node boundary", full_node_end_boundary},
        {"cross-node identity fallback", crossing_node_keeps_identity},
        {"atomic late failure", late_affinity_failure_is_atomic},
        {"wide-offset identity fallback", wider_local_address_keeps_identity},
        {"same-domain normalization", same_domain_overlaps_normalize},
        {"same-domain distinct node spans", same_domain_distinct_nodes_stay_separate},
        {"cross-domain overlap", cross_domain_overlap_keeps_identity},
        {"canonical post-validation", post_transform_validation},
        {"injectivity, round trips and heap indexes", mapping_is_injective_and_preserves_indexes},
    };
    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int before = failures;
        cases[i].run();
        printf("%s: %s\n", failures == before ? "PASS" : "FAIL", cases[i].name);
    }
    return failures != 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    smp = args.source_root.resolve() / 'system/smp.c'
    vmem = args.source_root.resolve() / 'system/loongarch/vmem.c'
    functions = ''.join(extract_function(smp, name, optional=True) for name in (
        # The first name permits running these regressions on the old source.
        'checkout_max_memory_bits_of_this_numa_node',
        'numa_transform_span', 'numa_canonical_span', 'numa_transform_ranges_valid',
        'map_the_numa_memory_range', 'numa_local_address_bits',
    ))
    functions += ''.join(extract_function(smp, name) for name in (
        'check_if_needs_to_map', 'smp_numa_transform_valid',
        'sort_and_validate_memory_affinity_ranges', 'smp_narrow_to_proximity_domain',
    ))
    functions += ''.join(extract_function(vmem, name) for name in (
        'vmem_prepare_execution_contexts', 'first_word_mapping', 'last_word_mapping', 'page_of',
    ))
    with tempfile.TemporaryDirectory(prefix='numa-topology-') as temp:
        source, binary = Path(temp) / 'harness.c', Path(temp) / 'harness'
        source.write_text(PREAMBLE + functions + HARNESS)
        command = shlex.split(args.cc) + ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror']
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined']
        subprocess.run(command + [str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
