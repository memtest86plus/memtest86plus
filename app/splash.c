// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester

#include <stdbool.h>
#include <stdint.h>

#include "font.h"
#include "gfx.h"
#include "keyboard.h"
#include "screen.h"
#include "usbhcd.h"

#include "string.h"

#include "config.h"
#include "build_version.h"

#include "splash.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define BG_TOP_COLOUR       0x000000
#define BG_BOTTOM_COLOUR    0x000000
#define VERSION_COLOUR      0x8f8f8f
#define STATUS_COLOUR       0xbfbfbf

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

extern const gfx_image_t splash_logo_white;

static bool         active = false;

static screen_fb_t  fb;

static int          text_scale;
static int          status_row;
static int          notice_row;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static void draw_centred_text(int y, uint32_t colour, const char *str)
{
    int width = strlen(str) * FONT_WIDTH * text_scale;
    gfx_draw_text((fb.width - width) / 2, y, text_scale, colour, str);
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void splash_start(void)
{
    // Early debug output and the "no USB keyboards" countdown must be displayed
    if (enable_trace || (usb_init_options & USB_DEBUG) || keyboard_types == KT_USB || !screen_hold(&fb)) {
        return;
    }

    active = true;

    gfx_init(&fb);
    gfx_set_background(BG_TOP_COLOUR, BG_BOTTOM_COLOUR);
    gfx_clear_rect(0, 0, fb.width, fb.height);

    bool shrink = fb.width < 1024;
    int logo_width  = shrink ? splash_logo_white.width  / 2 : splash_logo_white.width;
    int logo_height = shrink ? splash_logo_white.height / 2 : splash_logo_white.height;
    int logo_y = (fb.height * 2) / 5 - logo_height / 2;
    gfx_draw_image((fb.width - logo_width) / 2, logo_y, &splash_logo_white, shrink);

    text_scale = fb.width >= 1280 ? 2 : 1; // > or >= ?
    status_row = (fb.height * 3) / 4;
    notice_row = (fb.height * 7) / 8;

    draw_centred_text(logo_y + logo_height + FONT_HEIGHT * text_scale, VERSION_COLOUR, "v" MT_VERSION);
}

void splash_status(const char *message)
{
    if (!active) return;

    gfx_clear_rect(0, status_row, fb.width, FONT_HEIGHT * text_scale);
    draw_centred_text(status_row, STATUS_COLOUR, message);
}

void splash_notice(const char *message)
{
    if (!active) return;

    gfx_clear_rect(0, status_row, fb.width, FONT_HEIGHT * text_scale);
    gfx_clear_rect(0, notice_row, fb.width, FONT_HEIGHT * text_scale);
    draw_centred_text(notice_row, STATUS_COLOUR, message);
}

void splash_end(void)
{
    if (!active) return;

    active = false;
    screen_release();
}
