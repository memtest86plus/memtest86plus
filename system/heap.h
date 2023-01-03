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

typedef enum {
    HEAP_TYPE_LM_1,
    HEAP_TYPE_HM_1,
    HEAP_TYPE_LAST
} heap_type_t;

/**
 * Initialises the heaps.
 */
void heap_init(void);

/**
 * Allocates a chunk of physical memory in the given heap. The allocated
 * region will be at least the requested size with the requested alignment.
 * This memory is always mapped to the identical address in virtual memory.
 *
 * \param heap_id      - the target heap.
 * \param size         - the requested size in bytes.
 * \param alignment    - the requested byte alignment (must be a power of 2).
 *
 * \returns
 * On success, the allocated address in physical memory. On failure, 0.
 */
uintptr_t heap_alloc(heap_type_t heap_id, size_t size, uintptr_t alignment);

/**
 * Returns a value indicating the current allocation state of the given
 * memory heap. This value may be passed to heap_rewind() to free any
 * memory from that heap allocated after this call.
 *
 * \param heap_id      - the target heap.
 *
 * \returns
 * An opaque value indicating the current allocation state.
 */
uintptr_t heap_mark(heap_type_t heap_id);

/**
 * Frees any memory allocated in the given heap since the specified mark was
 * obtained from a call to heap_mark().
 *
 * \param heap_id      - the target heap.
 * \param mark         - the mark that indicates how much memory to free.
 */
void heap_rewind(heap_type_t heap_id, uintptr_t mark);

#endif // HEAP_H
