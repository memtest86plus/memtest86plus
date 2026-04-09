// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester.

#include "cpuinfo.h"
#include "tsc.h"
#include "io.h"
#include "heap.h"
#include "memctrl.h"
#include "pmem.h"
#include "smbios.h"
#include "spd.h"
#include "usbhcd.h"
#include "usbmsd.h"
#include "fat32.h"

#include "display.h"
#include "error.h"
#include "test.h"
#include "tests.h"

#include "string.h"
#include "unistd.h"

#include "reports.h"

#include "build_version.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define RESULTS_BUF_SIZE    8192

#define POP_R       3
#define POP_C       21
#define POP_W       38
#define POP_LI      (POP_C + 5)

#define MILLISEC    1000

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

// RTC (CMOS) register reading. Ports 0x70/0x71 BCD format.

static uint8_t rtc_read(uint8_t reg)
{
    outb(reg, 0x70);
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

// Minimal buffer printf supporting %s, %i, %u, %x with field width.

static char *buf_end;

static char *buf_append_char(char *pos, char c)
{
    if (pos < buf_end) {
        *pos = c;
    }
    return pos + 1;
}

static char *buf_append_str(char *pos, const char *s)
{
    while (*s) {
        pos = buf_append_char(pos, *s++);
    }
    return pos;
}

static char *buf_append_uint(char *pos, uintptr_t value, int base, int min_width)
{
    char digits[20];
    int len = 0;

    if (value == 0) {
        digits[len++] = '0';
    } else {
        while (value > 0) {
            int d = value % base;
            digits[len++] = d < 10 ? '0' + d : 'a' + d - 10;
            value /= base;
        }
    }

    // Pad with leading zeros.
    while (len < min_width) {
        digits[len++] = '0';
    }

    // Output in reverse.
    for (int i = len - 1; i >= 0; i--) {
        pos = buf_append_char(pos, digits[i]);
    }
    return pos;
}

static char *buf_append_int(char *pos, int value, int min_width)
{
    if (value < 0) {
        pos = buf_append_char(pos, '-');
        value = -value;
        if (min_width > 0) min_width--;
    }
    return buf_append_uint(pos, value, 10, min_width);
}

static char *buf_printf(char *pos, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            pos = buf_append_char(pos, *fmt++);
            continue;
        }
        fmt++;

        // Parse width.
        int width = 0;
        bool pad_zero = (*fmt == '0');
        if (pad_zero) fmt++;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
          case 's':
            pos = buf_append_str(pos, va_arg(args, const char *));
            break;
          case 'i':
            pos = buf_append_int(pos, va_arg(args, int), pad_zero ? width : 1);
            break;
          case 'u':
            pos = buf_append_uint(pos, va_arg(args, uintptr_t), 10, pad_zero ? width : 1);
            break;
          case 'x':
            pos = buf_append_uint(pos, va_arg(args, uintptr_t), 16, pad_zero ? width : 1);
            break;
          case '%':
            pos = buf_append_char(pos, '%');
            break;
          default:
            pos = buf_append_char(pos, *fmt);
            break;
        }
        if (*fmt) fmt++;
    }

    va_end(args);
    return pos;
}

static int format_results(char *buf, int bufsize)
{
    buf_end = buf + bufsize - 1;
    char *pos = buf;

    pos = buf_printf(pos, "Memtest86+ v%s Report\r\n", MT_VERSION);
    pos = buf_printf(pos, "=======================\r\n\r\n");

    // Date & time from RTC.
    uint8_t rtc_sec  = bcd_to_bin(rtc_read(0x00));
    uint8_t rtc_min  = bcd_to_bin(rtc_read(0x02));
    uint8_t rtc_hour = bcd_to_bin(rtc_read(0x04));
    uint8_t rtc_day  = bcd_to_bin(rtc_read(0x07));
    uint8_t rtc_mon  = bcd_to_bin(rtc_read(0x08));
    uint8_t rtc_year = bcd_to_bin(rtc_read(0x09));
    uint8_t rtc_cent = bcd_to_bin(rtc_read(0x32));
    int full_year = (rtc_cent ? rtc_cent * 100 : 2000) + rtc_year;
    pos = buf_printf(pos, "Date: %04i-%02i-%02i %02i:%02i:%02i\r\n",
                     full_year, rtc_mon, rtc_day, rtc_hour, rtc_min, rtc_sec);
    pos = buf_printf(pos, "\r\n");

    // System info.
    if (cpu_model) {
        pos = buf_printf(pos, "CPU: %s\r\n", cpu_model);
    }

    // Motherboard (DMI) info.
    const char *board_mfg, *board_prod;
    get_smbios_board_info(&board_mfg, &board_prod);
    if (board_mfg && board_prod) {
        pos = buf_printf(pos, "Motherboard: %s %s\r\n", board_mfg, board_prod);
    }

    pos = buf_printf(pos, "Memory: %u MB\r\n", (uintptr_t)(num_pm_pages / 256));

    // IMC or RAM spec line.
    if (imc.freq) {
        if (imc.type[3] == '5') {
            pos = buf_printf(pos, "IMC: %s-%u / CAS %u%s-%u-%u-%u\r\n",
                             imc.type, imc.freq, imc.tCL, imc.tCL_dec ? ".5" : "",
                             imc.tRCD, imc.tRP, imc.tRAS);
        } else {
            pos = buf_printf(pos, "IMC: %uMHz (%s-%u) CAS %u%s-%u-%u-%u\r\n",
                             imc.freq / 2, imc.type, imc.freq, imc.tCL,
                             imc.tCL_dec ? ".5" : "", imc.tRCD, imc.tRP, imc.tRAS);
        }
    } else if (ram.freq > 0 && ram.tCL > 0) {
        if (ram.freq <= 166) {
            pos = buf_printf(pos, "RAM: %uMHz (%s PC%u) CAS %u-%u-%u-%u\r\n",
                             ram.freq, ram.type, ram.freq, ram.tCL,
                             ram.tRCD, ram.tRP, ram.tRAS);
        } else {
            pos = buf_printf(pos, "RAM: %uMHz (%s-%u) CAS %u%s-%u-%u-%u\r\n",
                             ram.freq / 2, ram.type, ram.freq, ram.tCL,
                             ram.tCL_dec ? ".5" : "", ram.tRCD, ram.tRP, ram.tRAS);
        }
    }

    pos = buf_printf(pos, "\r\n");

    // SPD information (from boot-time cache).
    pos = buf_printf(pos, "Memory SPD Information:\r\n");
    bool found_spd = false;
    for (int i = 0; i < MAX_SPD_SLOT; i++) {
        const spd_info *spdi = &spd_slot_cache[i];
        if (!spdi->isValid) continue;

        found_spd = true;
        pos = buf_printf(pos, "  Slot %i: %u MB %s-%u",
                         spdi->slot_num, spdi->module_size, spdi->type, spdi->freq);

        if (spdi->hasECC) {
            pos = buf_printf(pos, " ECC");
        }

        const char *mfg = jedec_manufacturer_name(spdi->jedec_code);
        if (mfg) {
            pos = buf_printf(pos, " - %s", mfg);
        } else if (spdi->jedec_code != 0) {
            pos = buf_printf(pos, " - Unknown (0x%x)", (uintptr_t)spdi->jedec_code);
        }

        if (spdi->sku[0]) {
            pos = buf_printf(pos, " %s", spdi->sku);
        }

        pos = buf_printf(pos, "\r\n");
    }
    if (!found_spd) {
        pos = buf_printf(pos, "  No SPD data available\r\n");
    }
    pos = buf_printf(pos, "\r\n");

    // Per-test results.
    pos = buf_printf(pos, "Per-Test Results:\r\n");
    for (int i = 0; i < NUM_TEST_PATTERNS; i++) {
        // Extract description text from between brackets.
        const char *desc = test_list[i].description;
        const char *start = desc;
        if (*start == '[') start++;
        int len = 0;
        while (start[len] && start[len] != ']') len++;
        // Trim trailing spaces.
        while (len > 0 && start[len - 1] == ' ') len--;

        pos = buf_printf(pos, "  Test %02i: [", i);
        for (int j = 0; j < len; j++) {
            pos = buf_append_char(pos, start[j]);
        }
        for (int j = len; j < 38; j++) {
            pos = buf_append_char(pos, ' ');
        }
        pos = buf_printf(pos, "]  Errors: %i\r\n", test_list[i].errors);
    }
    pos = buf_printf(pos, "\r\n");

    // Test summary.
    pos = buf_printf(pos, "Passes completed: %i\r\n", pass_num);
    pos = buf_printf(pos, "Errors: %u\r\n", (uintptr_t)error_count);
    pos = buf_printf(pos, "ECC Errors: %u\r\n", (uintptr_t)error_count_cecc);
    pos = buf_printf(pos, "\r\n");

    // Elapsed time.
    int elapsed_secs = 0;
    if (clks_per_msec > 0 && run_start_time > 0) {
        elapsed_secs = (int)((get_tsc() - run_start_time) / (1000 * (uint64_t)clks_per_msec));
    }
    int el_hours = elapsed_secs / 3600;
    int el_mins  = (elapsed_secs % 3600) / 60;
    int el_secs  = elapsed_secs % 60;

    // Status.
    if (pass_num == 0) {
        pos = buf_printf(pos, "Final Result: In progress (running for %02i:%02i:%02i)\r\n",
                         el_hours, el_mins, el_secs);
    } else if (error_count == 0) {
        pos = buf_printf(pos, "Final Result: PASS (running for %02i:%02i:%02i)\r\n",
                         el_hours, el_mins, el_secs);
    } else {
        pos = buf_printf(pos, "Final Result: FAIL (running for %02i:%02i:%02i)\r\n",
                         el_hours, el_mins, el_secs);
    }

    // Null-terminate.
    if (pos >= buf_end) pos = buf_end;
    *pos = '\0';

    return (int)(pos - buf);
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void save_results_to_usb(void)
{
    // Save heap state so we can free everything when done.
    uintptr_t heap_lm_mark = heap_mark(HEAP_TYPE_LM_1);

    prints(POP_R+14, POP_LI, "Searching for USB drive...");

    // Find a mass storage device.
    usb_msd_t msd;
    if (!find_usb_mass_storage(&msd)) {
        prints(POP_R+14, POP_LI, "No USB drive found.      ");
        usleep(2000 * MILLISEC);
        goto cleanup;
    }

    prints(POP_R+14, POP_LI, "Initializing USB drive...");

    if (!msd_init(&msd)) {
        prints(POP_R+14, POP_LI, "USB drive init failed.   ");
        usleep(2000 * MILLISEC);
        goto cleanup;
    }

    // Allocate a sector buffer.
    uintptr_t sec_buf_addr = heap_alloc(HEAP_TYPE_LM_1, msd.block_size, 64);
    if (sec_buf_addr == 0) {
        prints(POP_R+14, POP_LI, "Memory allocation failed.");
        usleep(2000 * MILLISEC);
        goto cleanup;
    }

    prints(POP_R+14, POP_LI, "Mounting FAT32 filesystem...");

    fat32_fs_t fs;
    if (!fat32_mount(&fs, &msd, (uint8_t *)sec_buf_addr)) {
        prints(POP_R+14, POP_LI, "No FAT32 filesystem found.");
        usleep(2000 * MILLISEC);
        goto cleanup;
    }

    // Generate filename.
    char filename[12];
    if (!fat32_next_filename(&fs, filename)) {
        prints(POP_R+14, POP_LI, "All filename slots full. ");
        usleep(2000 * MILLISEC);
        goto cleanup;
    }

    // Format results into a buffer.
    uintptr_t res_buf_addr = heap_alloc(HEAP_TYPE_LM_1, RESULTS_BUF_SIZE, 64);
    if (res_buf_addr == 0) {
        prints(POP_R+14, POP_LI, "Memory allocation failed.");
        usleep(2000 * MILLISEC);
        goto cleanup;
    }

    char *results_buf = (char *)res_buf_addr;
    int results_len = format_results(results_buf, RESULTS_BUF_SIZE);

    // Build display filename with dot: "MT86P_NN.TXT"
    char display_name[16];
    memcpy(display_name, filename, 6);
    display_name[6] = filename[6];
    display_name[7] = filename[7];
    display_name[8] = '.';
    display_name[9] = 'T';
    display_name[10] = 'X';
    display_name[11] = 'T';
    display_name[12] = '\0';

    printf(POP_R+14, POP_LI, "Writing %s...   ", display_name);

    if (!fat32_write_file(&fs, filename, results_buf, results_len)) {
        prints(POP_R+14, POP_LI, "Write failed!            ");
        usleep(2000 * MILLISEC);
        goto cleanup;
    }

    printf(POP_R+14, POP_C + (POP_W - 31) / 2, "Saved %s successfully.", display_name);
    usleep(2000 * MILLISEC);

cleanup:
    // Re-arm keyboard interrupt TRBs that may have been consumed
    // by the bulk transfer event handling.
    usb_rearm_keyboards();
    heap_rewind(HEAP_TYPE_LM_1, heap_lm_mark);
}
