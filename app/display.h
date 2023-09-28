// SPDX-License-Identifier: GPL-2.0
#ifndef DISPLAY_H
#define DISPLAY_H
/**
 * \file
 *
 * Provides (macro) functions that implement the UI display.
 * All knowledge about the display layout is encapsulated here.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 * Copyright (C) 2004-2023 Sam Demeulemeester.
 */

#include <stdbool.h>

#include "screen.h"

#include "print.h"
#include "string.h"

#include "test.h"

#define ROW_SPD         13

#define ROW_MESSAGE_T   10
#define ROW_MESSAGE_B   (SCREEN_HEIGHT - 2)

#define ROW_SCROLL_T    (ROW_MESSAGE_T + 2)
#define ROW_SCROLL_B    (SCREEN_HEIGHT - 2)

#define ROW_FOOTER      (SCREEN_HEIGHT - 1)

#define BAR_LENGTH      40

#define ERROR_LIMIT     UINT64_C(999999999999)

typedef enum {
    DISPLAY_MODE_NA,
    DISPLAY_MODE_SPD,
    DISPLAY_MODE_IMC
} display_mode_t;

#define display_cpu_model(str) \
    prints(0, 30, str)

#define display_cpu_clk(freq) \
    printf(1, 10, "%iMHz", freq)

#define display_cpu_addr_mode(str) \
    prints(4, 75, str)

#define display_l1_cache_size(size) \
    printf(2, 9, "%6kB", (uintptr_t)(size))

#define display_l2_cache_size(size) \
    printf(3, 9, "%6kB", (uintptr_t)(size))

#define display_l3_cache_size(size) \
    printf(4, 9, "%6kB", (uintptr_t)(size))

#define display_memory_size(size) \
    printf(5, 9, "%6kB", (uintptr_t)(size))

#define display_l1_cache_speed(speed) \
    printf(2, 18, "%S6kB/s", (uintptr_t)(speed))

#define display_l2_cache_speed(speed) \
    printf(3, 18, "%S6kB/s", (uintptr_t)(speed))

#define display_l3_cache_speed(speed) \
    printf(4, 18, "%S6kB/s", (uintptr_t)(speed))

#define display_ram_speed(speed) \
    printf(5, 18, "%S6kB/s", (uintptr_t)(speed))

#define display_status(status) \
    prints(7, 68, status)

#define display_threading(nb, mode) \
    printf(7,31, "%uT (%s)", nb, mode)

#define display_threading_disabled() \
    prints(7,31, "Disabled")

#define display_cpu_topo_hybrid(num_pcores, num_ecores, num_threads) \
    { \
        clear_screen_region(7, 5, 7, 25); \
        printf(7, 5, "%uP+%uE-Cores (%uT)", num_pcores, num_ecores, num_threads); \
    }

#define display_cpu_topo_hybrid_short(num_threads) \
    printf(7, 5, "%u Threads (Hybrid)", num_threads)

#define display_cpu_topo_multi_socket(num_sockets, num_cores, num_threads) \
    printf(7, 5, "%uS / %uC / %uT", num_sockets, num_cores, num_threads)

#define display_cpu_topo( num_cores, num_threads) \
    printf(7, 5, "%u Cores %u Threads", num_cores, num_threads)

#define display_cpu_topo_short( num_cores, num_threads) \
    printf(7, 5, "%u Cores (%uT)",  num_cores, num_threads)

#define display_spec_mode(mode) \
    prints(8,0, mode);

#define display_spec_ddr5(freq, type, cl, cl_dec, rcd, rp, ras) \
    printf(8,5, "%s-%u / CAS %u%s-%u-%u-%u", \
                type, freq, cl, cl_dec?".5":"", rcd, rp, ras);

#define display_spec_ddr(freq, type, cl, cl_dec, rcd, rp, ras) \
    printf(8,5, "%uMHz (%s-%u) CAS %u%s-%u-%u-%u", \
                freq / 2, type, freq, cl, cl_dec?".5":"", rcd, rp, ras);

#define display_spec_sdr(freq, type, cl, rcd, rp, ras) \
    printf(8,5, "%uMHz (%s PC%u) CAS %u-%u-%u-%u", \
                freq, type, freq, cl, rcd, rp, ras);

#define display_dmi_mb(sys_ma, sys_sku) \
    dmicol = prints(23, dmicol, sys_man); \
    prints(23, dmicol + 1, sys_sku);

#define display_active_cpu(cpu_num) \
    prints(8, 7, "Core #"); \
    printi(8, 13, cpu_num, 3, false, true)

#define display_all_active() \
    prints(8, 7, "All Cores")

#define display_spinner(spin_state) \
    printc(7, 77, spin_state)

#define display_pass_percentage(pct) \
    printi(1, 34, pct, 3, false, false)

#define display_pass_bar(length) \
    while (length > pass_bar_length) {          \
        printc(1, 39 + pass_bar_length, '#');   \
        pass_bar_length++;                      \
    }

#define display_test_percentage(pct) \
    printi(2, 34, pct, 3, false, false)

#define display_test_bar(length) \
    while (length > test_bar_length) {          \
        printc(2, 39 + test_bar_length, '#');   \
        test_bar_length++;                      \
    }

#define display_test_number(number) \
    printi(3, 36, number, 2, false, true)

#define display_test_description(str) \
    prints(3, 39, str)

#define display_test_addresses(pb, pe, total) \
    { \
        clear_screen_region(4, 39, 4, SCREEN_WIDTH - 6); \
        printf(4, 39, "%kB - %kB [%kB of %kB]", pb, pe, (pe) - (pb), total); \
    }

#define display_test_stage_description(...) \
    { \
        clear_screen_region(4, 39, 4, SCREEN_WIDTH - 6); \
        printf(4, 39, __VA_ARGS__); \
    }

#define display_test_pattern_name(str) \
    { \
        clear_screen_region(5, 39, 5, SCREEN_WIDTH - 1); \
        prints(5, 39, str); \
    }

#define display_test_pattern_names(str, step) \
    { \
        clear_screen_region(5, 39, 5, SCREEN_WIDTH - 1); \
        printf(5, 39, "%s - %i", str, step); \
    }

#define display_test_pattern_value(pattern) \
    { \
        clear_screen_region(5, 39, 5, SCREEN_WIDTH - 1); \
        printf(5, 39, "0x%0*x", TESTWORD_DIGITS, pattern); \
    }

#define display_test_pattern_values(pattern, offset) \
    { \
        clear_screen_region(5, 39, 5, SCREEN_WIDTH - 1); \
        printf(5, 39, "0x%0*x - %i", TESTWORD_DIGITS, pattern, offset); \
    }

#define display_run_time(hours, mins, secs) \
    printf(7, 51, "%i:%02i:%02i", hours, mins, secs)

#define display_pass_count(count) \
    printi(8, 51, count, 0, false, true)

#define display_err_count_without_ecc(count) \
    printi(8, 68, count, 0, false, true)

#define display_err_count_with_ecc(count_err, count_ecc) \
    { \
        printi(8, 62, count_err, 0, false, true); \
        printi(8, 74, count_ecc, 0, false, true); \
    }

#define clear_message_area() \
    { \
        clear_screen_region(ROW_MESSAGE_T, 0, ROW_MESSAGE_B, SCREEN_WIDTH - 1); \
        scroll_message_row = ROW_SCROLL_T - 1; \
    }

#define display_pinned_message(row, col, ...) \
    printf(ROW_MESSAGE_T + row, col, __VA_ARGS__)

#define display_scrolled_message(col, ...) \
    printf(scroll_message_row, col, __VA_ARGS__)

#define display_notice(str) \
    prints(ROW_MESSAGE_T + 8, (SCREEN_WIDTH - strlen(str)) / 2, str)

#define display_notice_with_args(length, ...) \
    printf(ROW_MESSAGE_T + 8, (SCREEN_WIDTH - length) / 2, __VA_ARGS__)

#define clear_footer_message() \
    { \
        set_background_colour(WHITE); \
        clear_screen_region(ROW_FOOTER, 56, ROW_FOOTER, SCREEN_WIDTH - 1); \
        set_background_colour(BLUE);  \
    }

#define display_footer_message(str) \
    { \
        set_foreground_colour(BLUE);  \
        prints(ROW_FOOTER, 56, str);  \
        set_foreground_colour(WHITE); \
    }

#define trace(my_cpu, ...) \
    if (enable_trace) do_trace(my_cpu, __VA_ARGS__)

#define display_msr_failed_flag() \
    printc(0, SCREEN_WIDTH - 1, '*');

extern int scroll_message_row;

extern display_mode_t display_mode;

void display_init(void);

void display_cpu_topology(void);

void post_display_init(void);

void display_start_run(void);

void display_start_pass(void);

void display_start_test(void);

void display_error_count(void);

void display_temperature(void);

void display_big_status(bool pass);

void restore_big_status(void);

void check_input(void);

void set_scroll_lock(bool enabled);

void toggle_scroll_lock(void);

void scroll(void);

void do_tick(int my_cpu);

void do_trace(int my_cpu, const char *fmt, ...);

#endif // DISPLAY_H
