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

static heap_t heaps[HEAP_TYPE_LAST] = {
    { .segment = -1, .start = 0, .end = 0 },
    { .segment = -1, .start = 0, .end = 0 }
};

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static size_t num_pages(size_t size)
{
    return (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

uintptr_t heap_alloc(heap_type_t heap_id, size_t size, uintptr_t alignment)
{
    const heap_t * heap = &heaps[heap_id];
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

uintptr_t heap_mark(heap_type_t heap_id)
{
    const heap_t * heap = &heaps[heap_id];
    if (heap->segment < 0) {
        return 0;
    }
    return pm_map[heap->segment].end;
}

void heap_rewind(heap_type_t heap_id, uintptr_t mark)
{
    const heap_t * heap = &heaps[heap_id];
    if (heap->segment >= 0 && mark > pm_map[heap->segment].end && mark <= heap->end) {
        pm_map[heap->segment].end = mark;
    }
}
//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void heap_init(void)
{
    // For x86_64 and i386 use the largest 20-bit addressable physical memory segment for the low-memory heap.
    // For loongarch use the largest 28-bit addressable physical memory segment for the low-memory heap.
    // Use the largest 32-bit addressable physical memory segment for the high-memory heap.
    // Exclude memory occupied by the program or below it in that segment.
    uintptr_t program_start = (uintptr_t)_start >> PAGE_SHIFT;
    uintptr_t program_end   = program_start + num_pages(_end - _start);
    uintptr_t max_segment_size = 0;
#if defined(__i386__) || defined (__x86_64__)
    uintptr_t low_memory_heap = PAGE_C(1, MB);
#elif defined(__loongarch_lp64)
    uintptr_t low_memory_heap = PAGE_C(256, MB);
#endif
    for (int i = 0; i < pm_map_size && pm_map[i].end <= PAGE_C(4,GB); i++) {
        uintptr_t try_heap_start = pm_map[i].start;
        uintptr_t try_heap_end   = pm_map[i].end;
        if (program_start >= try_heap_start && program_end <= try_heap_end) {
            try_heap_start = program_end;
        }
        uintptr_t segment_size = try_heap_end - try_heap_start;
        if (segment_size >= max_segment_size) {
            max_segment_size = segment_size;
            if (try_heap_end <= low_memory_heap) {
                heaps[HEAP_TYPE_LM_1].segment = i;
                heaps[HEAP_TYPE_LM_1].start   = try_heap_start;
                heaps[HEAP_TYPE_LM_1].end     = try_heap_end;
            }
            heaps[HEAP_TYPE_HM_1].segment = i;
            heaps[HEAP_TYPE_HM_1].start   = try_heap_start;
            heaps[HEAP_TYPE_HM_1].end     = try_heap_end;
        }
    }
}
