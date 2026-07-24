// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester.

#include "cpuinfo.h"
#include "tsc.h"
#include "io.h"
#include "heap.h"
#include "hwctrl.h"
#include "memctrl.h"
#include "pmem.h"
#include "serial.h"
#include "smbios.h"
#include "smp.h"
#include "spd.h"
#include "usbhcd.h"
#include "usbmsd.h"
#include "fat32.h"

#include "config.h"

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

#define SERIAL_LOG_INTERVAL     10  // Seconds between serial log progress lines.

#if defined(__x86_64__)
#define SLOG_ARCH   "x64"
#elif defined(__i386__)
#define SLOG_ARCH   "x32"
#elif defined(__loongarch_lp64)
#define SLOG_ARCH   "la64"
#elif defined(__aarch64__)
#define SLOG_ARCH   "arm64"
#endif

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

// RTC (CMOS) register reading. Ports 0x70/0x71 BCD format.
// ISA-only; LoongArch has no equivalent fixed-port RTC.

#if defined(__i386__) || defined(__x86_64__)
static uint8_t rtc_read(uint8_t reg)
{
    outb(reg, 0x70);
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

static void rtc_get_datetime(int *year, int *mon, int *day, int *hour, int *min, int *sec)
{
    // Wait for any update in progress to complete (UIP bit, status register A).
    for (int i = 0; i < 20000 && (rtc_read(0x0A) & 0x80); i++) {}

    uint8_t rtc_stb  = rtc_read(0x0B);
    uint8_t rtc_sec  = rtc_read(0x00);
    uint8_t rtc_min  = rtc_read(0x02);
    uint8_t rtc_hour = rtc_read(0x04);
    uint8_t rtc_day  = rtc_read(0x07);
    uint8_t rtc_mon  = rtc_read(0x08);
    uint8_t rtc_year = rtc_read(0x09);
    uint8_t rtc_cent = rtc_read(0x32);

    bool rtc_pm = rtc_hour & 0x80;
    rtc_hour &= 0x7F;

    // Registers are BCD unless the RTC is in binary mode (status register B, DM bit).
    if (!(rtc_stb & 0x04)) {
        rtc_sec  = bcd_to_bin(rtc_sec);
        rtc_min  = bcd_to_bin(rtc_min);
        rtc_hour = bcd_to_bin(rtc_hour);
        rtc_day  = bcd_to_bin(rtc_day);
        rtc_mon  = bcd_to_bin(rtc_mon);
        rtc_year = bcd_to_bin(rtc_year);
        rtc_cent = bcd_to_bin(rtc_cent);
    }

    // Convert 12-hour mode (hour bit 7 = PM) to 24-hour.
    if (!(rtc_stb & 0x02)) {
        rtc_hour = rtc_hour % 12 + (rtc_pm ? 12 : 0);
    }

    *year = (rtc_cent ? rtc_cent * 100 : 2000) + rtc_year;
    *mon  = rtc_mon;
    *day  = rtc_day;
    *hour = rtc_hour;
    *min  = rtc_min;
    *sec  = rtc_sec;
}
#endif

// Minimal buffer printf supporting %s, %i, %u, %x with field width.
// %u and %x consume a uintptr_t argument - cast at every call site: on x86_64
// the upper half of a default-promoted 32-bit vararg register is undefined.

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

static char *buf_vprintf(char *pos, const char *fmt, va_list args)
{
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

    return pos;
}

static char *buf_printf(char *pos, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    pos = buf_vprintf(pos, fmt, args);
    va_end(args);
    return pos;
}

// Extract the description text from between the brackets, trailing spaces trimmed.
static int test_desc_text(int n, const char **text)
{
    const char *start = test_list[n].description;
    if (*start == '[') start++;
    int len = 0;
    while (start[len] && start[len] != ']') len++;
    while (len > 0 && start[len - 1] == ' ') len--;
    *text = start;
    return len;
}

static int format_results(char *buf, int bufsize)
{
    buf_end = buf + bufsize - 1;
    char *pos = buf;

    pos = buf_printf(pos, "Memtest86+ v" MT_VERSION " Report\r\n");
    pos = buf_printf(pos, "=======================\r\n\r\n");

    // Date & time from RTC (x86 ISA CMOS only).
#if defined(__i386__) || defined(__x86_64__)
    int year, mon, day, hour, min, sec;
    rtc_get_datetime(&year, &mon, &day, &hour, &min, &sec);
    pos = buf_printf(pos, "Date: %04i-%02i-%02i %02i:%02i:%02i\r\n",
                     year, mon, day, hour, min, sec);
    pos = buf_printf(pos, "\r\n");
#endif

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

    const char *system_serial, *baseboard_serial;
    get_smbios_serial_info(&system_serial, &baseboard_serial);
    if (system_serial) {
        pos = buf_printf(pos, "System Serial: %s\r\n", system_serial);
    }
    if (baseboard_serial) {
        pos = buf_printf(pos, "Baseboard Serial: %s\r\n", baseboard_serial);
    }

    pos = buf_printf(pos, "Memory: %u MB\r\n", (uintptr_t)(num_pm_pages / 256));

    // IMC or RAM spec line.
    if (imc.freq) {
        if (imc.type[3] == '5') {
            pos = buf_printf(pos, "IMC: %s-%u / CAS %u%s-%u-%u-%u\r\n",
                             imc.type, (uintptr_t)imc.freq, (uintptr_t)imc.tCL,
                             imc.tCL_dec ? ".5" : "",
                             (uintptr_t)imc.tRCD, (uintptr_t)imc.tRP, (uintptr_t)imc.tRAS);
        } else {
            pos = buf_printf(pos, "IMC: %uMHz (%s-%u) CAS %u%s-%u-%u-%u\r\n",
                             (uintptr_t)(imc.freq / 2), imc.type, (uintptr_t)imc.freq,
                             (uintptr_t)imc.tCL, imc.tCL_dec ? ".5" : "",
                             (uintptr_t)imc.tRCD, (uintptr_t)imc.tRP, (uintptr_t)imc.tRAS);
        }
    } else if (ram.freq > 0 && ram.tCL > 0) {
        if (ram.freq <= 166) {
            pos = buf_printf(pos, "RAM: %uMHz (%s PC%u) CAS %u-%u-%u-%u\r\n",
                             (uintptr_t)ram.freq, ram.type, (uintptr_t)ram.freq,
                             (uintptr_t)ram.tCL,
                             (uintptr_t)ram.tRCD, (uintptr_t)ram.tRP, (uintptr_t)ram.tRAS);
        } else {
            pos = buf_printf(pos, "RAM: %uMHz (%s-%u) CAS %u%s-%u-%u-%u\r\n",
                             (uintptr_t)(ram.freq / 2), ram.type, (uintptr_t)ram.freq,
                             (uintptr_t)ram.tCL, ram.tCL_dec ? ".5" : "",
                             (uintptr_t)ram.tRCD, (uintptr_t)ram.tRP, (uintptr_t)ram.tRAS);
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
                         spdi->slot_num, (uintptr_t)spdi->module_size,
                         spdi->type, (uintptr_t)spdi->freq);

        if (spdi->hasECC) {
            pos = buf_printf(pos, " ECC");
        }

        const char *mfg = get_jep106_name(spdi->jedec_code);
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
        if (!test_list[i].enabled) {
            continue;
        }
        const char *start;
        int len = test_desc_text(i, &start);

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
// Serial Log
//------------------------------------------------------------------------------

static char     slog_buf[256];

static uint64_t log_start_time = 0;
static uint64_t next_log_time  = 0;
static bool     log_running    = false;
static bool     log_error_seen = false;

static int      log_test_ticks = 0;
static int      log_pass_ticks = 0;

// Start a log line with the "MTLOG ts=<sec>.<decisec> ev=<event>" prefix.
static char *slog_begin(const char *event)
{
    uintptr_t ds = 0;

    buf_end = slog_buf + sizeof(slog_buf) - 3;  // Reserve space for CR, LF, NUL.

    if (clks_per_msec > 0) {
        ds = (uintptr_t)((get_tsc() - log_start_time) / (100 * (uint64_t)clks_per_msec));
    }
    return buf_printf(slog_buf, "MTLOG ts=%u.%u ev=%s", ds / 10, ds % 10, event);
}

static void slog_end(char *pos)
{
    if (pos > buf_end) pos = buf_end;
    *pos++ = '\r';
    *pos++ = '\n';
    *pos   = '\0';
    tty_send_string(slog_buf);
}

// Emit a complete log line in one call.
static void slog(const char *event, const char *fmt, ...)
{
    char *pos = slog_begin(event);

    va_list args;
    va_start(args, fmt);
    pos = buf_vprintf(pos, fmt, args);
    va_end(args);

    slog_end(pos);
}

static int slog_pct(int ticks, int total)
{
    if (total <= 0) return 0;
    int pct = 100 * ticks / total;
    return pct > 100 ? 100 : pct;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void save_results_to_usb(void)
{
    // The drive may have been plugged in after boot - scan for it now. This must be
    // done before the heap mark below is recorded, so that anything allocated for a
    // newly found drive is not freed when the save completes.
    if (!usb_mass_storage_found) {
        prints(POP_R+14, POP_LI, "Scanning for USB drive...   ");
        (void)usb_scan_for_msd();
    }

    // Save heap state so we can free everything when done.
    uintptr_t heap_lm_mark = heap_mark(HEAP_TYPE_LM_1);

    prints(POP_R+14, POP_LI, "Searching for USB drive...  ");

    // Find a mass storage device.
    usb_msd_t msd;
    if (!find_usb_mass_storage(&msd)) {
        prints(POP_R+14, POP_LI, "No USB drive found.         ");
        usleep(2000 * MILLISEC);
        goto cleanup;
    }

    prints(POP_R+14, POP_LI, "Initializing USB drive...   ");

    if (!msd_init(&msd)) {
        prints(POP_R+14, POP_LI, "USB drive init failed.      ");
        usleep(2000 * MILLISEC);
        goto cleanup;
    }

    // Allocate a sector buffer.
    uintptr_t sec_buf_addr = heap_alloc(HEAP_TYPE_LM_1, msd.block_size, 64);
    if (sec_buf_addr == 0) {
        prints(POP_R+14, POP_LI, "Memory allocation failed.   ");
        usleep(2000 * MILLISEC);
        goto cleanup;
    }

    prints(POP_R+14, POP_LI, "Mounting FAT32 filesystem...");

    fat32_fs_t fs;
    if (!fat32_mount(&fs, &msd, (uint8_t *)sec_buf_addr)) {
        prints(POP_R+14, POP_LI, "No FAT32 filesystem found.  ");
        usleep(2000 * MILLISEC);
        goto cleanup;
    }

    // Generate filename.
    char filename[12];
    if (!fat32_next_filename(&fs, filename)) {
        prints(POP_R+14, POP_LI, "All filename slots full.    ");
        usleep(2000 * MILLISEC);
        goto cleanup;
    }

    // Format results into a buffer.
    uintptr_t res_buf_addr = heap_alloc(HEAP_TYPE_LM_1, RESULTS_BUF_SIZE, 64);
    if (res_buf_addr == 0) {
        prints(POP_R+14, POP_LI, "Memory allocation failed.   ");
        usleep(2000 * MILLISEC);
        goto cleanup;
    }

    char *results_buf = (char *)res_buf_addr;
    int results_len = format_results(results_buf, RESULTS_BUF_SIZE);

    // Build display filename with dot: "MT86P_NN.TXT"
    char display_name[16];
    memcpy(display_name, filename, 8);
    display_name[8] = '.';
    display_name[9] = 'T';
    display_name[10] = 'X';
    display_name[11] = 'T';
    display_name[12] = '\0';

    printf(POP_R+14, POP_LI, "Writing %s...        ", display_name);

    if (!fat32_write_file(&fs, filename, results_buf, results_len)) {
        prints(POP_R+14, POP_LI, "Write failed!               ");
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

void serial_log_start(void)
{
    if (!enable_tty_log) return;

    if (clks_per_msec > 0) {
        log_start_time = get_tsc();
    }

    slog("start", " version=" MT_VERSION "." GIT_HASH " arch=" SLOG_ARCH);
}

void serial_log_run_start(void)
{
    if (!enable_tty_log) return;

#if defined(__i386__) || defined(__x86_64__)
    int year, mon, day, hour, min, sec;
    rtc_get_datetime(&year, &mon, &day, &hour, &min, &sec);
    slog("info", " rtc=\"%04i-%02i-%02i %02i:%02i:%02i\"", year, mon, day, hour, min, sec);
#endif

    if (cpu_model) {
        slog("info", " cpu=\"%s\"", cpu_model);
    }

    const char *board_mfg, *board_prod;
    get_smbios_board_info(&board_mfg, &board_prod);
    if (board_mfg && board_prod) {
        slog("info", " board=\"%s %s\"", board_mfg, board_prod);
    }

    const char *system_serial, *baseboard_serial;
    get_smbios_serial_info(&system_serial, &baseboard_serial);
    if (system_serial) {
        slog("info", " sys_serial=\"%s\"", system_serial);
    }
    if (baseboard_serial) {
        slog("info", " board_serial=\"%s\"", baseboard_serial);
    }

    slog("info", " mem_mb=%u", (uintptr_t)(num_pm_pages / 256));

    if (imc.freq) {
        slog("info", " imc=%s-%u cas=%u%s-%u-%u-%u",
             imc.type, (uintptr_t)imc.freq, (uintptr_t)imc.tCL, imc.tCL_dec ? ".5" : "",
             (uintptr_t)imc.tRCD, (uintptr_t)imc.tRP, (uintptr_t)imc.tRAS);
    } else if (ram.freq > 0 && ram.tCL > 0) {
        slog("info", " ram=%s-%u cas=%u%s-%u-%u-%u",
             ram.type, (uintptr_t)ram.freq, (uintptr_t)ram.tCL, ram.tCL_dec ? ".5" : "",
             (uintptr_t)ram.tRCD, (uintptr_t)ram.tRP, (uintptr_t)ram.tRAS);
    }

    for (int i = 0; i < MAX_SPD_SLOT; i++) {
        const spd_info *spdi = &spd_slot_cache[i];
        if (!spdi->isValid) continue;

        char *pos = slog_begin("spd");
        pos = buf_printf(pos, " slot=%i size_mb=%u type=%s freq=%u ecc=%i",
                         spdi->slot_num, (uintptr_t)spdi->module_size,
                         spdi->type, (uintptr_t)spdi->freq, spdi->hasECC ? 1 : 0);

        const char *mfg = get_jep106_name(spdi->jedec_code);
        if (mfg) {
            pos = buf_printf(pos, " mfg=\"%s\"", mfg);
        } else if (spdi->jedec_code != 0) {
            pos = buf_printf(pos, " mfg_id=0x%x", (uintptr_t)spdi->jedec_code);
        }

        if (spdi->sku[0]) {
            pos = buf_printf(pos, " sku=\"%s\"", spdi->sku);
        }
        slog_end(pos);
    }

    log_running = true;
}

void serial_log_event(slog_event_t event)
{
    if (!log_running) return;

    switch (event) {
      case SLOG_PASS_START:
        log_pass_ticks = 0;
        slog("pass_start", " pass=%i", pass_num);
        break;
      case SLOG_PASS_END:
        slog("pass_end", " pass=%i errors=%u status=%s",
             pass_num, (uintptr_t)error_count, error_count == 0 ? "pass" : "fail");
        // Hooked before main() increments pass_num, so completed passes = pass_num + 1.
        if (log_max_passes > 0 && pass_num + 1 >= log_max_passes) {
            slog("done", " passes=%i status=%s", pass_num + 1, error_count == 0 ? "pass" : "fail");
            usleep(100 * MILLISEC);  // Let the UART drain the last line.
            reboot();
        }
        break;
      case SLOG_TEST_START: {
        log_error_seen = false;
        log_test_ticks = 0;
        const char *text;
        int len = test_desc_text(test_num, &text);
        char name[40];
        memcpy(name, text, len);
        name[len] = '\0';
        slog("test_start", " test=%i name=\"%s\"", test_num, name);
        break;
      }
      case SLOG_TEST_END:
        slog("test_end", " test=%i errors=%i", test_num, test_list[test_num].errors);
        break;
    }
}

void serial_log_tick(void)
{
    if (!log_running || clks_per_msec == 0) return;

    log_test_ticks++;
    log_pass_ticks++;

    uint64_t now = get_tsc();
    if (now < next_log_time) return;
    next_log_time = now + SERIAL_LOG_INTERVAL * 1000 * (uint64_t)clks_per_msec;

    // Same progress calculation as do_tick(), from our own tick counters.
    pass_type_t pass_type = (pass_num == 0) ? FAST_PASS : FULL_PASS;

    slog("tick", " pass=%i pass_pct=%i test=%i test_pct=%i errors=%u",
         pass_num, slog_pct(log_pass_ticks, ticks_per_pass[pass_type]),
         test_num, slog_pct(log_test_ticks, ticks_per_test[pass_type][test_num]),
         (uintptr_t)error_count);
}

void serial_log_error(const char *type, testword_t page, testword_t offset, testword_t expect, testword_t actual)
{
    if (!log_running || log_error_seen) return;

    log_error_seen = true;

    slog("error", " type=%s test=%i cpu=%i addr=0x%x%03x expect=0x%x actual=0x%x",
         type, test_num, smp_my_cpu_num(),
         (uintptr_t)page, (uintptr_t)offset, (uintptr_t)expect, (uintptr_t)actual);
}
