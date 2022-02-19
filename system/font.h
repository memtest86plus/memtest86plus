// SPDX-License-Identifier: GPL-2.0
#ifndef FONT_H
#define FONT_H
/**
 * \file
 *
 * Provides the font used for framebuffer display.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdint.h>

/**
 * Font size definitions.
 */
#define FONT_WIDTH  8
#define FONT_HEIGHT 16
#define FONT_CHARS  256

/**
 * The font data.
 */
extern const uint8_t font_data[FONT_CHARS][FONT_HEIGHT];

#endif // FONT_H
