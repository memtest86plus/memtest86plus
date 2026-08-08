// SPDX-License-Identifier: GPL-2.0
#ifndef VMEM_H
#define VMEM_H
/**
 * \file
 *
 * Provides functions to handle physical memory page mapping into virtual
 * memory.
 *
 * The startup code sets up the paging tables to give us a 4GB virtual address
 * space, initially identity mapped to the first 4GB of physical memory. We
 * leave the lower 2GB permanently mapped, and use the upper 2GB for mapping
 * the remaining physical memory as required.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memsize.h"

/**
 * The size of the physical memory region (in pages) that is permanently mapped
 * into virtual memory, starting at virtual address 0.
 */
#define VM_PINNED_SIZE  PAGE_C(2,GB)

/**
 * The size of a physical memory region (in pages) that can be mapped into
 * virtual memory by a call to map_window().
 */
#define VM_WINDOW_SIZE  PAGE_C(1,GB)

/**
 * Set if any range could not be mapped when the page tables were built or
 * rebuilt (never cleared). Accessing an unmapped range will fault.
 */
extern bool paging_incomplete;

/**
 * Maps a physical memory region into the upper 2GB of virtual memory. The
 * virtual address will have the same alignment within a page as the physical
 * address.
 *
 * \param base_addr         - the physical byte address of the region.
 * \param size              - the region size in bytes.
 * \param only_for_startup  - if true, the region will remain mapped until the
 *                            first call to map_window(), otherwise it will be
 *                            permanently mapped.
 *
 * \returns
 * On success, the mapped address in virtual memory, On failure, 0.
 */
uintptr_t map_region(uintptr_t base_addr, size_t size, bool only_for_startup);

/**
 * Prepares the architecture's physical-memory mapping model for the given
 * number of execution contexts, which must be 1..VMEM_MAX_CONTEXTS.
 * On x86-64 this initializes every per-context PML4/PDP/PD2 root; on
 * AArch64 and LoongArch64 it validates the shared identity/direct mapping.
 * Returns false (leaving a single context usable) on failure or an
 * unsupported context count. Call before AP startup.
 */
bool vmem_prepare_execution_contexts(int num_contexts);

/**
 * Maps a \ref VM_WINDOW_SIZE region of physical memory into the upper 2GB of
 * virtual memory for the given execution context. The physical memory region
 * must be aligned on a \ref VM_WINDOW_SIZE boundary. The virtual address will
 * be similarly aligned. The region will remain mapped until the next call to
 * map_window() for the same context.
 *
 * \param context_id    - the execution context whose paging state is used.
 * \param start_page    - the physical page number of the region.
 *
 * \returns
 * On success, true. On failure, false.
 */
bool map_window(int context_id, uintptr_t start_page);

/**
 * Returns a virtual memory pointer to the first word of the specified physical
 * memory page. Physical memory pages above \ref VM_PINNED_SIZE must have been
 * mapped by a call to map_window() prior to calling this function.
 *
 * \param page              - the physical page number.
 *
 * \returns
 * A pointer to the first word of the page.
 */
void *first_word_mapping(uintptr_t page);

/**
 * Returns a virtual memory pointer to the last word of the specified physical
 * memory page. Physical memory pages above \ref VM_PINNED_SIZE must have been
 * mapped by a call to map_window() prior to calling this function.
 *
 * \param page              - the physical page number.
 * \param word_size         - the size of a word in bytes.
 *
 * \returns
 * A pointer to the last word of the page.
 */
void *last_word_mapping(uintptr_t page, size_t word_size);

/**
 * Returns the page number of the physical memory page containing the specified
 * virtual memory address, translated through the given execution context's
 * current window mapping. The specified address must either be permanently
 * mapped or mapped by a call to map_window() prior to calling this function.
 *
 * \param addr              - the virtual memory address.
 * \param context_id        - the execution context whose mapping is used.
 *
 * \returns
 * The corresponding physical page number.
 */
uintptr_t page_of(void *addr, int context_id);

#endif // VMEM_H
