// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from memtest86+ memsize.c
//
// memsize.c - MemTest-86  Version 3.3
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"
#include "bootparams.h"

#include "memsize.h"

#include "string.h"

#include "pmem.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// The reserved memory starting at 640KB.

#define RESERVED_MEM_START  0x0a0000
#define RESERVED_MEM_END    0x100000

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

pm_map_t    pm_map[MAX_MEM_SEGMENTS];

int         pm_map_size = 0;

size_t      num_pm_pages = 0;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

// Some PC BIOS e820 responses include overlapping entries.
// Here we create a new map with the overlaps removed.
static int sanitize_e820_map(e820_entry_t new_map[], const e820_entry_t orig_map[], int orig_entries)
{
    struct change_member {
        const e820_entry_t *entry;  // pointer to original bios entry
        bool                start;  // true = start addr, false = end addr
        uint64_t            addr;   // address for this change point
    };

    struct change_member  change_point_list[2*E820_MAP_SIZE];
    struct change_member *change_point[2*E820_MAP_SIZE];
    struct change_member *change_tmp;

    const e820_entry_t   *overlap_list[E820_MAP_SIZE];

    /*
        Visually we're performing the following (1,2,3,4 = memory types)...
        Sample memory map (w/overlaps):
           ____22__________________
           ______________________4_
           ____1111________________
           _44_____________________
           11111111________________
           ____________________33__
           ___________44___________
           __________33333_________
           ______________22________
           ___________________2222_
           _________111111111______
           _____________________11_
           _________________4______

        Sanitized equivalent (no overlap):
           1_______________________
           _44_____________________
           ___1____________________
           ____22__________________
           ______11________________
           _________1______________
           __________3_____________
           ___________44___________
           _____________33_________
           _______________2________
           ________________1_______
           _________________4______
           ___________________2____
           ____________________33__
           ______________________4_
    */

    // Bail out if we find any unreasonable addresses in the original map.
    for (int i = 0; i < orig_entries; i++) {
        if (orig_map[i].addr + orig_map[i].size < orig_map[i].addr) {
            return 0;
        }
    }

    // Create pointers for initial change-point information (for sorting).
    for (int i = 0; i < 2*orig_entries; i++) {
        change_point[i] = &change_point_list[i];
    }

    // Record all known change-points (starting and ending addresses).
    int chg_idx = 0;
    for (int i = 0; i < orig_entries; i++) {
        change_point[chg_idx]->addr  = orig_map[i].addr;
        change_point[chg_idx]->start = true;
        change_point[chg_idx]->entry = &orig_map[i];
        chg_idx++;

        change_point[chg_idx]->addr  = orig_map[i].addr + orig_map[i].size;
        change_point[chg_idx]->start = false;
        change_point[chg_idx]->entry = &orig_map[i];
        chg_idx++;
    }

    // Sort change-point list by memory addresses (low -> high).
    bool still_changing = true;
    while (still_changing) {
        still_changing = false;
        for (int i = 1; i < 2*orig_entries; i++) {
            // If current_addr > last_addr or if current_addr = last_addr
            // and current is a start addr and last is an end addr, swap.
            if ((change_point[i]->addr <  change_point[i-1]->addr)
            || ((change_point[i]->addr == change_point[i-1]->addr) && change_point[i]->start && !change_point[i-1]->start)) {
                change_tmp        = change_point[i];
                change_point[i]   = change_point[i-1];
                change_point[i-1] = change_tmp;
                still_changing = true;
            }
        }
    }

    // Create a new bios memory map, removing overlaps.

    int overlap_entries = 0;
    int new_map_entries = 0;

    uint64_t last_addr = 0;
    uint32_t last_type = E820_NONE;

    // Loop through change-points, determining effect on the new map.
    for (chg_idx = 0; chg_idx < 2*orig_entries; chg_idx++) {
        // Keep track of all overlapping entries.
        if (change_point[chg_idx]->start) {
            // Add map entry to overlap list (> 1 entry implies an overlap)
            overlap_list[overlap_entries++] = change_point[chg_idx]->entry;
        } else {
            // Remove entry from list (order independent, so swap with last)
            for (int i = 0; i < overlap_entries; i++) {
                if (overlap_list[i] == change_point[chg_idx]->entry) {
                    overlap_list[i] = overlap_list[overlap_entries-1];
                }
            }
            overlap_entries--;
        }
        // If there are overlapping entries, decide which "type" to use
        // (larger value takes precedence -- 1=usable, 2,3,4,4+=unusable)
        uint32_t current_type = E820_NONE;
        for (int i = 0; i < overlap_entries; i++) {
            if (overlap_list[i]->type > current_type) {
                current_type = overlap_list[i]->type;
            }
        }
        // Continue building up new map based on this information.
        if (current_type != last_type) {
            if (last_type != E820_NONE) {
                new_map[new_map_entries].size = change_point[chg_idx]->addr - last_addr;
                // Move forward only if the new size was non-zero
                if (new_map[new_map_entries].size != 0) {
                    if (++new_map_entries >= E820_MAP_SIZE) {
                        break;
                    }
                }
            }
            if (current_type != E820_NONE) {
                new_map[new_map_entries].addr = change_point[chg_idx]->addr;
                new_map[new_map_entries].type = current_type;
                last_addr = change_point[chg_idx]->addr;
            }
            last_type = current_type;
        }
    }

    return new_map_entries;
}

static void init_pm_map(const e820_entry_t e820_map[], int e820_entries)
{
    pm_map_size = 0;
    for (int i = 0; i < e820_entries; i++) {
        if (e820_map[i].type == E820_RAM || e820_map[i].type == E820_ACPI) {
            uint64_t start = e820_map[i].addr;
            uint64_t end   = start + e820_map[i].size;

            // Don't ever use memory between 640KB and 1024KB
            if (start > RESERVED_MEM_START && start < RESERVED_MEM_END) {
                if (end < RESERVED_MEM_END) {
                    continue;
                }
                start = RESERVED_MEM_END;
            }
            if (end > RESERVED_MEM_START && end < RESERVED_MEM_END) {
                end = RESERVED_MEM_START;
            }

            pm_map[pm_map_size].start = (start + PAGE_SIZE - 1) >> PAGE_SHIFT;
            pm_map[pm_map_size].end   = end >> PAGE_SHIFT;
            num_pm_pages += pm_map[pm_map_size].end - pm_map[pm_map_size].start;
            if ((pm_map_size > 0) && (pm_map[pm_map_size].start == pm_map[pm_map_size - 1].end)) {
                pm_map[pm_map_size - 1].end = pm_map[pm_map_size].end;
            } else {
                pm_map_size++;
            }
        }
    }
}

static void sort_pm_map(void)
{
    // Do an insertion sort on the pm_map. On an already sorted list this should be a O(n) algorithm.
    for (int i = 0; i < pm_map_size; i++) {
        // Find where to insert the current element.
        int j = i - 1;
        while (j >= 0) {
            if (pm_map[i].start > pm_map[j].start) {
                j++;
                break;
            }
            j--;
        }
        // Insert the current element.
        if (i != j) {
            pm_map_t temp;
            temp = pm_map[i];
            memmove(&pm_map[j], &pm_map[j+1], (i - j) * sizeof(temp));
            pm_map[j] = temp;
        }
    }
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void pmem_init(void)
{
    e820_entry_t sanitized_map[E820_MAP_SIZE];

    num_pm_pages = 0;

    const boot_params_t *boot_params = (boot_params_t *)boot_params_addr;

    int sanitized_entries = sanitize_e820_map(sanitized_map, boot_params->e820_map, boot_params->e820_entries);

    init_pm_map(sanitized_map, sanitized_entries);
    sort_pm_map();
}
