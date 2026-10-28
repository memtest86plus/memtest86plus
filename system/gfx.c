// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester

#include <stdbool.h>
#include <stdint.h>

#include "font.h"
#include "screen.h"

#include "gfx.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAX_IMAGE_WIDTH     1024

//------------------------------------------------------------------------------
// Private Types
//------------------------------------------------------------------------------

typedef struct {
    const uint8_t   *src;
    int             count;
    bool            repeat;
    uint8_t         value;
} rle_state_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static screen_fb_t fb;

static uint8_t image_row[2][MAX_IMAGE_WIDTH];

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static uint32_t pack(uint32_t colour)
{
    return ((colour >> 16) & 0xff) << fb.red_pos
         | ((colour >>  8) & 0xff) << fb.green_pos
         | ((colour >>  0) & 0xff) << fb.blue_pos;
}

static void put_pixel(int x, int y, uint32_t pixel)
{
    if (x < 0 || y < 0 || x >= fb.width || y >= fb.height) return;

    ((uint32_t *)fb.base)[y * fb.stride + x] = pixel;
}

static uint32_t image_colour(const gfx_image_t *image, uint8_t value)
{
    int index = value >> 4;
    int alpha = value & 0xf;
    if (index == 0 || index > 3) return 0;

    uint32_t fg = image->colours[index];
    uint32_t colour = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        colour |= (((fg >> shift) & 0xff) * alpha / 15) << shift;
    }
    return colour;
}

static uint32_t average4(uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3)
{
    uint32_t colour = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        uint32_t sum = ((c0 >> shift) & 0xff) + ((c1 >> shift) & 0xff)
                     + ((c2 >> shift) & 0xff) + ((c3 >> shift) & 0xff);
        colour |= (sum / 4) << shift;
    }
    return colour;
}

static uint8_t rle_next(rle_state_t *rle)
{
    if (rle->count == 0) {
        uint8_t header = *rle->src++;
        rle->repeat = header & 0x80;
        rle->count  = (header & 0x7f) + 1;
        if (rle->repeat) {
            rle->value = *rle->src++;
        }
    }
    rle->count--;
    return rle->repeat ? rle->value : *rle->src++;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void gfx_init(const screen_fb_t *info)
{
    fb = *info;
}

void gfx_clear_rect(int x, int y, int width, int height)
{
    if (x < 0) { width  += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width  > fb.width)  width  = fb.width  - x;
    if (y + height > fb.height) height = fb.height - y;

    for (int row = y; row < y + height; row++) {
        uint32_t *line = (uint32_t *)fb.base + row * fb.stride;
        for (int col = x; col < x + width; col++) {
            line[col] = 0;
        }
    }
}

void gfx_draw_text(int x, int y, int scale, uint32_t colour, const char *str)
{
    uint32_t pixel = pack(colour);
    for (; *str != '\0'; str++, x += FONT_WIDTH * scale) {
        for (int py = 0; py < FONT_HEIGHT * scale; py++) {
            uint8_t font_row = font_data[(uint8_t)*str][py / scale];
            for (int px = 0; px < FONT_WIDTH * scale; px++) {
                if ((font_row << (px / scale)) & 0x80) {
                    put_pixel(x + px, y + py, pixel);
                }
            }
        }
    }
}

void gfx_draw_image(int x, int y, const gfx_image_t *image, bool shrink)
{
    if (image->width > MAX_IMAGE_WIDTH) return;

    rle_state_t rle = { .src = image->data, .count = 0, .repeat = false, .value = 0 };

    int step = shrink ? 2 : 1;
    for (int row = 0; row + step <= image->height; row += step) {
        for (int i = 0; i < step; i++) {
            for (int col = 0; col < image->width; col++) {
                image_row[i][col] = rle_next(&rle);
            }
        }
        int py = y + row / step;
        for (int col = 0; col + step <= image->width; col += step) {
            uint32_t colour;
            if (shrink) {
                colour = average4(image_colour(image, image_row[0][col]),
                                  image_colour(image, image_row[0][col + 1]),
                                  image_colour(image, image_row[1][col]),
                                  image_colour(image, image_row[1][col + 1]));
            } else {
                if ((image_row[0][col] >> 4) == 0) continue;
                colour = image_colour(image, image_row[0][col]);
            }
            put_pixel(x + col / step, py, pack(colour));
        }
    }
}
