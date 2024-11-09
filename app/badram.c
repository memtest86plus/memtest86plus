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
#include "memsize.h"

#include "config.h"

#include "badram.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAX_PATTERNS 10
#define PATTERNS_SIZE (MAX_PATTERNS + 1)

// DEFAULT_MASK covers a uintptr_t, since that is the testing granularity.
#if (ARCH_BITS == 64)
#define DEFAULT_MASK (UINT64_MAX << 3)
#else
#define DEFAULT_MASK (UINT64_MAX << 2)
#endif

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef struct {
    uint64_t   addr;    // used as the lower address in memmap or pages mode
    uint64_t   mask;    // used as the upper address in memmap or pages mode
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
    if (error_mode == ERROR_MODE_BADRAM) {
        *mask = COMBINE_MASK(addr1, mask1, addr2, mask2);

        *addr  = addr1 | addr2;
        *addr &= *mask;    // Normalise to ensure sorting on .addr will work as intended
    } else {
        *addr = (addr1 < addr2) ? addr1 : addr2;  // the lower address
        *mask = (mask1 > mask2) ? mask1 : mask2;  // the upper address
    }
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
    uint64_t addr, mask;
    combine(addr1, mask1, addr2, mask2, &addr, &mask);
    if (error_mode == ERROR_MODE_BADRAM) {
        return addresses(mask) - addresses(mask1);
    } else {
        return (mask - addr) - (mask1 - addr1);
    }
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
    if (error_mode == ERROR_MODE_BADRAM) {
        // Normalise to ensure sorting on .addr will work as intended
        pattern.addr &= pattern.mask;
    }

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

static int num_digits(uint64_t value)
{
    int count = 0;

    do {
        value >>= 4;
        count++;
    } while (value != 0);

    return count;
}

static int display_hex_uint64(int col, uint64_t value)
{
#if (ARCH_BITS == 64)
    return display_scrolled_message(col, "0x%x", value);
#else
    if (value > 0xffffffffU) {
        return display_scrolled_message(col, "0x%x%08x", (uintptr_t)(value >> 32), (uintptr_t)(value & 0xFFFFFFFFU));
    } else {
        return display_scrolled_message(col, "0x%x", (uintptr_t)value);
    }
#endif
}

static int scroll_if_needed(int col, int text_width, int indent)
{
    if (col > (SCREEN_WIDTH - text_width)) {
        scroll();
        col = indent;
    }
    return col;
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
    pattern_t pattern;

    pattern.addr = ((uint64_t)page << PAGE_SHIFT) + offset;
    if (error_mode == ERROR_MODE_BADRAM) {
        pattern.mask = DEFAULT_MASK;
    } else {
        pattern.mask = pattern.addr;
    }

    // Test if covered by an existing entry or can be covered by adding one
    // testword address to an existing entry.
    for (int i = 0; i < num_patterns; i++) {
        uint64_t cost = combi_cost(patterns[i].addr, patterns[i].mask, pattern.addr, pattern.mask);
        if (cost == 0) {
            return false;
        }
        if (cost == sizeof(uintptr_t)) {
            combine(patterns[i].addr, patterns[i].mask, pattern.addr, pattern.mask,
                    &patterns[i].addr, &patterns[i].mask);
            return true;
        }
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

    int col = 0;
    switch (error_mode) {
      case ERROR_MODE_BADRAM:
        display_pinned_message(0, 0, "BadRAM Patterns (excludes test 0 and test 7)");
        display_pinned_message(1, 0, "--------------------------------------------");
        scroll();
        col = display_scrolled_message(col, "badram=");
        for (int i = 0; i < num_patterns; i++) {
            if (i > 0) {
                col = display_scrolled_message(col, ",");
            }
            col = scroll_if_needed(col, num_digits(patterns[i].addr) + num_digits(patterns[i].mask) + 5, 7);
            col = display_hex_uint64(col, patterns[i].addr);
            col = display_scrolled_message(col, ",");
            col = display_hex_uint64(col, patterns[i].mask);
        }
        break;
      case ERROR_MODE_MEMMAP:
        display_pinned_message(0, 0, "Linux memmap (excludes test 0 and test 7)");
        display_pinned_message(1, 0, "-----------------------------------------");
        scroll();
        col = display_scrolled_message(0, "memmap=");
        for (int i = 0; i < num_patterns; i++) {
            if (i > 0) {
                col = display_scrolled_message(col, ",");
            }
            uint64_t size = patterns[i].mask - patterns[i].addr + sizeof(uintptr_t);
            col = scroll_if_needed(col, num_digits(size) + num_digits(patterns[i].addr) + 5, 7);
            col = display_hex_uint64(col, size);
            col = display_scrolled_message(col, "$");
            col = display_hex_uint64(col, patterns[i].addr);
        }
        break;
      case ERROR_MODE_PAGES:
        display_pinned_message(0, 0, "Bad pages (excludes test 0 and test 7)");
        display_pinned_message(1, 0, "--------------------------------------");
        scroll();
        for (int i = 0; i < num_patterns; i++) {
            if (i > 0) {
                col = display_scrolled_message(col, ",");
            }
            uint64_t lower_page = patterns[i].addr >> PAGE_SHIFT;
            uint64_t upper_page = patterns[i].mask >> PAGE_SHIFT;
            col = scroll_if_needed(col, num_digits(lower_page) + (upper_page != lower_page ? num_digits(upper_page) + 6 : 2), 0);
            col = display_hex_uint64(col, lower_page);
            if (upper_page != lower_page) {
                col = display_scrolled_message(col, "..");
                col = display_hex_uint64(col, upper_page);
            }
        }
        break;
      default:
        break;
    }
}
