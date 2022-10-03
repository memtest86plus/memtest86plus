// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Martin Whitaker.
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

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAX_PATTERNS 10

// DEFAULT_MASK covers a uintptr_t, since that is the testing granularity.
#ifdef __x86_64__
#define DEFAULT_MASK (UINTPTR_MAX << 3)
#else
#define DEFAULT_MASK (UINTPTR_MAX << 2)
#endif

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef struct {
    uintptr_t   addr;
    uintptr_t   mask;
} pattern_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static pattern_t    patterns[MAX_PATTERNS];
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
static void combine(uintptr_t addr1, uintptr_t mask1, uintptr_t addr2, uintptr_t mask2, uintptr_t *addr, uintptr_t *mask)
{
    *mask = COMBINE_MASK(addr1, mask1, addr2, mask2);

    *addr  = addr1 | addr2;
    *addr &= *mask;    // Normalise, no fundamental need for this
}

/*
 * Count the number of addresses covered with a mask.
 */
static uintptr_t addresses(uintptr_t mask)
{
    uintptr_t ctr = 1;
    int i = 8*sizeof(uintptr_t);
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
static uintptr_t combi_cost(uintptr_t addr1, uintptr_t mask1, uintptr_t addr2, uintptr_t mask2)
{
    uintptr_t cost1 = addresses(mask1);
    uintptr_t tmp, mask;
    combine(addr1, mask1, addr2, mask2, &tmp, &mask);
    return addresses(mask) - cost1;
}

/*
 * Find the cheapest array index to extend with the given addr/mask pair.
 * Return -1 if nothing below the given minimum cost can be found.
 */
static int cheap_index(uintptr_t addr1, uintptr_t mask1, uintptr_t min_cost)
{
    int i = num_patterns;
    int idx = -1;
    while (i-- > 0) {
        uintptr_t tmp_cost = combi_cost(patterns[i].addr, patterns[i].mask, addr1, mask1);
        if (tmp_cost < min_cost) {
            min_cost = tmp_cost;
            idx = i;
        }
    }
    return idx;
}

/*
 * Try to find a relocation index for idx if it costs nothing.
 * Return -1 if no such index exists.
 */
static int relocate_index(int idx)
{
    uintptr_t addr = patterns[idx].addr;
    uintptr_t mask = patterns[idx].mask;
    patterns[idx].addr = ~patterns[idx].addr;    // Never select idx
    int new = cheap_index(addr, mask, 1 + addresses(mask));
    patterns[idx].addr = addr;
    return new;
}

/*
 * Relocate the given index idx only if free of charge.
 * This is useful to combine to `neighbouring' sections to integrate.
 * Inspired on the Buddy memalloc principle in the Linux kernel.
 */
static void relocate_if_free(int idx)
{
    int newidx = relocate_index(idx);
    if (newidx >= 0) {
        uintptr_t caddr, cmask;
        combine(patterns[newidx].addr, patterns[newidx].mask,
                patterns[   idx].addr, patterns[   idx].mask,
                &caddr, &cmask);
        patterns[newidx].addr = caddr;
        patterns[newidx].mask = cmask;
        if (idx < --num_patterns) {
            patterns[idx].addr = patterns[num_patterns].addr;
            patterns[idx].mask = patterns[num_patterns].mask;
        }
        relocate_if_free (newidx);
    }
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void badram_init(void)
{
    num_patterns = 0;
}

bool badram_insert(uintptr_t addr)
{
    if (cheap_index(addr, DEFAULT_MASK, 1) != -1) {
        return false;
    }

    if (num_patterns < MAX_PATTERNS) {
        patterns[num_patterns].addr = addr;
        patterns[num_patterns].mask = DEFAULT_MASK;
        num_patterns++;
        relocate_if_free(num_patterns - 1);
    } else {
        int idx = cheap_index(addr, DEFAULT_MASK, UINTPTR_MAX);
        uintptr_t caddr, cmask;
        combine(patterns[idx].addr, patterns[idx].mask, addr, DEFAULT_MASK, &caddr, &cmask);
        patterns[idx].addr = caddr;
        patterns[idx].mask = cmask;
        relocate_if_free(idx);
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
    display_pinned_message(0, 0, "BadRAM Patterns");
    display_pinned_message(1, 0, "---------------");
    scroll();
    display_scrolled_message(0, "badram=");
    int col = 7;
    for (int i = 0; i < num_patterns; i++) {
        if (i > 0) {
            display_scrolled_message(col, ",");
            col++;
        }
        int text_width = 2 * (TESTWORD_DIGITS + 2) + 1;
        if (col > (SCREEN_WIDTH - text_width)) {
            scroll();
            col = 7;
        }
        display_scrolled_message(col, "0x%0*x,0x%0*x",
                                 TESTWORD_DIGITS, patterns[i].addr,
                                 TESTWORD_DIGITS, patterns[i].mask);
        col += text_width;
    }
}
