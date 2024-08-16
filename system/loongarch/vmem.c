// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.

#include <stdbool.h>
#include <stdint.h>
#include <larchintrin.h>

#include "boot.h"

#include "cpuid.h"

#include "vmem.h"

extern bool map_numa_memory_range;
extern uint8_t highest_map_bit;

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAX_REGION_PAGES  256     // VM pages
#define PCI_IO_PERFIX     0xEULL << 40
#define DMW0_BASE         0x8000000000000000
#define MMIO_BASE         DMW0_BASE | PCI_IO_PERFIX

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

uintptr_t map_region(uintptr_t base_addr, size_t size __attribute__((unused)), bool only_for_startup __attribute__((unused)))
{
    if (((base_addr < 0x80000000) && (base_addr > 0x30000000)) || ((base_addr & PCI_IO_PERFIX) != 0x0)) {
        return MMIO_BASE | base_addr;
    } else if ((base_addr < 0x20000000) && (base_addr > 0x10000000)) {
        return DMW0_BASE | base_addr;
    } else {
        return base_addr;
    }
}

bool map_window(uintptr_t start_page __attribute__((unused)))
{
    return true;
}

void *first_word_mapping(uintptr_t page)
{
    void *result;

    if (map_numa_memory_range == true) {
        if ((page >> (highest_map_bit - PAGE_SHIFT)) & 0xF) {
            uintptr_t alias = (((uintptr_t)(page >> (highest_map_bit - PAGE_SHIFT)) & 0xF) << 32) ;
            alias |= page & ~(0xF << (highest_map_bit - PAGE_SHIFT));
            result = (void *)(alias << PAGE_SHIFT);
        } else {
            result = (void *)(page << PAGE_SHIFT);
        }
    } else {
        result = (void *)(page << PAGE_SHIFT);
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
    return page;
}
