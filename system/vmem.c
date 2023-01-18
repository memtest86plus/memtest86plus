// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from memtest86+ vmem.c
//
// vmem.c - MemTest-86
//
// Virtual memory handling (PAE)
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"

#include "cpuid.h"

#include "vmem.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// The startup code sets up the paging tables to give us 4GB of virtual address
// space, using 2MB pages, initially identity mapped to the first 4GB of physical
// memory. We use the third GB to map the physical memory window we are currently
// testing, and the following 512MB to map the screen frame buffer, ACPI tables,
// and any hardware devices we need to access that are not in the permanently
// mapped regions.

#define MAX_REGION_PAGES    256     // VM pages

#define VM_WINDOW_START     SIZE_C(2,GB)
#define VM_REGION_START     (VM_WINDOW_START + SIZE_C(1,GB))
#define VM_REGION_END       (VM_REGION_START + MAX_REGION_PAGES * VM_PAGE_SIZE - 1)
#define VM_SPACE_END        0xffffffff

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static unsigned int device_pages_used = 0;

static uintptr_t    mapped_window = 2;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static void load_pdbr()
{
    void *page_table;
    if (cpuid_info.flags.lm == 1) {
        page_table = pml4;
    } else {
        page_table = pdp;
    }

    __asm__ __volatile__(
#ifdef __x86_64__
        "movq %0, %%cr3\n\t"
#else
        "movl %0, %%cr3\n\t"
#endif
        :
        : "r" (page_table)
        : "rax"
    );
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

uintptr_t map_region(uintptr_t base_addr, size_t size, bool only_for_startup)
{
    uintptr_t last_addr = base_addr + size - 1;
    // Check if the requested region is permanently mapped. If it is only needed during startup,
    // this includes the region we will eventually use for the memory test window.
    if (last_addr < (only_for_startup ? VM_REGION_START : VM_WINDOW_START) || (base_addr > VM_REGION_END && last_addr <= VM_SPACE_END)) {
        return base_addr;
    }
    // Check if the requested region is already mapped.
    uintptr_t first_virt_page = 0;
    uintptr_t first_phys_page = base_addr >> VM_PAGE_SHIFT;
    uintptr_t last_phys_page  = last_addr >> VM_PAGE_SHIFT;
    uintptr_t curr_virt_page  = first_virt_page;
    uintptr_t curr_phys_page  = first_phys_page;
    while (curr_virt_page < device_pages_used && curr_phys_page <= last_phys_page) {
        uintptr_t mapped_phys_page = pd3[curr_virt_page++] >> VM_PAGE_SHIFT;
        if (mapped_phys_page == curr_phys_page) {
            curr_phys_page++;
        } else {
            first_virt_page = curr_virt_page;
            curr_phys_page = first_phys_page;
        }
    }
    // If not, map it. Note that this will extend a partial match at the end of the current map.
    while (curr_phys_page <= last_phys_page) {
        if (device_pages_used == MAX_REGION_PAGES) return 0;
        pd3[device_pages_used++] = (curr_phys_page++ << VM_PAGE_SHIFT) + 0x83;
    }
    // Reload the PDBR to flush any remnants of the old mapping.
    load_pdbr();
    // Return the mapped address.
    return VM_REGION_START + first_virt_page * VM_PAGE_SIZE + base_addr % VM_PAGE_SIZE;
}

bool map_window(uintptr_t start_page)
{
    uintptr_t window = start_page >> (30 - PAGE_SHIFT);

    if (window < 2) {
        // Less than 2 GB so no mapping is required.
        return true;
    }
    if (cpuid_info.flags.pae == 0) {
        // No PAE, so we can only access 4GB.
        if (window < 4) {
            mapped_window = window;
            return true;
        }
        return false;
    }
    if (cpuid_info.flags.lm == 0 && (start_page >= PAGE_C(64,GB))) {
        // Fail, we want an address that is out of bounds
        // for PAE and no long mode (ie. 32 bit CPU).
        return false;
    }
    // Compute the page table entries.
    uint64_t flags = cpuid_info.flags.nx ? UINT64_C(0x8000000000000083) : 0x83;
    for (uintptr_t i = 0; i < 512; i++) {
        pd2[i] = ((uint64_t)window << 30) + (i << VM_PAGE_SHIFT) + flags;
    }
    // Reload the PDBR to flush any remnants of the old mapping.
    load_pdbr();

    mapped_window = window;
    return true;
}

void *first_word_mapping(uintptr_t page)
{
    void *result;
    if (page < PAGE_C(2,GB)) {
        // If the address is less than 2GB, it is directly mapped.
        result = (void *)(page << PAGE_SHIFT);
    } else {
        // Otherwise it is mapped to the third GB.
        uintptr_t alias = PAGE_C(2,GB) + page % PAGE_C(1,GB);
        result = (void *)(alias << PAGE_SHIFT);
    }
    return result;
}

void *last_word_mapping(uintptr_t page, size_t word_size)
{
    return (uint8_t *)first_word_mapping(page) + (PAGE_SIZE - word_size);
}

uintptr_t page_of(void *addr)
{
    uintptr_t page = (uintptr_t)addr >> PAGE_SHIFT;
    if (page >= PAGE_C(2,GB)) {
        page = page % PAGE_C(1,GB);
        page += mapped_window << (30 - PAGE_SHIFT);
    }
    return page;
}
