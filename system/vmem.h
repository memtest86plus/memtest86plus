// SPDX-License-Identifier: GPL-2.0
#ifndef VMEM_H
#define VMEM_H
/**
 * \file
 *
 * Provides functions to handle physical memory page mapping into virtual
 * memory.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memsize.h"

#define VM_PINNED_SIZE  PAGE_C(2,GB)

#define VM_WINDOW_SIZE  PAGE_C(1,GB)

uintptr_t map_region(uintptr_t base_addr, size_t size, bool only_for_startup);

bool map_window(uintptr_t start_page);

void *first_word_mapping(uintptr_t page);

void *last_word_mapping(uintptr_t page, size_t word_size);

uintptr_t page_of(void *addr);

#endif // VMEM_H
