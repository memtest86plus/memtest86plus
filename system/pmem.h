// SPDX-License-Identifier: GPL-2.0
#ifndef PMEM_H
#define PMEM_H
/**
 * \file
 *
 * Provides a description of the system physical memory map.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stddef.h>
#include <stdint.h>

#define MAX_MEM_SEGMENTS    128     // must be >= E820_MAP_SIZE (boot.h)

typedef struct {
    uintptr_t       start;
    uintptr_t       end;
} pm_map_t;

extern pm_map_t     pm_map[MAX_MEM_SEGMENTS];
extern int          pm_map_size;

extern size_t       num_pm_pages;

#if defined(__aarch64__)
// Load limits computed at run time from the start of physical RAM, which may
// begin well above address 0. Set by pmem_init().
extern uintptr_t    low_load_limit;
extern uintptr_t    high_load_limit;
#endif

void pmem_init(void);

#endif /* PMEM_H */
