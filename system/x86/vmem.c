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
#include "assert.h"

#include "cpuid.h"
#include "spinlock.h"

#include "vmem.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// The startup code builds a single set of paging tables, shared by every CPU,
// giving 4GB of virtual address space with 2MB pages, initially identity
// mapped to the first 4GB of physical memory. We use the third GB to map the
// physical memory window we are currently testing, and the following 512MB to
// map the screen frame buffer, ACPI tables, and any hardware devices we need
// to access that are not in the permanently mapped regions. Every execution
// context beyond the first tests through its own 1GB window, located at 4GB
// and above, mapped by a C-code-inserted PDP entry pointing at a private
// Page Directory Table.

#define MAX_REGION_PAGES    256     // VM pages

#define VM_WINDOW_START     SIZE_C(2,GB)
#define VM_REGION_START     (VM_WINDOW_START + SIZE_C(1,GB))
#define VM_REGION_END       (VM_REGION_START + MAX_REGION_PAGES * VM_PAGE_SIZE - 1)
#define VM_SPACE_END        0xffffffff

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

bool paging_incomplete = false;     // never set on this architecture

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static unsigned int device_pages_used = 0;

static int      num_contexts = 1;

static uintptr_t    mapped_window[VMEM_MAX_CONTEXTS] = { 2 };

// Serialises PD2 reconstruction within a context. Initialized before AP
// startup: after relocation the copied BSS is not re-zeroed, so an explicit
// init is required.
static spinlock_t   window_mutex[VMEM_MAX_CONTEXTS] = { 0 };

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static void load_pdbr(void)
{
#ifdef __x86_64__
    void *page_table = pml4;

    __asm__ __volatile__(
        "movq %0, %%cr3\n\t"
        :
        : "r" (page_table)
        : "rax", "memory"
    );
#else
    // Preserve the current i586 PAE/non-long-mode CR3 path exactly.
    void *page_table;
    if (cpuid_info.flags.lm == 1) {
        page_table = pml4;
    } else {
        page_table = pdp;
    }

    __asm__ __volatile__(
        "movl %0, %%cr3\n\t"
        :
        : "r" (page_table)
        : "rax", "memory"
    );
#endif
}

// Builds the shared x86-64 paging root: the PML4 and PDP base entries, plus
// one PDP entry per additional test window slot, each pointing at a private
// Page Directory Table. The slots' mapped_window[] bookkeeping is not touched
// here: the initial pool content (physical 2GB) matches the initial
// bookkeeping, and later map_window() calls rebuild only the slots in use.
#ifdef __x86_64__
static void init_page_tables(void)
{
    pml4[0] = (uintptr_t)pdp + 0x3;
    pdp[0] = (uintptr_t)pd0 + 0x3;
    pdp[1] = (uintptr_t)pd1 + 0x3;
    pdp[2] = (uintptr_t)win_pd + 0x3;
    pdp[3] = (uintptr_t)pd3 + 0x3;
    // Test window slots 1..n: context k maps its windows at 4GB and above.
    // Unused entries stay zeroed, so an accidental access faults instead of
    // silently aliasing another context's window.
    for (int context_id = 1; context_id < num_contexts; context_id++) {
        pdp[context_id + 3] = (uintptr_t)(win_pd + context_id * 512) + 0x3;
    }
}
#endif

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

bool vmem_prepare_execution_contexts(int requested_contexts)
{
#ifndef __x86_64__
    if (requested_contexts != 1) {
        return false;
    }
    num_contexts = 1;
    load_pdbr();
    return true;
#else
    if (requested_contexts < 1 || requested_contexts > VMEM_MAX_CONTEXTS) {
        num_contexts = 1;
        return false;
    }

    num_contexts = requested_contexts;
    init_page_tables();

    // First boot: every test window still carries the initial physical 2GB
    // mappings from the image, so the bookkeeping must agree with them.
    for (int context_id = 0; context_id < num_contexts; context_id++) {
        mapped_window[context_id] = 2;
    }

    // Every CPU shares this paging root; the startup code loads it and
    // each CPU reloads it again after every relocation.
    load_pdbr();
    return true;
#endif
}

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

bool map_window(int context_id, uintptr_t start_page)
{
    uintptr_t window = start_page >> (30 - PAGE_SHIFT);

    if (window < 2) {
        // The region is below the test window, so it is already mapped.
#ifdef __x86_64__
        // The paging root is shared, but reload it anyway to flush the
        // calling CPU's stale translations.
        load_pdbr();
#endif
        return true;
    }
    if (cpuid_info.flags.pae == 0) {
        // No PAE, so we can only access 4GB.
        if (window < 4) {
            mapped_window[context_id] = window;
            return true;
        }
        return false;
    }
    if (cpuid_info.flags.lm == 0 && (start_page >= PAGE_C(64,GB))) {
         // Fail, we want an address that is out of bounds
         // for PAE and no long mode (ie. 32 bit CPU).
        return false;
    }

    spin_lock(&window_mutex[context_id]);
    if (mapped_window[context_id] != window) {
        // Compute the page table entries for this context's test window.
        uint64_t *context_win_pd = win_pd + context_id * 512;
        for (int i = 0; i < 512; i++) {
            context_win_pd[i] = ((uint64_t)window << 30) + (i << VM_PAGE_SHIFT) + 0x83;
        }
        mapped_window[context_id] = window;
    }
    // Every CPU must reload its own PDBR to flush any remnants of the old
    // mapping, even when another CPU rebuilt the window's table.
    load_pdbr();
    spin_unlock(&window_mutex[context_id]);
    return true;
}

void *first_word_mapping(uintptr_t page, int context_id)
{
    void *result;
    if (page < PAGE_C(2,GB)) {
        // If the address is less than 2GB, it is directly mapped.
        result = (void *)(page << PAGE_SHIFT);
    } else {
        // Otherwise it is mapped into the context's test window.
        uintptr_t alias = (vmem_window_base(context_id) >> PAGE_SHIFT) + page % PAGE_C(1,GB);
        result = (void *)(alias << PAGE_SHIFT);
    }
    return result;
}

void *last_word_mapping(uintptr_t page, size_t word_size, int context_id)
{
    return (uint8_t *)first_word_mapping(page, context_id) + (PAGE_SIZE - word_size);
}

uintptr_t page_of(void *addr, int context_id)
{
    uintptr_t page = (uintptr_t)addr >> PAGE_SHIFT;
    uintptr_t window_base = vmem_window_base(context_id) >> PAGE_SHIFT;
    if (page >= window_base) {
        page = page % PAGE_C(1,GB);
        page += mapped_window[context_id] << (30 - PAGE_SHIFT);
    }
    return page;
}
