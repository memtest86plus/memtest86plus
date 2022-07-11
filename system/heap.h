// SPDX-License-Identifier: GPL-2.0
#ifndef HEAP_H
#define HEAP_H
/**
 * \file
 *
 * Provides functions to allocate and free chunks of physical memory that will
 * be excluded from the memory tests. Two separate heaps are supported, one in
 * low (20-bit addressable) memory, the other in high (32-bit addressable)
 * memory.
 *
 *//*
 * Copyright (C) 2022 Martin Whitaker.
 */

#include <stddef.h>
#include <stdint.h>

/**
 * Initialises the heaps.
 */
void heap_init(void);

/**
 * Allocates a chunk of physical memory below 1MB. The allocated region will
 * be at least the requested size with the requested alignment. This memory
 * is always mapped to the identical address in virtual memory.
 *
 * \param size              - the requested size in bytes.
 * \param alignment         - the requested byte alignment (must be a power of 2).
 *
 * \returns
 * On success, the allocated address in physical memory. On failure, 0.
 */
uintptr_t lm_heap_alloc(size_t size, uintptr_t alignment);

/**
 * Returns a value indicating the current allocation state of the low-memory
 * heap. This value may be passed to lm_heap_rewind() to free any low memory
 * allocated after this call.
 *
 * \returns
 * An opaque value indicating the current allocation state.
 */
uintptr_t lm_heap_mark(void);

/**
 * Frees any low memory allocated since the specified mark was obtained from
 * a call to lm_heap_mark().
 *
 * \param mark              - the mark that indicates how much memory to free.
 */
void lm_heap_rewind(uintptr_t mark);

/**
 * Allocates a chunk of physical memory below 4GB. The allocated region will
 * be at least the requested size with the requested alignment. The caller is
 * responsible for mapping it into virtual memory if required.
 *
 * \param size              - the requested size in bytes.
 * \param alignment         - the requested byte alignment (must be a power of 2).
 *
 * \returns
 * On success, the allocated address in physical memory. On failure, 0.
 */
uintptr_t hm_heap_alloc(size_t size, uintptr_t alignment);

/**
 * Returns a value indicating the current allocation state of the high-memory
 * heap. This value may be passed to hm_heap_rewind() to free any high memory
 * allocated after this call.
 *
 * \returns
 * An opaque value indicating the current allocation state.
 */
uintptr_t hm_heap_mark(void);

/**
 * Frees any high memory allocated since the specified mark was obtained from
 * a call to hm_heap_mark().
 *
 * \param mark              - the mark that indicates how much memory to free.
 */
void hm_heap_rewind(uintptr_t mark);

#endif // HEAP_H
