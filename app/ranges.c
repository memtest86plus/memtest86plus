// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 David Koňařík.
#include "ranges.h"
#include "display.h"
#include "screen.h"
#include "test.h"
#include "assert.h"

// 20 visible + 1 reserved for insert process
#define MAX_ERROR_RANGES 21

static struct {
    uintptr_t start;
    uintptr_t end; // Exclusive
} ranges[MAX_ERROR_RANGES];
static bool range_usage_map[MAX_ERROR_RANGES];

void ranges_display_init(void)
{
    for (uint64_t i = 0; i < MAX_ERROR_RANGES; i++) {
        range_usage_map[i] = false;
    }
}

bool ranges_display_insert(uintptr_t addr)
{
    uintptr_t addr_end = addr + sizeof(testword_t);
    // First check if we don't already contain this address or if we can't
    // directly add it
    for (uint64_t i = 0; i < MAX_ERROR_RANGES; i++) {
        if (!range_usage_map[i]) {
            continue;
        } else if (ranges[i].start <= addr && ranges[i].end >= addr_end) {
            return false;
        } else if(ranges[i].end == addr) {
            ranges[i].end = addr_end;
            return true;
        } else if(ranges[i].start == addr_end) {
            ranges[i].start = addr;
            return true;
        }
    }
    
    uint64_t range_count = 0;
    int64_t new_range_idx = -1;

    // Find a free range slot for out temporary range
    for (uint64_t i = 0; i < MAX_ERROR_RANGES; i++) {
        if (range_usage_map[i]) {
            range_count++;
        } else {
            new_range_idx = i;
        }
    }
    assert(new_range_idx != -1);
    range_usage_map[new_range_idx] = true;
    ranges[new_range_idx].start = addr;
    ranges[new_range_idx].end = addr_end;
    range_count++;
    
    // If we can spare the range slot, keep it as-is
    // We always keep one free range slot for the next temp range
    if (range_count <= MAX_ERROR_RANGES - 1) {
        return true;
    }
    // Otherwise find the minimum-waste pair of ranges to merge
    uint64_t merge_1, merge_2; // The two ranges to merge
    uint64_t min_waste = UINT64_MAX;
    for (uint64_t i = 0; i < MAX_ERROR_RANGES; i++) {
        if (!range_usage_map[i]) {
            continue;
        }
        for (uint64_t j = i; j < MAX_ERROR_RANGES; j++) {
            if(!range_usage_map[j]) {
                continue;
            }
            uint64_t waste =
                ranges[i].end < ranges[j].start
                ? ranges[j].start - ranges[i].end
                : ranges[i].start - ranges[j].end;
            if(waste < min_waste) {
                merge_1 = i;
                merge_2 = j;
                min_waste = waste;
            }
        }
    }
    assert(min_waste != UINT64_MAX);
    // Merge the two ranges and free the second
    ranges[merge_1].end = ranges[merge_2].end;
    range_usage_map[merge_2] = false;

    return true;
}

void ranges_display()
{
    check_input();

    clear_message_area();
    display_pinned_message(0, 0, "Faulty memory ranges (start,length):");
    scroll();
    int col = 0;
    for (uint64_t i = 0; i < MAX_ERROR_RANGES; i++) {
        if (!range_usage_map[i]) {
            continue;
        }
        int text_width = 2 * (16 + 2) + 2;
        if (col > SCREEN_WIDTH - text_width) {
            scroll();
            col = 0;
        }
        display_scrolled_message(
            col, "0x%016x,0x%x",
            ranges[i].start, ranges[i].end - ranges[i].start);
        col += text_width;
    }
}
