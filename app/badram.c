// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2024 Martin Whitaker.
//
// Derived from memtest86+ patn.c:
//
// MemTest86+ V1.60 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.x86-secret.com - http://www.memtest.org
// ----------------------------------------------------
// Pattern extension for memtest86
//
// Generates patterns for the Linux kernel's BadRAM extension that avoids
// allocation of faulty pages.
//
// Released under version 2 of the Gnu Public License.
//
// By Rick van Rein, vanrein@zonnet.nl
//
// What it does:
//  - Keep track of a number of BadRAM patterns in an array;
//  - Combine new faulty addresses with it whenever possible;
//  - Keep masks as selective as possible by minimising resulting faults;
//  - Print a new pattern only when the pattern array is changed.

#include <stdbool.h>
#include <stdint.h>

#include "display.h"

#include "badram.h"
#include "memsize.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAX_PATTERNS 10
#define PATTERNS_SIZE (MAX_PATTERNS + 1)

// DEFAULT_MASK covers a uintptr_t, since that is the testing granularity.
#ifdef __x86_64__
#define DEFAULT_MASK (UINT64_MAX << 3)
#else
#define DEFAULT_MASK (UINT64_MAX << 2)
#endif

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef struct {
    uint64_t   addr;
    uint64_t   mask;
} pattern_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static pattern_t    patterns[PATTERNS_SIZE];
static int          num_patterns = 0;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

// New mask is 1 where both masks were 1 (b & d) and the addresses were equal ~(a ^ c).
// If addresses were unequal the new mask must be 0 to allow for both values.
#define COMBINE_MASK(a,b,c,d) ((b & d) & ~(a ^ c))

/*
 * Combine two addr/mask pairs to one addr/mask pair.
 */
static void combine(uint64_t addr1, uint64_t mask1, uint64_t addr2, uint64_t mask2, uint64_t *addr, uint64_t *mask)
{
    *mask = COMBINE_MASK(addr1, mask1, addr2, mask2);

    *addr  = addr1 | addr2;
    *addr &= *mask;    // Normalise to ensure sorting on .addr will work as intended
}

/*
 * Count the number of addresses covered with a mask.
 */
static uint64_t addresses(uint64_t mask)
{
    uint64_t ctr = 1;
    int i = 8*sizeof(uint64_t);
    while (i-- > 0) {
        if (! (mask & 1)) {
            ctr += ctr;
        }
        mask >>= 1;
    }
    return ctr;
}

/*
 * Count how many more addresses would be covered by addr1/mask1 when combined
 * with addr2/mask2.
 */
static uint64_t combi_cost(uint64_t addr1, uint64_t mask1, uint64_t addr2, uint64_t mask2)
{
    uint64_t cost1 = addresses(mask1);
    uint64_t tmp, mask;
    combine(addr1, mask1, addr2, mask2, &tmp, &mask);
    return addresses(mask) - cost1;
}

/*
 * Determine if pattern is already covered by an existing pattern.
 * Return true if that's the case, else false.
 */
static bool is_covered(pattern_t pattern)
{
    for (int i = 0; i < num_patterns; i++) {
        if (combi_cost(patterns[i].addr, patterns[i].mask, pattern.addr, pattern.mask) == 0) {
            return true;
        }
    }
    return false;
}

/*
 * Find the pair of entries that would be the cheapest to merge.
 * Assumes patterns is sorted by .addr asc and that for each index i, the cheapest entry to merge with is at i-1 or i+1.
 * Return -1 if <= 1 patterns exist, else the index of the first entry of the pair (the other being that + 1).
 */
static int cheapest_pair()
{
    // This is guaranteed to be overwritten with >= 0 as long as num_patterns > 1
    int merge_idx = -1;

    uint64_t min_cost = UINT64_MAX;
    for (int i = 0; i < num_patterns - 1; i++) {
        uint64_t tmp_cost = combi_cost(
            patterns[i].addr,
            patterns[i].mask,
            patterns[i+1].addr,
            patterns[i+1].mask
        );
        if (tmp_cost <= min_cost) {
            min_cost = tmp_cost;
            merge_idx = i;
        }
    }
    return merge_idx;
}

/*
 * Remove entries at idx and idx+1.
 */
static void remove_pair(int idx)
{
    for (int i = idx; i < num_patterns - 2; i++) {
        patterns[i] = patterns[i + 2];
    }
    patterns[num_patterns - 1].addr = 0u;
    patterns[num_patterns - 1].mask = 0u;
    patterns[num_patterns - 2].addr = 0u;
    patterns[num_patterns - 2].mask = 0u;
    num_patterns -= 2;
}

/*
 * Get the combined entry of idx1 and idx2.
 */
static pattern_t combined_pattern(int idx1, int idx2)
{
    pattern_t combined;
    combine(
            patterns[idx1].addr,
            patterns[idx1].mask,
            patterns[idx2].addr,
            patterns[idx2].mask,
            &combined.addr,
            &combined.mask
    );
    return combined;
}

/*
 * Insert pattern at index idx, shuffling other entries on index towards the end.
 */
static void insert_at(pattern_t pattern, int idx)
{
    // Move all entries >= idx one index towards the end to make space for the new entry
    for (int i = num_patterns - 1; i >= idx; i--) {
        patterns[i + 1] = patterns[i];
    }

    patterns[idx] = pattern;
    num_patterns++;
}

/*
 * Insert entry (addr, mask) in patterns in an index i so that patterns[i-1].addr < patterns[i]
 * NOTE: Assumes patterns is already sorted by .addr asc!
 */
static void insert_sorted(pattern_t pattern)
{
    // Normalise to ensure sorting on .addr will work as intended
    pattern.addr &= pattern.mask;

    // Find index to insert entry into
    int new_idx = num_patterns;
    for (int i = 0; i < num_patterns; i++) {
        if (pattern.addr < patterns[i].addr) {
            new_idx = i;
            break;
        }
    }

    insert_at(pattern, new_idx);
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void badram_init(void)
{
    num_patterns = 0;

    for (int idx = 0; idx < PATTERNS_SIZE; idx++) {
        patterns[idx].addr = 0u;
        patterns[idx].mask = 0u;
    }
}

bool badram_insert(testword_t page, testword_t offset)
{
    pattern_t pattern = {
        .addr = ((uint64_t)page << PAGE_SHIFT) + offset,
        .mask = DEFAULT_MASK
    };

    // If covered by existing entry we return immediately
    if (is_covered(pattern)) {
        return false;
    }

    // Add entry in order sorted by .addr asc
    insert_sorted(pattern);

    // If we have more patterns than the max we need to force a merge
    if (num_patterns > MAX_PATTERNS) {
        // Find the pair that is the cheapest to merge
        // merge_idx will be -1 if num_patterns < 2, but that means MAX_PATTERNS = 0 which is not a valid state anyway
        int merge_idx = cheapest_pair();

        pattern_t combined = combined_pattern(merge_idx, merge_idx + 1);

        // Remove the source pair so that we can maintain order as combined does not necessarily belong in merge_idx
        remove_pair(merge_idx);

        insert_sorted(combined);
    }
    return true;
}

void badram_display(void)
{
    if (num_patterns == 0) {
        return;
    }

    check_input();

    clear_message_area();
    display_pinned_message(0, 0, "BadRAM Patterns (excludes test 0 and test 7)");
    display_pinned_message(1, 0, "--------------------------------------------");
    scroll();
    display_scrolled_message(0, "badram=");
    int col = 7;
    for (int i = 0; i < num_patterns; i++) {
        if (i > 0) {
            display_scrolled_message(col, ",");
            col++;
        }
        int text_width = 2 * (16 + 2) + 1;
        if (col > (SCREEN_WIDTH - text_width)) {
            scroll();
            col = 7;
        }
        display_scrolled_message(col, "0x%08x%08x,0x%08x%08x",
                                 (uintptr_t)(patterns[i].addr >> 32), (uintptr_t)(patterns[i].addr & 0xFFFFFFFFU),
                                 (uintptr_t)(patterns[i].mask >> 32), (uintptr_t)(patterns[i].mask & 0xFFFFFFFFU));
        col += text_width;
    }
}
