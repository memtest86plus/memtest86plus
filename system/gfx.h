// SPDX-License-Identifier: GPL-2.0
#ifndef GFX_H
#define GFX_H
/**
 * \file
 *
 * Provides write-only drawing primitives for a native-mode 32bpp framebuffer
 * Colours are specified as 0xRRGGBB values.
 *
 *//*
 * Copyright (C) 2026 Sam Demeulemeester.
 */

#include <stdbool.h>
#include <stdint.h>

#include "screen.h"

/**
 * An image compressed as a PackBits RLE stream of pixel bytes, each holding a
 * colour index (bits 7:4, 0 = transparent) and a 4-bit alpha value (bits 3:0).
 */
typedef struct {
    int             width;
    int             height;
    uint32_t        colours[4];
    const uint8_t   *data;
} gfx_image_t;

/**
 * Initialises the drawing primitives for the specified framebuffer.
 */
void gfx_init(const screen_fb_t *fb);

/**
 * Sets the background to a vertical gradient between the specified colours.
 */
void gfx_set_background(uint32_t top_colour, uint32_t bottom_colour);

/**
 * Repaints the specified rectangle with the background.
 */
void gfx_clear_rect(int x, int y, int width, int height);

/**
 * Draws the string's foreground pixels at (x,y) (top left),
 * using the 8x16 font enlarged by an integer scale factor.
 */
void gfx_draw_text(int x, int y, int scale, uint32_t colour, const char *str);

/**
 * Draws the image at (x,y) (top left), blended over the background, 
 * and at half size if shrink is true.
 */
void gfx_draw_image(int x, int y, const gfx_image_t *image, bool shrink);

#endif // GFX_H
