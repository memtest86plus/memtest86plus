// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.

#include <stdbool.h>
#include <stdint.h>
#include <larchintrin.h>
#include <string.h>

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

#define PCIE32_LOW_ADDRESS   0x30000000
#define PCIE32_HIGH_ADDRESS  0x80000000
#define PCIE40_LOW_ADDRESS   0x4000000000ULL
#define PCIE40_HIGH_ADDRESS  0xC000000000ULL
#define PCIE64_LOW_ADDRESS   0x8000000000ULL
#define PCIE64_HIGH_ADDRESS  0xFD00000000ULL
//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

uintptr_t map_region(uintptr_t base_addr, size_t size __attribute__((unused)), bool only_for_startup __attribute__((unused)))
{
    const bool is_2k_or_3b6000m =
        (strstr(cpuid_info.brand_id.str, "2K") != NULL) ||
        (strstr(cpuid_info.brand_id.str, "3B6000M") != NULL);

    // 32-bit PCI memory space base differs by platform
    const uintptr_t pci32_lo = PCIE32_LOW_ADDRESS;
    const uintptr_t pci32_hi = PCIE32_HIGH_ADDRESS;
    // 64-bit PCI memory space base differs by platform
    const uintptr_t pci64_lo = is_2k_or_3b6000m ? PCIE40_LOW_ADDRESS : PCIE64_LOW_ADDRESS;
    const uintptr_t pci64_hi = is_2k_or_3b6000m ? PCIE40_HIGH_ADDRESS : PCIE64_HIGH_ADDRESS;

    if (((base_addr < pci32_hi) && (base_addr >= pci32_lo)) ||  // 32-bit PCI memory space
      ((base_addr < pci64_hi) && (base_addr >= pci64_lo)) ||    // 64-bit PCI memory space
      ((base_addr & PCI_IO_PERFIX) != 0x0)) {
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
