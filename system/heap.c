// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022 Martin Whitaker.
// Copyright (C) 2026 Sam Demeulemeester.
//
// WARNING: heap memory is taken out of the memory map, so it is never tested.
// Think twice before adding anything that makes a heap grow.
//
// Once testing has started, the only allocations are those of the USB save (drive
// rescan and file buffers), so heap_reserve_runtime() keeps HEAP_RUNTIME_PAGES
// low-memory pages aside (currently 12 pages, 48 KB) only when such a save is
// possible. Any other run-time user must add its own condition and page count
// there: without a reserve, every low-memory allocation made after that call fails.
// The startup allocations come on top and depend on the hardware: one page for
// SMP, then a few pages per USB controller.

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"

#include "memsize.h"
#include "pmem.h"
#include "usbhcd.h"

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

// Once reserved, the low-memory heap allocates below runtime_top only.
static bool         runtime_reserved = false;
static uintptr_t    runtime_floor    = 0;
static uintptr_t    runtime_top      = 0;
static uintptr_t    runtime_end      = 0;

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
    bool runtime = (heap_id == HEAP_TYPE_LM_1) && runtime_reserved;
    uintptr_t top   = runtime ? runtime_top   : pm_map[heap->segment].end;
    uintptr_t floor = runtime ? runtime_floor : heap->start;
    if (num_pages(size) > top - floor) {
        return 0;
    }
    uintptr_t addr = top - num_pages(size);
    addr &= ~((alignment - 1) >> PAGE_SHIFT);
    if (addr < floor) {
        return 0;
    }
    if (runtime) {
        runtime_top = addr;
    } else {
        pm_map[heap->segment].end = addr;
    }
    return addr << PAGE_SHIFT;
}

uintptr_t heap_mark(heap_type_t heap_id)
{
    const heap_t * heap = &heaps[heap_id];
    if (heap->segment < 0) {
        return 0;
    }
    if (heap_id == HEAP_TYPE_LM_1 && runtime_reserved) {
        return runtime_top;
    }
    return pm_map[heap->segment].end;
}

void heap_rewind(heap_type_t heap_id, uintptr_t mark)
{
    const heap_t * heap = &heaps[heap_id];
    if (heap_id == HEAP_TYPE_LM_1 && runtime_reserved) {
        if (mark > runtime_top && mark <= runtime_end) {
            runtime_top = mark;
        }
        return;
    }
    if (heap->segment >= 0 && mark > pm_map[heap->segment].end && mark <= heap->end) {
        pm_map[heap->segment].end = mark;
    }
}

void heap_reserve_runtime(void)
{
    const heap_t * heap = &heaps[HEAP_TYPE_LM_1];
    if (runtime_reserved || heap->segment < 0) {
        return;
    }
    uintptr_t pages = usb_save_possible() ? HEAP_RUNTIME_PAGES : 0;
    runtime_end = runtime_top = pm_map[heap->segment].end;
    uintptr_t avail_pages = runtime_top - heap->start;
    runtime_floor = runtime_top - (avail_pages < pages ? avail_pages : pages);
    pm_map[heap->segment].end = runtime_floor;
    runtime_reserved = true;
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
    uintptr_t heap_limit = PAGE_C(4,GB);
#elif defined(__loongarch_lp64)
    uintptr_t low_memory_heap = PAGE_C(256, MB);
    uintptr_t heap_limit = PAGE_C(4,GB);
#elif defined(__aarch64__)
    // RAM may start well above 0, so make the heap limits relative to the
    // start of RAM. If RAM starts below 3GB, keep the heap below an absolute
    // 3GB: some SoCs cannot DMA above it (e.g. BCM2712 USB on Raspberry Pi 5).
    uintptr_t low_memory_heap = pm_map[0].start + PAGE_C(256, MB);
    uintptr_t heap_limit;
    if (pm_map[0].start < PAGE_C(3,GB)) {
        heap_limit = PAGE_C(3,GB);
    } else {
        heap_limit = pm_map[0].start + PAGE_C(4,GB);
    }

    // If a memory segment straddles the heap limit, split it at the limit, so
    // the scan below can assign a heap even when RAM is one contiguous region.
    for (int i = 0; i < pm_map_size; i++) {
        if (pm_map[i].start < heap_limit && pm_map[i].end > heap_limit) {
            if (pm_map_size < MAX_MEM_SEGMENTS) {
                for (int j = pm_map_size; j > i + 1; j--) {
                    pm_map[j] = pm_map[j - 1];
                }
                pm_map[i + 1].start = heap_limit;
                pm_map[i + 1].end   = pm_map[i].end;
                pm_map[i].end       = heap_limit;
                pm_map_size++;
            }
            break;
        }
    }
#endif
    for (int i = 0; i < pm_map_size && pm_map[i].end <= heap_limit; i++) {
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

#if defined(__aarch64__)
    // There is no low-memory addressing constraint on this architecture, so
    // if no segment small enough for the low-memory heap was found (RAM is
    // typically one big contiguous region), just use the high-memory heap.
    if (heaps[HEAP_TYPE_LM_1].segment < 0) {
        heaps[HEAP_TYPE_LM_1] = heaps[HEAP_TYPE_HM_1];
    }
#endif
}
