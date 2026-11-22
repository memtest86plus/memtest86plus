// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester

#include <stdbool.h>
#include <stdint.h>

#include "cpuinfo.h"
#include "font.h"
#include "gfx.h"
#include "memctrl.h"
#include "pmem.h"
#include "screen.h"
#include "smbios.h"
#include "spd.h"
#include "tsc.h"

#include "string.h"

#include "config.h"
#include "dimm_err.h"
#include "display.h"
#include "error.h"
#include "test.h"

#include "splash_logo.h"

#include "result_page.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define BG              0x000000
#define PCB_OK          0x1d6b38
#define PCB_FAIL        0xa31d1d
#define PCB_SUSPECT     0x80520a
#define HATCH_FAIL      0x6e1010
#define CHIP            0x141414
#define CHIP_EDGE       0x2c2c2c
#define GOLD            0xd2a63c
#define EMPTY_LINE      0x505050
#define TEXT_WHITE      0xe6e6e6
#define TEXT_GREY       0x8f8f8f
#define TEXT_DIM        0x5c5c5c
#define TEXT_OK         0x45c46a
#define TEXT_FAIL       0xff5a5a
#define TEXT_SUSPECT    0xf0b030

#define MAX_SLOTS       MAX_DMI_MEM_DEVICES
#define MAX_ROW_SLOTS   8                   // more slots are drawn on two rows

#define TEXT_LEN        96

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef enum { SLOT_EMPTY, SLOT_OK, SLOT_FAIL, SLOT_SUSPECT } slot_state_t;

typedef struct {
    int8_t          dmi;                    // dmi_slot_info[] index, -1 = none
    int8_t          spd;                    // spd_slot_cache[] index, -1 = none
    slot_state_t    state;
    uint64_t        errors;
    uint64_t        suspect;                // errors in this module or the other one of its channel
} slot_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static bool         shown = false;

static screen_fb_t  fb;

static uint64_t     drawn_errors;
static int          drawn_pass;

static slot_t       slots[MAX_SLOTS];
static int          num_slots;

static uint64_t     attributed;
static uint64_t     ambiguous;
static uint64_t     unattributed;

static int          mod_x, mod_y;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static int append_str(char *buf, int len, const char *str)
{
    while (*str && len < TEXT_LEN - 1) {
        buf[len++] = *str++;
    }
    buf[len] = '\0';
    return len;
}

static int append_uint(char *buf, int len, uint64_t value, int min_digits)
{
    char digits[20];
    int n = 0;

    do {
        digits[n++] = '0' + value % 10;
        value /= 10;
    } while (value > 0 || n < min_digits);
    while (n > 0 && len < TEXT_LEN - 1) {
        buf[len++] = digits[--n];
    }
    buf[len] = '\0';
    return len;
}

static int append_count(char *buf, int len, uint64_t count, const char *noun)
{
    len = append_uint(buf, len, count, 1);
    len = append_str(buf, len, noun);
    return (count == 1) ? len : append_str(buf, len, "s");
}

// Shortens a board label: "Controller0-ChannelA-DIMM1" -> "MC0-ChA-DIMM1".
static void append_label(char *buf, const char *label)
{
    static const char * const word[] = { "controller", "channel" };
    static const char * const abbr[] = { "MC", "Ch" };
    int len = 0;

    buf[0] = '\0';
    while (*label && len < TEXT_LEN - 1) {
        int w;
        for (w = 0; w < 2; w++) {
            int k = 0;
            while (word[w][k] && (label[k] | 0x20) == word[w][k]) {
                k++;
            }
            if (word[w][k] == '\0') {
                len = append_str(buf, len, abbr[w]);
                label += k;
                break;
            }
        }
        if (w == 2) {
            buf[len++] = *label++;
            buf[len] = '\0';
        }
    }
}

// Centres str on cx, at 1x if it does not fit max_width, then cut (keeping its end if keep_tail).
static void draw_text(int cx, int y, int max_width, int scale, uint32_t colour, const char *str, bool keep_tail)
{
    char text[TEXT_LEN];

    int len = strlen(str);
    if (len * FONT_WIDTH * scale > max_width) {
        scale = 1;
    }
    int max_len = max_width / FONT_WIDTH;
    if (max_len > TEXT_LEN - 1) {
        max_len = TEXT_LEN - 1;
    }
    if (len > max_len) {
        if (keep_tail) {
            str += len - max_len;
            // Don't start on a separator.
            while (max_len > 1 && (*str == '-' || *str == '_' || *str == ' ')) {
                str++;
                max_len--;
            }
        }
        len = max_len;
    }
    for (int i = 0; i < len; i++) {
        text[i] = str[i];
    }
    text[len] = '\0';
    gfx_draw_text(cx - len * FONT_WIDTH * scale / 2, y, scale, colour, text);
}

// Module space: u along the module (down the screen), v across it, gold fingers at the v = width edge.
static void mod_rect(int u, int v, int du, int dv, uint32_t colour)
{
    gfx_fill_rect(mod_x + v, mod_y + u, dv, du, colour);
}

static void draw_module(int x, int y, int length, int width, slot_state_t state)
{
    mod_x = x;
    mod_y = y;

    if (state == SLOT_EMPTY) {
        // Dashed outline of the module footprint.
        for (int u = 0; u < length; u += 16) {
            int du = (u + 9 > length) ? length - u : 9;
            mod_rect(u, 0, du, 2, EMPTY_LINE);
            mod_rect(u, width - 2, du, 2, EMPTY_LINE);
        }
        for (int v = 0; v < width; v += 16) {
            int dv = (v + 9 > width) ? width - v : 9;
            mod_rect(0, v, 2, dv, EMPTY_LINE);
            mod_rect(length - 2, v, 2, dv, EMPTY_LINE);
        }
        return;
    }

    mod_rect(0, 0, length, width, (state == SLOT_OK) ? PCB_OK : (state == SLOT_FAIL) ? PCB_FAIL : PCB_SUSPECT);
    if (state == SLOT_FAIL) {
        // Diagonal hatch: the state must not rely on the colour alone.
        for (int row = 0; row < length; row++) {
            for (int col = -(row % 18); col < width; col += 18) {
                int start = (col < 0) ? 0 : col;
                int end   = (col + 7 > width) ? width : col + 7;
                if (end > start) {
                    gfx_fill_rect(x + start, y + row, end - start, 1, HATCH_FAIL);
                }
            }
        }
    }

    // Latch cut-outs at both ends.
    int latch_u = length / 80 + 2;
    mod_rect(0, width * 45 / 100, latch_u, width / 8, BG);
    mod_rect(length - latch_u, width * 45 / 100, latch_u, width / 8, BG);

    // 2 x 4 DRAM chips with the PMIC in between.
    int chip_u = length * 8 / 100, chip_dv = width * 38 / 100, chip_v = width * 16 / 100;
    int gap = length * 2 / 100, centre = length * 7 / 100;
    int u0 = (length - (8 * chip_u + 6 * gap + centre)) / 2;
    for (int i = 0; i < 8; i++) {
        int u = u0 + i * (chip_u + gap) + (i >= 4 ? centre - gap : 0);
        mod_rect(u, chip_v, chip_u, chip_dv, CHIP);
        mod_rect(u, chip_v, chip_u, 2, CHIP_EDGE);
    }
    mod_rect(length / 2 - length / 60, width * 26 / 100, length / 30, width * 14 / 100, CHIP);

    // Gold fingers, with the key notch.
    int finger_dv = width * 14 / 100;
    int notch_u = length * 47 / 100, notch_du = length / 70 + 3;
    for (int u = length * 3 / 100; u < length * 97 / 100; u += 5) {
        if (u + 3 > notch_u - 2 && u < notch_u + notch_du + 2) continue;
        mod_rect(u, width - finger_dv, 3, finger_dv, GOLD);
    }
    mod_rect(notch_u, width - finger_dv - 4, notch_du, finger_dv + 4, BG);
}

// One slot per SMBIOS socket, else per SPD slot. Only values are kept: see the relocation rules.
static void build_model(void)
{
    int num_populated = 0, num_spd = 0, last_populated = 0;

    num_slots = 0;
    if (dmi_num_slots > 0) {
        for (int i = 0; i < dmi_num_slots; i++) {
            slots[num_slots++] = (slot_t){ .dmi = i, .spd = -1 };
        }
    }
    for (int spd = 0; spd < MAX_SPD_SLOT; spd++) {
        if (!spd_slot_cache[spd].isValid) {
            continue;
        }
        num_spd++;
        int dmi = smbios_spd_slot_dmi(spd);
        if (dmi_num_slots == 0) {
            dmi = num_slots++;
            slots[dmi] = (slot_t){ .dmi = -1 };
        }
        if (dmi >= 0) {
            slots[dmi].spd     = spd;
            slots[dmi].errors  = dimm_err_slot_errors(spd);
            slots[dmi].suspect = dimm_err_slot_suspect(spd);
        }
    }
    for (int i = 0; i < num_slots; i++) {
        if (slots[i].dmi < 0 || dmi_slot_info[slots[i].dmi].size_mb != 0) {
            num_populated++;
            last_populated = i;
        }
    }
    // A single module gets every error (unless the socket list may be truncated).
    if (num_populated == 1 && num_spd <= 1 && dmi_num_slots < MAX_DMI_MEM_DEVICES) {
        slots[last_populated].errors = error_count;
    }

    attributed = 0;
    for (int i = 0; i < num_slots; i++) {
        attributed += slots[i].errors;
    }
    ambiguous    = dimm_err_ambiguous();
    unattributed = (error_count > attributed + ambiguous) ? error_count - attributed - ambiguous : 0;

    for (int i = 0; i < num_slots; i++) {
        slot_t *slot = &slots[i];
        if (slot->dmi >= 0 && dmi_slot_info[slot->dmi].size_mb == 0) {
            slot->state = SLOT_EMPTY;
        } else if (slot->errors > 0) {
            slot->state = SLOT_FAIL;
        } else if (slot->suspect > 0) {
            slot->state = SLOT_SUSPECT;
        } else {
            slot->state = (attributed + ambiguous == 0 && error_count > 0) ? SLOT_SUSPECT : SLOT_OK;
        }
    }
}

static const char *slot_bank(int i)
{
    return (slots[i].dmi >= 0) ? dmi_slot_info[slots[i].dmi].bank : "";
}

static bool same_bank(int i, int j)
{
    return strncmp(slot_bank(i), slot_bank(j), sizeof(dmi_slot_info[0].bank)) == 0;
}

static bool unique_locator(int i)
{
    for (int j = 0; j < num_slots; j++) {
        if (j != i && slots[i].dmi >= 0 && slots[j].dmi >= 0
            && strncmp(dmi_slot_info[slots[i].dmi].locator, dmi_slot_info[slots[j].dmi].locator,
                       sizeof(dmi_slot_info[0].locator)) == 0) {
            return false;
        }
    }
    return true;
}

static void draw_slot_text(int i, int cx, int y, int max_width, int scale, bool grouped)
{
    const slot_t *slot = &slots[i];
    const dmi_slot_info_t *dmi = (slot->dmi >= 0) ? &dmi_slot_info[slot->dmi] : NULL;
    bool empty = (slot->state == SLOT_EMPTY);
    int line = FONT_HEIGHT * scale + 6;
    char text[TEXT_LEN];

    if (dmi != NULL) {
        append_label(text, dmi->locator);
    } else {
        append_uint(text, append_str(text, 0, "SLOT "), slot->spd, 1);
    }
    draw_text(cx, y, max_width, scale, empty ? TEXT_DIM : TEXT_WHITE, text, true);
    y += line;

    uint32_t size_mb = (slot->spd >= 0) ? spd_slot_cache[slot->spd].module_size : dmi->size_mb;
    if (!empty && size_mb != 0 && size_mb != DMI_SLOT_SIZE_UNKNOWN) {
        bool gb = (size_mb % 1024 == 0);
        append_str(text, append_uint(text, 0, gb ? size_mb / 1024 : size_mb, 1), gb ? " GB" : " MB");
        draw_text(cx, y, max_width, scale, TEXT_GREY, text, false);
    }
    y += line;

    // Sockets with the same locator are told apart by their bank, shown here if not in a heading.
    if (dmi != NULL && !grouped && !unique_locator(i)) {
        draw_text(cx, y, max_width, 1, TEXT_DIM, dmi->bank, true);
    } else if (!empty) {
        draw_text(cx, y, max_width, 1, TEXT_DIM, (slot->spd >= 0) ? spd_slot_cache[slot->spd].sku : dmi->part, false);
    }
    y += FONT_HEIGHT + 6;

    switch (slot->state) {
      case SLOT_EMPTY:
        draw_text(cx, y, max_width, scale, TEXT_DIM, "EMPTY", false);
        break;
      case SLOT_OK:
        draw_text(cx, y, max_width, scale, TEXT_OK, "OK", false);
        break;
      case SLOT_SUSPECT:
        draw_text(cx, y, max_width, scale, TEXT_SUSPECT, "SUSPECT", false);
        break;
      case SLOT_FAIL:
        if (slot->errors == 1) {
            append_str(text, 0, "1 ERROR");
        } else if (slot->errors < 100000) {
            append_str(text, append_uint(text, 0, slot->errors, 1), " ERRORS");
        } else {
            append_str(text, 0, ">99999 ERRORS");
        }
        draw_text(cx, y, max_width, scale, TEXT_FAIL, text, false);
        break;
    }
}

static void draw(void)
{
    char text[TEXT_LEN];
    int len;

    drawn_errors = error_count;
    drawn_pass   = pass_num;

    build_model();

    gfx_fill_rect(0, 0, fb.width, fb.height, BG);

    int scale  = (fb.width >= 1280 && fb.height >= 720) ? 2 : 1;
    int line   = FONT_HEIGHT * scale;
    int margin = fb.height / 30;
    int cx     = fb.width / 2;
    int y      = margin;

    // Header: logo, verdict, summary, notice.
    if (fb.height >= 600) {
        gfx_draw_image(cx - splash_logo_white.width / 4, y, &splash_logo_white, true);
        y += splash_logo_white.height / 2 + line / 2;
    }

    int verdict_scale = (fb.height >= 900) ? 5 : (fb.height >= 600) ? 4 : 2;
    if (error_count > 0) {
        draw_text(cx, y, fb.width, verdict_scale, TEXT_FAIL, "FAIL", false);
    } else if (pass_num > 0) {
        draw_text(cx, y, fb.width, verdict_scale, TEXT_OK, "PASS", false);
    } else {
        draw_text(cx, y, fb.width, verdict_scale, TEXT_GREY, "TESTING", false);
    }
    y += FONT_HEIGHT * verdict_scale + line / 2;

    len = append_str(text, append_uint(text, 0, pass_num, 1), (pass_num == 1) ? " pass" : " passes");
    len = append_count(text, append_str(text, len, " \xfa "), error_count, " error");
    if (clks_per_msec > 0) {
        int secs = (get_tsc() - run_start_time) / (1000 * (uint64_t)clks_per_msec);
        len = append_uint(text, append_str(text, len, " \xfa "), secs / 3600, 1);
        len = append_uint(text, append_str(text, len, ":"), (secs / 60) % 60, 2);
        len = append_uint(text, append_str(text, len, ":"), secs % 60, 2);
    }
    uint64_t mem_mb = (num_pm_pages + 128) / 256;
    len = append_str(text, len, " \xfa ");
    if (mem_mb >= 1024) {
        len = append_str(text, append_uint(text, len, (mem_mb + 512) / 1024, 1), " GB");
    } else {
        len = append_str(text, append_uint(text, len, mem_mb, 1), " MB");
    }
    // Running configuration from the IMC, else the SPD one.
    const char *type = (imc.freq > 0) ? imc.type : ram.type;
    int freq = (imc.freq > 0) ? imc.freq : ram.freq;
    if (freq > 0) {
        len = append_str(text, append_str(text, append_str(text, len, " "), type), "-");
        len = append_uint(text, len, freq, 1);
    }
    draw_text(cx, y, fb.width, scale, TEXT_GREY, text, false);
    y += line + line / 2;

    len = 0;
    if (ambiguous > 0) {
        len = append_str(text, append_count(text, 0, ambiguous, " error"), " in one of the amber modules");
    }
    if (unattributed > 0 && attributed + ambiguous == 0) {
        len = append_str(text, 0, "Errors not attributed to a module (DIMM decoder: ");
        len = append_str(text, append_str(text, len, memctrl_decode_status()), ")");
    } else if (unattributed > 0) {
        len = append_count(text, append_str(text, len, (len > 0) ? ", " : ""), unattributed, " more error");
        len = append_str(text, len, " not attributed to a module");
    }
    if (len > 0) {
        draw_text(cx, y, fb.width, scale, TEXT_SUSPECT, text, false);
    }
    y += line + line / 2;

    int footer_y = fb.height - margin - line;
    draw_text(cx, footer_y, fb.width, scale, TEXT_GREY, "Press any key to return to the test screen", false);

    if (num_slots == 0) {
        return;
    }

    // Modules: one row, with a heading per bank when sockets share banks, or two rows without headings.
    int rows    = (num_slots > MAX_ROW_SLOTS) ? 2 : 1;
    int per_row = (num_slots + rows - 1) / rows;

    int groups = 1;
    for (int i = 1; i < num_slots; i++) {
        if (!same_bank(i, i - 1)) groups++;
    }
    bool grouped = (rows == 1 && groups > 1 && groups < num_slots);
    if (!grouped) {
        groups = 1;
    }

    // The gap between two groups is half a pitch.
    int pitch = 2 * (fb.width - 2 * margin) / (2 * per_row + groups - 1);
    if (pitch > 125 * scale) {
        pitch = 125 * scale;
    }
    int gap = pitch / 2;

    int heading = grouped ? line + 14 : 0;
    int slot_text = 3 * (line + 6) + FONT_HEIGHT + 6;
    int row_height = (footer_y - line - y - heading) / rows;

    int length = row_height - slot_text - 12 - (rows - 1) * FONT_HEIGHT;
    int width  = pitch * 42 / 100;
    if (width > length * 10 / 42) {
        width = length * 10 / 42;
    } else {
        length = width * 42 / 10;
    }
    if (length < 32) {
        return;
    }

    for (int row = 0; row < rows; row++) {
        int first = row * per_row;
        int count = (num_slots - first < per_row) ? num_slots - first : per_row;
        int top = y + heading + row * row_height;
        int x = (fb.width - (count * pitch + (groups - 1) * gap)) / 2;

        for (int i = first; i < first + count; i++) {
            if (grouped && (i == first || !same_bank(i, i - 1))) {
                if (i != first) {
                    x += gap;
                }
                int n = 1;
                while (i + n < num_slots && same_bank(i + n, i)) n++;
                draw_text(x + n * pitch / 2, y, n * pitch - 16, scale, TEXT_GREY, slot_bank(i), true);
                gfx_fill_rect(x + 8, y + line + 4, n * pitch - 16, 2, TEXT_DIM);
            }
            draw_module(x + (pitch - width) / 2, top, length, width, slots[i].state);
            draw_slot_text(i, x + pitch / 2, top + length + 12, pitch - 8, scale, grouped);
            x += pitch;
        }
    }
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

bool result_page_available(void)
{
    return !enable_trace && !enable_tty && screen_can_hold();
}

bool result_page_show(void)
{
    if (shown) {
        return true;
    }
    if (!result_page_available() || !screen_hold(&fb)) {
        return false;
    }
    shown = true;

    gfx_init(&fb);
    draw();
    return true;
}

bool result_page_shown(void)
{
    return shown;
}

void result_page_refresh(void)
{
    if (shown && (error_count != drawn_errors || pass_num != drawn_pass)) {
        draw();
    }
}

void result_page_hide(void)
{
    if (!shown) return;

    shown = false;
    screen_release();
}
