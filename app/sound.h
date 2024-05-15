// SPDX-License-Identifier: GPL-2.0
#ifndef SOUND_H
#define SOUND_H
/**
 * \file
 *
 * Provides types and variables used when performing the memory tests.
 *
 * Copyright (C) 2024 Anton Ivanov (aka demiurg_spb+rigler).
 */

#include <stdbool.h>

void sound_beep(bool ok);
void sound_tick_task(void);


#endif // SOUND_H
