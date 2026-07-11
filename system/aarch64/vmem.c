// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2026 Sam Demeulemeester.
//
// ARM64 (AArch64) virtual memory management.
//
// We build our own identity-mapped page tables covering all the memory
// regions described by the BIOS/EFI memory map, so all mapping functions
// simply return the physical address unchanged. RAM is mapped as Normal
// write-back cacheable memory, the frame buffer as Normal non-cacheable
// memory, and anything else mapped on demand (MMIO) as Device-nGnRnE.
//
// The tables use the 4KB granule with a 48-bit input address range and
// are built from 1GB and 2MB block descriptors only. paging_init() is
// called from the startup code, both on first boot and after the program
// has relocated itself, in the latter case while still running on the
// previous copy's page tables.

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"
#include "bootparams.h"

#include "string.h"

#include "vmem.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define BLOCK_2M            (UINT64_C(1) << 21)
#define BLOCK_1G            (UINT64_C(1) << 30)

#define NUM_L1_TABLES       8       // each covers 512GB
#define NUM_L2_TABLES       128     // each covers 1GB

// Descriptor types

#define PTE_TYPE_BLOCK      UINT64_C(0x1)
#define PTE_TYPE_TABLE      UINT64_C(0x3)
#define PTE_VALID           UINT64_C(0x1)

#define PTE_ADDR_MASK       UINT64_C(0x0000FFFFFFFFF000)

// Lower and upper block attributes. The MAIR indices must match the
// MAIR_EL1 value set in boot/aarch64/startup.S.

#define PTE_ATTRINDX(idx)   ((uint64_t)(idx) << 2)
#define PTE_SH_INNER        (UINT64_C(3) << 8)
#define PTE_AF              (UINT64_C(1) << 10)
#define PTE_PXN             (UINT64_C(1) << 53)
#define PTE_UXN             (UINT64_C(1) << 54)

#define PTE_ATTR_NORMAL     (PTE_ATTRINDX(0) | PTE_SH_INNER | PTE_AF | PTE_UXN)
#define PTE_ATTR_DEVICE     (PTE_ATTRINDX(1) | PTE_AF | PTE_PXN | PTE_UXN)
#define PTE_ATTR_NORMAL_NC  (PTE_ATTRINDX(2) | PTE_SH_INNER | PTE_AF | PTE_PXN | PTE_UXN)

#define MAX_DEVICE_REGIONS  64

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef struct {
    uint64_t    start;
    uint64_t    end;
} device_region_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

// The level 1 and level 2 table pools. Tables are allocated on demand.

static uint64_t l1_tables[NUM_L1_TABLES][512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t l2_tables[NUM_L2_TABLES][512] __attribute__((aligned(PAGE_SIZE)));

static int num_l1_used = 0;
static int num_l2_used = 0;

// The device (MMIO) regions mapped so far. The page tables are rebuilt from
// scratch each time the program relocates itself, so we must remember these
// regions in order to replay the mappings.

static device_region_t device_regions[MAX_DEVICE_REGIONS];

static int num_device_regions = 0;

// A snapshot of the information we need from the boot params. The boot
// params live in memory that will be overwritten once testing starts, but
// the page tables are rebuilt on every relocation, so we must keep our own
// copy. These variables are part of the program image, and so are preserved
// by relocation.

static bool boot_info_saved = false;

static e820_entry_t e820_copy[E820_MAP_SIZE];
static int e820_copy_entries = 0;

static uint64_t lfb_start = 0;
static uint64_t lfb_end = 0;

static uint64_t cmd_line_start = 0;
static uint64_t cmd_line_end = 0;

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

// The level 0 (top) translation table. Loaded into TTBR0_EL1 by at startup

uint64_t ttbr0_table[512] __attribute__((aligned(PAGE_SIZE)));

bool paging_incomplete = false;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static uint64_t *alloc_l1_table(void)
{
    if (num_l1_used >= NUM_L1_TABLES) {
        return NULL;
    }
    uint64_t *table = l1_tables[num_l1_used++];
    memset(table, 0, PAGE_SIZE);
    return table;
}

static uint64_t *alloc_l2_table(void)
{
    if (num_l2_used >= NUM_L2_TABLES) {
        return NULL;
    }
    uint64_t *table = l2_tables[num_l2_used++];
    memset(table, 0, PAGE_SIZE);
    return table;
}

// Returns the table pointed to by a table descriptor, creating a new table
// if the descriptor is empty. Returns NULL if the descriptor is a block
// (the range is already mapped at a coarser granularity) or if the table
// pool is exhausted.

static uint64_t *get_l1_table(uint64_t *entry)
{
    if (*entry & PTE_VALID) {
        return (uint64_t *)(uintptr_t)(*entry & PTE_ADDR_MASK);
    }
    uint64_t *table = alloc_l1_table();
    if (table != NULL) {
        *entry = (uintptr_t)table | PTE_TYPE_TABLE;
    }
    return table;
}

static uint64_t *get_l2_table(uint64_t *entry)
{
    if (*entry & PTE_VALID) {
        if ((*entry & PTE_TYPE_TABLE) != PTE_TYPE_TABLE) {
            return NULL;    // already mapped by a 1GB block
        }
        return (uint64_t *)(uintptr_t)(*entry & PTE_ADDR_MASK);
    }
    uint64_t *table = alloc_l2_table();
    if (table != NULL) {
        *entry = (uintptr_t)table | PTE_TYPE_TABLE;
    }
    return table;
}

// Identity-maps the given physical address range with the given attributes,
// rounding it out to 2MB boundaries. Existing mappings are left untouched,
// so the first mapping of a region wins. Returns false if we ran out of
// translation tables.

static bool map_range(uint64_t start, uint64_t end, uint64_t attrs)
{
    // Reject inverted ranges and anything beyond the 48-bit input address
    // range, before the rounding below can wrap for an end near 2^64.
    if (start > end || end > (UINT64_C(1) << 48)) {
        return false;
    }

    start = start & ~(BLOCK_2M - 1);
    end   = (end + BLOCK_2M - 1) & ~(BLOCK_2M - 1);

    uint64_t addr = start;
    while (addr < end) {
        uint64_t *l1_table = get_l1_table(&ttbr0_table[(addr >> 39) & 511]);
        if (l1_table == NULL) {
            return false;
        }
        uint64_t *l1_entry = &l1_table[(addr >> 30) & 511];
        if ((addr & (BLOCK_1G - 1)) == 0 && (end - addr) >= BLOCK_1G && !(*l1_entry & PTE_VALID)) {
            *l1_entry = addr | attrs | PTE_TYPE_BLOCK;
            addr += BLOCK_1G;
            continue;
        }
        if ((*l1_entry & PTE_VALID) && (*l1_entry & PTE_TYPE_TABLE) != PTE_TYPE_TABLE) {
            // Already mapped by a 1GB block.
            addr += BLOCK_2M;
            continue;
        }
        uint64_t *l2_table = get_l2_table(l1_entry);
        if (l2_table == NULL) {
            return false;
        }
        uint64_t *l2_entry = &l2_table[(addr >> 21) & 511];
        if (!(*l2_entry & PTE_VALID)) {
            *l2_entry = addr | attrs | PTE_TYPE_BLOCK;
        }
        addr += BLOCK_2M;
    }

    // Make sure the table updates are visible to the table walker.
    __asm__ __volatile__ ("dsb ishst" ::: "memory");

    return true;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

// Builds the identity-mapped page tables from the boot params. Called from
// the startup code with the MMU still using either the firmware's page
// tables (first boot) or the previous program copy's page tables (after reloc)

void paging_init(void)
{
    // The first time we are called (from the boot code, before any memory
    // has been touched), take a snapshot of the boot information we need.
    // The boot params themselves live in memory that gets overwritten once
    // testing starts, so they cannot be relied upon after that.
    if (!boot_info_saved) {
        const boot_params_t *boot_params = (const boot_params_t *)boot_params_addr;

        const screen_info_t *si = &boot_params->screen_info;
        if (si->orig_video_isVGA == VIDEO_TYPE_EFI) {
            lfb_start = si->lfb_base;
            if (LFB_CAPABILITY_64BIT_BASE & si->capabilities) {
                lfb_start |= (uint64_t)si->ext_lfb_base << 32;
            }
            lfb_end = lfb_start + (uint64_t)si->lfb_linelength * si->lfb_height;
        }

        e820_copy_entries = boot_params->e820_entries;
        if (e820_copy_entries > E820_MAP_SIZE) {
            e820_copy_entries = E820_MAP_SIZE;
        }
        memcpy(e820_copy, boot_params->e820_map, e820_copy_entries * sizeof(e820_entry_t));

        if (boot_params->cmd_line_ptr != 0) {
            cmd_line_start = boot_params->cmd_line_ptr;
            cmd_line_end   = cmd_line_start + boot_params->cmd_line_size;
        }

        boot_info_saved = true;
    }

    num_l1_used = 0;
    num_l2_used = 0;
    memset(ttbr0_table, 0, sizeof(ttbr0_table));

    // Map the frame buffer first, as Normal non-cacheable memory. On some
    // systems it is allocated from RAM, and we must make sure the display
    // controller sees our writes.
    if (lfb_end != 0) {
        paging_incomplete |= !map_range(lfb_start, lfb_end, PTE_ATTR_NORMAL_NC);
    }

    // Map all the memory regions described by the BIOS memory map as Normal
    // write-back cacheable memory.
    for (int i = 0; i < e820_copy_entries; i++) {
        const e820_entry_t *entry = &e820_copy[i];
        if (entry->type == E820_RAM || entry->type == E820_ACPI) {
            paging_incomplete |= !map_range(entry->addr, entry->addr + entry->size, PTE_ATTR_NORMAL);
        }
    }

    // The boot command line may live outside the mapped regions if the boot
    // loader put it somewhere unusual.
    if (cmd_line_start != 0) {
        paging_incomplete |= !map_range(cmd_line_start, cmd_line_end, PTE_ATTR_NORMAL);
    }

    // Replay the device mappings made via map_region.
    for (int i = 0; i < num_device_regions; i++) {
        paging_incomplete |= !map_range(device_regions[i].start, device_regions[i].end, PTE_ATTR_DEVICE);
    }
}

uintptr_t map_region(uintptr_t base_addr, size_t size, bool only_for_startup __attribute__((unused)))
{
    if (size == 0) {
        size = 1;
    }

    // Reject sizes that would wrap the address space.
    if (base_addr + size < base_addr) {
        return 0;
    }

    // Record the region (at block granularity) so the mapping can be
    // replayed when the page tables are rebuilt after relocation.
    uint64_t start = base_addr & ~(BLOCK_2M - 1);
    uint64_t end   = (base_addr + size + BLOCK_2M - 1) & ~(BLOCK_2M - 1);
    int i;
    for (i = 0; i < num_device_regions; i++) {
        if (start >= device_regions[i].start && end <= device_regions[i].end) {
            break;
        }
    }
    if (i == num_device_regions) {
        if (num_device_regions >= MAX_DEVICE_REGIONS) {
            return 0;
        }
        device_regions[num_device_regions].start = start;
        device_regions[num_device_regions].end   = end;
        num_device_regions++;
    }

    // Anything not already mapped is assumed to be MMIO and gets a Device
    // mapping. Regions mapped earlier (e.g. RAM, the frame buffer) keep
    // their original attributes.
    if (!map_range(base_addr, base_addr + size, PTE_ATTR_DEVICE)) {
        return 0;
    }

    __asm__ __volatile__ ("isb" ::: "memory");

    return base_addr;
}

bool map_window(uintptr_t start_page __attribute__((unused)))
{
    // All of physical memory is permanently identity mapped.
    return true;
}

void *first_word_mapping(uintptr_t page)
{
    return (void *)(page << PAGE_SHIFT);
}

void *last_word_mapping(uintptr_t page, size_t word_size)
{
    return (uint8_t *)first_word_mapping(page) + (PAGE_SIZE - word_size);
}

uintptr_t page_of(void *addr)
{
    return (uintptr_t)addr >> PAGE_SHIFT;
}
