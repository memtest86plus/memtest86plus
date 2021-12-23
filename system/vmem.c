// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2021 Martin Whitaker.
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
// Private Variables
//------------------------------------------------------------------------------

static int          device_pages_used = 0;

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

uintptr_t map_device(uintptr_t base_addr, size_t size)
{
    uintptr_t first_virt_page = device_pages_used;
    uintptr_t first_phys_page = base_addr >> VM_PAGE_SHIFT;
    uintptr_t last_phys_page  = (base_addr + size - 1) >> VM_PAGE_SHIFT;
    // Compute the page table entries.
    for (uintptr_t page = first_phys_page; page <= last_phys_page; page++) {
        if (device_pages_used == 512) return 0;
        pd3[device_pages_used++] = (page << VM_PAGE_SHIFT) + 0x83;
    }
    // Reload the PDBR to flush any remnants of the old mapping.
    load_pdbr();
    // Return the mapped address.
    return ADDR_C(3,GB) + first_virt_page * VM_PAGE_SIZE + base_addr % VM_PAGE_SIZE;
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
    for (uintptr_t i = 0; i < 512; i++) {
        pd2[i] = ((uint64_t)window << 30) + (i << VM_PAGE_SHIFT) + 0x83;
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
