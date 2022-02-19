// SPDX-License-Identifier: GPL-2.0
#ifndef PMEM_H
#define PMEM_H
/**
 * \file
 *
 * Provides a description of the system physical memory map.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stddef.h>
#include <stdint.h>

#define MAX_MEM_SEGMENTS    127

typedef struct {
    uintptr_t       start;
    uintptr_t       end;
} pm_map_t;

extern pm_map_t     pm_map[MAX_MEM_SEGMENTS];
extern int          pm_map_size;

extern size_t       num_pm_pages;

void pmem_init(void);

#endif /* PMEM_H */
