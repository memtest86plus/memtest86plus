// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022 Martin Whitaker.

#include <stdint.h>

#include "boot.h"

#include "memsize.h"
#include "pmem.h"

#include "heap.h"

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef struct {
    int         segment;
    uintptr_t   start;
    uintptr_t   end;
} heap_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static heap_t lm_heap = { .segment = -1, .start = 0, .end = 0 };
static heap_t hm_heap = { .segment = -1, .start = 0, .end = 0 };

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static size_t num_pages(size_t size)
{
    return (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

static uintptr_t heap_alloc(const heap_t *heap, size_t size, uintptr_t alignment)
{
    if (heap->segment < 0) {
        return 0;
    }
    uintptr_t addr = pm_map[heap->segment].end - num_pages(size);
    addr &= ~((alignment - 1) >> PAGE_SHIFT);
    if (addr < heap->start) {
        return 0;
    }
    pm_map[heap->segment].end = addr;
    return addr << PAGE_SHIFT;
}

static uintptr_t heap_mark(const heap_t *heap)
{
    if (heap->segment < 0) {
        return 0;
    }
    return pm_map[heap->segment].end;
}

static void heap_rewind(const heap_t *heap, uintptr_t mark)
{
    if (heap->segment >= 0 && mark > pm_map[heap->segment].end && mark <= heap->end) {
        pm_map[heap->segment].end = mark;
    }
}
//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void heap_init(void)
{
    // Use the largest 20-bit addressable physical memory segment for the low-memory heap.
    // Use the largest 32-bit addressable physical memory segment for the high-memory heap.
    // Exclude memory occupied by the program or below it in that segment.
    uintptr_t program_start = (uintptr_t)_start >> PAGE_SHIFT;
    uintptr_t program_end   = program_start + num_pages(_end - _start);
    uintptr_t max_segment_size = 0;
    for (int i = 0; i < pm_map_size && pm_map[i].end <= PAGE_C(4,GB); i++) {
        uintptr_t try_heap_start = pm_map[i].start;
        uintptr_t try_heap_end   = pm_map[i].end;
        if (program_start >= try_heap_start && program_end <= try_heap_end) {
            try_heap_start = program_end;
        }
        uintptr_t segment_size = try_heap_end - try_heap_start;
        if (segment_size >= max_segment_size) {
            max_segment_size = segment_size;
            if (try_heap_end <= PAGE_C(1,MB)) {
                lm_heap.segment = i;
                lm_heap.start   = try_heap_start;
                lm_heap.end     = try_heap_end;
            }
            hm_heap.segment = i;
            hm_heap.start   = try_heap_start;
            hm_heap.end     = try_heap_end;
        }
    }
}

uintptr_t lm_heap_alloc(size_t size, uintptr_t alignment)
{
    return heap_alloc(&lm_heap, size, alignment);
}

uintptr_t lm_heap_mark(void)
{
    return heap_mark(&lm_heap);
}

void lm_heap_rewind(uintptr_t mark)
{
    heap_rewind(&lm_heap, mark);
}

uintptr_t hm_heap_alloc(size_t size, uintptr_t alignment)
{
    return heap_alloc(&hm_heap, size, alignment);
}

uintptr_t hm_heap_mark(void)
{
    return heap_mark(&hm_heap);
}

void hm_heap_rewind(uintptr_t mark)
{
    heap_rewind(&hm_heap, mark);
}
