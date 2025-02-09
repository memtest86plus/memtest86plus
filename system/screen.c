// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2024 Martin Whitaker

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"
#include "bootparams.h"

#include "font.h"
#include "vmem.h"

#include "string.h"

#include "screen.h"

//------------------------------------------------------------------------------
// Private Types
//------------------------------------------------------------------------------

typedef enum  __attribute__ ((packed)) {
    LFB_TOP_UP  = 0,
    LFB_RHS_UP  = 1,
    LFB_LHS_UP  = 2
} lfb_rotate_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

typedef struct {
    uint8_t     r;
    uint8_t     g;
    uint8_t     b;
} __attribute__((packed)) rgb_value_t;

static const rgb_value_t vga_pallete[16] = {
    //  R    G    B
    {   0,   0,   0 },  // BLACK
    {   0,   0, 170 },  // BLUE
    {   0, 170,   0 },  // GREEN
    {   0, 170, 170 },  // CYAN
    { 170,   0,   0 },  // RED
    { 170,   0, 170 },  // MAUVE
    { 170,  85,   0 },  // YELLOW (brown really)
    { 170, 170, 170 },  // WHITE
    {  85,  85,  85 },  // BOLD+BLACK
    {  85,  85, 255 },  // BOLD+BLUE
    {  85, 255,  85 },  // BOLD+GREEN
    {  85, 255, 255 },  // BOLD+CYAN
    { 255,  85,  85 },  // BOLD+RED
    { 255,  85, 255 },  // BOLD+MAUVE
    { 255, 255,  85 },  // BOLD+YELLOW
    { 255, 255, 255 }   // BOLD+WHITE
};

static vga_buffer_t *vga_buffer = NULL;

vga_buffer_t shadow_buffer;

static int lfb_bytes_per_pixel = 0;

static uintptr_t lfb_base;
static uintptr_t lfb_stride;

static uint32_t lfb_pallete[16];

static lfb_rotate_t lfb_rotate = LFB_TOP_UP;

static uint8_t current_attr = WHITE | BLUE << 4;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static void parse_option(const char *option, int option_length)
{
    if ((option_length < 8) || (strncmp(option, "screen.", 7) != 0))
        return;

    option_length -= 7;
    option += 7;
    if ((option_length == 6) && (strncmp(option, "rhs-up", 6) == 0)) {
        lfb_rotate = LFB_RHS_UP;
        return;
    }
    if ((option_length == 6) && (strncmp(option, "lhs-up", 6) == 0)) {
        lfb_rotate = LFB_LHS_UP;
        return;
    }
}

static void parse_cmd_line(uintptr_t cmd_line_addr, uint32_t cmd_line_size)
{
    if (cmd_line_addr != 0) {
        if (cmd_line_size == 0) cmd_line_size = 255;

        const char *cmd_line = (const char *)cmd_line_addr;
        const char *option = cmd_line;
        int option_length = 0;
        for (uint32_t i = 0; i < cmd_line_size; i++) {
            switch (cmd_line[i]) {
              case '\0':
                parse_option(option, option_length);
                return;
              case ' ':
                parse_option(option, option_length);
                option = &cmd_line[i+1];
                option_length = 0;
                break;
              default:
                option_length++;
                break;
            }
        }
    }
}

static void vga_put_char(int row, int col, uint8_t ch, uint8_t attr)
{
    shadow_buffer[row][col].ch   = ch;
    shadow_buffer[row][col].attr = attr;

    if (vga_buffer) {
        (*vga_buffer)[row][col].value = shadow_buffer[row][col].value;
    }
}

static int lfb_offset(int row, int col, int x, int y, int bpp)
{
    switch (lfb_rotate) {
      case LFB_RHS_UP:
        return (col * FONT_WIDTH  + x) * lfb_stride + ((SCREEN_HEIGHT - row) * FONT_HEIGHT - y - 1) * bpp;
      case LFB_LHS_UP:
        return ((SCREEN_WIDTH - col) * FONT_WIDTH - x - 1) * lfb_stride + (row * FONT_HEIGHT + y) * bpp;
      default:
        return 0;
    }
}

static void lfb8_put_char(int row, int col, uint8_t ch, uint8_t attr)
{
    if (shadow_buffer[row][col].ch   == ch &&
        shadow_buffer[row][col].attr == attr)
        return;

    shadow_buffer[row][col].ch   = ch;
    shadow_buffer[row][col].attr = attr;

    uint8_t fg_colour = attr % 16;
    uint8_t bg_colour = attr / 16;

    if (lfb_rotate) {
        for (int y = 0; y < FONT_HEIGHT; y++) {
            uint8_t font_row = font_data[ch][y];
            for (int x = 0; x < FONT_WIDTH; x++) {
                uint8_t *pixel = (uint8_t *)lfb_base + lfb_offset(row, col, x, y, 1);
                *pixel = font_row & 0x80 ? fg_colour : bg_colour;
                font_row <<= 1;
            }
        }
    } else {
        uint8_t *pixel_row = (uint8_t *)lfb_base + row * FONT_HEIGHT * lfb_stride + col * FONT_WIDTH;
        for (int y = 0; y < FONT_HEIGHT; y++) {
            uint8_t font_row = font_data[ch][y];
            for (int x = 0; x < FONT_WIDTH; x++) {
                pixel_row[x] = font_row & 0x80 ? fg_colour : bg_colour;
                font_row <<= 1;
            }
            pixel_row += lfb_stride;
        }
   }
}

static void lfb16_put_char(int row, int col, uint8_t ch, uint8_t attr)
{
    if (shadow_buffer[row][col].ch   == ch &&
        shadow_buffer[row][col].attr == attr)
        return;

    shadow_buffer[row][col].ch   = ch;
    shadow_buffer[row][col].attr = attr;

    uint16_t fg_colour = lfb_pallete[attr % 16];
    uint16_t bg_colour = lfb_pallete[attr / 16];

    if (lfb_rotate) {
        for (int y = 0; y < FONT_HEIGHT; y++) {
            uint8_t font_row = font_data[ch][y];
            for (int x = 0; x < FONT_WIDTH; x++) {
                uint16_t *pixel = (uint16_t *)lfb_base + lfb_offset(row, col, x, y, 1);
                *pixel = font_row & 0x80 ? fg_colour : bg_colour;
                font_row <<= 1;
            }
        }
    } else {
        uint16_t *pixel_row = (uint16_t *)lfb_base + row * FONT_HEIGHT * lfb_stride + col * FONT_WIDTH;
        for (int y = 0; y < FONT_HEIGHT; y++) {
            uint8_t font_row = font_data[ch][y];
            for (int x = 0; x < FONT_WIDTH; x++) {
                pixel_row[x] = font_row & 0x80 ? fg_colour : bg_colour;
                font_row <<= 1;
            }
            pixel_row += lfb_stride;
        }
    }
}

static void lfb24_put_char(int row, int col, uint8_t ch, uint8_t attr)
{

    if (shadow_buffer[row][col].ch   == ch &&
        shadow_buffer[row][col].attr == attr)
        return;

    shadow_buffer[row][col].ch   = ch;
    shadow_buffer[row][col].attr = attr;

    uint32_t fg_colour = lfb_pallete[attr % 16];
    uint32_t bg_colour = lfb_pallete[attr / 16];

    if (lfb_rotate) {
        for (int y = 0; y < FONT_HEIGHT; y++) {
            uint8_t font_row = font_data[ch][y];
            for (int x = 0; x < FONT_WIDTH; x++) {
                uint8_t *pixel = (uint8_t *)lfb_base + lfb_offset(row, col, x, y, 3);
                uint32_t colour = font_row & 0x80 ? fg_colour : bg_colour;
                pixel[0] = colour & 0xff; colour >>= 8;
                pixel[1] = colour & 0xff; colour >>= 8;
                pixel[2] = colour & 0xff;
                font_row <<= 1;
            }
        }
    } else {
        uint8_t *pixel_row = (uint8_t *)lfb_base + row * FONT_HEIGHT * lfb_stride + col * FONT_WIDTH * 3;
        for (int y = 0; y < FONT_HEIGHT; y++) {
            uint8_t font_row = font_data[ch][y];
            for (int x = 0; x < FONT_WIDTH * 3; x += 3) {
                uint32_t colour = font_row & 0x80 ? fg_colour : bg_colour;
                pixel_row[x+0] = colour & 0xff; colour >>= 8;
                pixel_row[x+1] = colour & 0xff; colour >>= 8;
                pixel_row[x+2] = colour & 0xff;
                font_row <<= 1;
            }
            pixel_row += lfb_stride;
        }
    }
}

static void lfb32_put_char(int row, int col, uint8_t ch, uint8_t attr)
{
    if (shadow_buffer[row][col].ch   == ch &&
        shadow_buffer[row][col].attr == attr)
        return;

    shadow_buffer[row][col].ch   = ch;
    shadow_buffer[row][col].attr = attr;

    uint32_t fg_colour = lfb_pallete[attr % 16];
    uint32_t bg_colour = lfb_pallete[attr / 16];

    if (lfb_rotate) {
        for (int y = 0; y < FONT_HEIGHT; y++) {
            uint8_t font_row = font_data[ch][y];
            for (int x = 0; x < FONT_WIDTH; x++) {
                uint32_t *pixel = (uint32_t *)lfb_base + lfb_offset(row, col, x, y, 1);
                *pixel = font_row & 0x80 ? fg_colour : bg_colour;
                font_row <<= 1;
            }
        }
    } else {
        uint32_t *pixel_row = (uint32_t *)lfb_base + row * FONT_HEIGHT * lfb_stride + col * FONT_WIDTH;
        for (int y = 0; y < FONT_HEIGHT; y++) {
            uint8_t font_row = font_data[ch][y];
            for (int x = 0; x < FONT_WIDTH; x++) {
                pixel_row[x] = font_row & 0x80 ? fg_colour : bg_colour;
                font_row <<= 1;
            }
            pixel_row += lfb_stride;
        }
    }
}

static void (*put_char)(int, int, uint8_t, uint8_t) = vga_put_char;

static void put_value(int row, int col, uint16_t value)
{
    put_char(row, col, value % 256, value / 256);
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void screen_init(void)
{
    const boot_params_t *boot_params = (boot_params_t *)boot_params_addr;

    parse_cmd_line(boot_params->cmd_line_ptr, boot_params->cmd_line_size);

    const screen_info_t *screen_info = &boot_params->screen_info;

    bool use_lfb = screen_info->orig_video_isVGA == VIDEO_TYPE_VLFB
                || screen_info->orig_video_isVGA == VIDEO_TYPE_EFI;

    if (use_lfb) {
        int lfb_width  = screen_info->lfb_width;
        int lfb_height = screen_info->lfb_height;
        int lfb_depth  = screen_info->lfb_depth;

        if (lfb_depth <= 8) {
            lfb_bytes_per_pixel = 1;
            put_char = lfb8_put_char;
        } else if (lfb_depth <= 16) {
            lfb_bytes_per_pixel = 2;
            put_char = lfb16_put_char;
        } else if (lfb_depth <= 24) {
            lfb_bytes_per_pixel = 3;
            put_char = lfb24_put_char;
        } else {
            lfb_bytes_per_pixel = 4;
            put_char = lfb32_put_char;
        }

        lfb_base = screen_info->lfb_base;
#if (ARCH_BITS == 64)
        if (LFB_CAPABILITY_64BIT_BASE & screen_info->capabilities) {
            lfb_base |= (uintptr_t)screen_info->ext_lfb_base << 32;
        }
#endif
        lfb_stride = screen_info->lfb_linelength;

        // Clip the framebuffer size to make sure we can map it into the 0.5GB device region.
        // This will produce a garbled display, but that's better than nothing.
        if (lfb_stride > 32768) {
            lfb_stride = 32768;
            if (lfb_width > (int)(lfb_stride / lfb_bytes_per_pixel)) {
                lfb_width = (int)(lfb_stride / lfb_bytes_per_pixel);
            }
        }
        if (lfb_height > 8192) lfb_height = 8192;

        // The above clipping should guarantee the mapping never fails.
        lfb_base = map_region(lfb_base, lfb_height * lfb_stride, false);

        // Blank the whole framebuffer.
        int pixels_per_word = sizeof(uint32_t) / lfb_bytes_per_pixel;
        uint32_t *line = (uint32_t *)lfb_base;
        for (int y = 0; y < lfb_height; y++) {
            for (int x = 0; x < (lfb_width / pixels_per_word); x++) {
                line[x] = 0;
            }
            line += lfb_stride / sizeof(uint32_t);
        }

        if (lfb_rotate) {
            int excess_width = lfb_width - (SCREEN_HEIGHT * FONT_HEIGHT);
            if (excess_width > 0) {
                lfb_base += (excess_width / 2) * lfb_bytes_per_pixel;
            }
            int excess_height = lfb_height - (SCREEN_WIDTH * FONT_WIDTH);
            if (excess_height > 0) {
                lfb_base += (excess_height / 2) * lfb_stride;
            }
        } else {
            int excess_width = lfb_width - (SCREEN_WIDTH * FONT_WIDTH);
            if (excess_width > 0) {
                lfb_base += (excess_width / 2) * lfb_bytes_per_pixel;
            }
            int excess_height = lfb_height - (SCREEN_HEIGHT * FONT_HEIGHT);
            if (excess_height > 0) {
                lfb_base += (excess_height / 2) * lfb_stride;
            }
        }

        if (lfb_bytes_per_pixel != 3) {
            lfb_stride /= lfb_bytes_per_pixel;
        }

        // Initialise the pallete.
        uint32_t r_max = (1 << screen_info->red_size  ) - 1;
        uint32_t g_max = (1 << screen_info->green_size) - 1;
        uint32_t b_max = (1 << screen_info->blue_size ) - 1;
        for (int i = 0; i < 16; i++) {
            uint32_t r = ((vga_pallete[i].r * r_max) / 255) << screen_info->red_pos;
            uint32_t g = ((vga_pallete[i].g * g_max) / 255) << screen_info->green_pos;
            uint32_t b = ((vga_pallete[i].b * b_max) / 255) << screen_info->blue_pos;
            lfb_pallete[i] = r | g | b;
        }
    } else if (screen_info->orig_video_isVGA != VIDEO_TYPE_NONE) {
        vga_buffer = (vga_buffer_t *)(0xb8000);
    }
}

void set_foreground_colour(screen_colour_t colour)
{
    current_attr = (current_attr & 0xf0) | (colour & 0x0f);
}

void set_background_colour(screen_colour_t  colour)
{
    current_attr = (current_attr & 0x8f) | ((colour << 4) & 0x70);
}

void clear_screen(void)
{
    for (int row = 0; row < SCREEN_HEIGHT; row++) {
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            put_char(row, col, ' ', current_attr);
        }
    }
}

void clear_screen_region(int start_row, int start_col, int end_row, int end_col)
{
    if (start_row < 0) start_row = 0;
    if (start_col < 0) start_col = 0;

    if (end_row >= SCREEN_HEIGHT) end_row = SCREEN_HEIGHT - 1;
    if (end_col >= SCREEN_WIDTH)  end_col = SCREEN_WIDTH  - 1;

    if (start_row > end_row) return;
    if (start_col > end_col) return;

    for (int row = start_row; row <= end_row; row++) {
        for (int col = start_col; col <= end_col; col++) {
            put_char(row, col, ' ', current_attr);
        }
    }
}

void scroll_screen_region(int start_row, int start_col, int end_row, int end_col)
{
    if (start_row < 0) start_row = 0;
    if (start_col < 0) start_col = 0;

    if (end_row >= SCREEN_HEIGHT) end_row = SCREEN_HEIGHT - 1;
    if (end_col >= SCREEN_WIDTH)  end_col = SCREEN_WIDTH  - 1;

    if (start_row > end_row) return;
    if (start_col > end_col) return;

    for (int row = start_row; row <= end_row; row++) {
        for (int col = start_col; col <= end_col; col++) {
            if (row < end_row) {
                put_value(row, col, shadow_buffer[row + 1][col].value);
            } else {
                put_char(row, col, ' ', current_attr);
            }
        }
    }
}

void save_screen_region(int start_row, int start_col, int end_row, int end_col, uint16_t buffer[])
{
    if (start_row < 0) start_row = 0;
    if (start_col < 0) start_col = 0;

    uint16_t *dst = &buffer[0];
    for (int row = start_row; row <= end_row; row++) {
        if (row >= SCREEN_HEIGHT) break;
        for (int col = start_col; col <= end_col; col++) {
            if (col >= SCREEN_WIDTH) break;
            *dst++ = shadow_buffer[row][col].value;
        }
    }
}

void restore_screen_region(int start_row, int start_col, int end_row, int end_col, const uint16_t buffer[])
{
    if (start_row < 0) start_row = 0;
    if (start_col < 0) start_col = 0;

    const uint16_t *src = &buffer[0];
    for (int row = start_row; row <= end_row; row++) {
        if (row >= SCREEN_HEIGHT) break;
        for (int col = start_col; col <= end_col; col++) {
            if (col >= SCREEN_WIDTH) break;
            put_value(row, col, *src++);
        }
    }
}

void print_char(int row, int col, char ch)
{
    if (row < 0 || row >= SCREEN_HEIGHT) return;
    if (col < 0 || col >= SCREEN_WIDTH)  return;

    put_char(row, col, ch, (current_attr & 0x0f) | (shadow_buffer[row][col].attr & 0xf0));
}
