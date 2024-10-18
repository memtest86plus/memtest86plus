// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 David Koňařík.
//
// Error display option that shows ranges of faulty memory locations.
#ifndef RANGES_H 
#define RANGES_H

#include <stdbool.h>
#include <stdint.h>

void ranges_display_init();

/**
 * Returns true if the range display should be redrawn
 */
bool ranges_display_insert(uintptr_t addr);

void ranges_display();

#endif // RANGES_H
