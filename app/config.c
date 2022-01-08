// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2021 Martin Whitaker.
//
// Derived from memtest86+ config.c:
//
// MemTest86+ V5.00 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.x86-secret.com - http://www.memtest.org
// ----------------------------------------------------
// config.c - MemTest-86  Version 3.4
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"
#include "bootparams.h"

#include "cpuinfo.h"
#include "hwctrl.h"
#include "keyboard.h"
#include "memsize.h"
#include "pmem.h"
#include "screen.h"
#include "smp.h"
#include "usbhcd.h"

#include "read.h"
#include "print.h"
#include "string.h"
#include "unistd.h"

#include "display.h"
#include "test.h"

#include "tests.h"

#include "config.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// Origin and size of the pop-up window.

#define POP_R    4
#define POP_C    22

#define POP_W    36
#define POP_H    16

#define POP_REGION  POP_R, POP_C, POP_R + POP_H - 1, POP_C + POP_W - 1

static const char *cpu_mode_str[] = { "PAR", "SEQ", "RR " };

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static uint16_t popup_save_buffer[POP_W * POP_H];

static bool smp_enabled = false;

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

uintptr_t       pm_limit_lower = 0;
uintptr_t       pm_limit_upper = 0;

uintptr_t       num_pages_to_test = 0;

cpu_mode_t      cpu_mode = PAR;

error_mode_t    error_mode = ERROR_MODE_NONE;

bool            enable_pcpu[MAX_PCPUS];

bool            enable_temperature = false;
bool            enable_trace       = false;

bool            pause_at_start     = true;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static void parse_option(const char *option, const char *params)
{
    if (option[0] == '\0') return;

    if (strncmp(option, "keyboard", 9) == 0 && params != NULL) {
        if (strncmp(params, "legacy", 7) == 0) {
            keyboard_types = KT_LEGACY;
        } else if (strncmp(params, "usb", 4) == 0) {
            keyboard_types = KT_USB;
        } else if (strncmp(params, "buggy-usb", 10) == 0) {
            keyboard_types = KT_USB;
            usb_init_options = USB_EXTRA_RESET;
        }
    } else if (strncmp(option, "nopause", 8) == 0) {
        pause_at_start = false;
    } else if (strncmp(option, "smp", 4) == 0) {
        smp_enabled = true;
    }
}

static void parse_command_line(char *cmd_line, int cmd_line_size)
{
    const char *option = cmd_line;
    const char *params = NULL;
    for (int i = 0; i < cmd_line_size; i++) {
        switch (cmd_line[i]) {
          case '\0':
            parse_option(option, params);
            return;
          case ' ':
            cmd_line[i] = '\0';
            parse_option(option, params);
            option = &cmd_line[i+1];
            params = NULL;
            break;
          case '=':
            cmd_line[i] = '\0';
            params = &cmd_line[i+1];
            break;
          default:
            break;
        }
    }
}

static void display_initial_notice(void)
{
    if (smp_enabled) {
        display_notice("Press <F1> to configure, <F2> to disable SMP, <Enter> to start testing");
    } else {
        display_notice("Press <F1> to configure, <F2> to enable SMP, <Enter> to start testing ");
    }
}

static void update_num_pages_to_test(void)
{
    num_pages_to_test = 0;
    for (int i = 0; i < pm_map_size; i++) {
        if (pm_map[i].start >= pm_limit_lower && pm_map[i].end <= pm_limit_upper) {
            num_pages_to_test += pm_map[i].end - pm_map[i].start;
            continue;
        }
        if (pm_map[i].start < pm_limit_lower) {
            if (pm_map[i].end < pm_limit_lower) {
                continue;
            }
            if (pm_map[i].end > pm_limit_upper) {
                num_pages_to_test += pm_limit_upper - pm_limit_lower;
            } else {
                num_pages_to_test += pm_map[i].end - pm_limit_lower;
            }
            continue;
        }
        if (pm_map[i].end > pm_limit_upper) {
            if (pm_map[i].start > pm_limit_upper) {
                continue;
            }
            num_pages_to_test += pm_limit_upper - pm_map[i].start;
        }
    }
}

static void clear_popup_row(int row)
{
    clear_screen_region(row, POP_C, row, POP_C + POP_W - 1);
}

static void display_input_message(int row, const char *message)
{
    clear_popup_row(row);
    prints(row, POP_C+2, message);
}

static void display_error_message(int row, const char *message)
{
    clear_popup_row(row);
    set_foreground_colour(YELLOW);
    prints(row, POP_C+2, message);
    set_foreground_colour(WHITE);
}

static void display_selection_header(int row, int max_num)
{
    prints(row+0, POP_C+2, "Current selection:");
    printc(row+1, POP_C+2, '0');
    for (int i = 1; i < max_num; i++) {
        printc(row+1, POP_C+2+i, i % 10 ? 0xc4 : 0xc3);
    }
    printi(row+1, POP_C+2+max_num, max_num, 2, false, true);
}

static void display_enabled(int row, int n, bool enabled)
{
    printc(row, POP_C+2+n, enabled ? '*' : '.');
}

static bool set_all_tests(bool enabled)
{
    clear_popup_row(POP_R+14);
    for (int i = 0; i < NUM_TEST_PATTERNS; i++) {
        test_list[i].enabled = enabled;
        display_enabled(POP_R+12, i, enabled);
    }
    return true;
}

static bool add_or_remove_test(bool add)
{
    
    display_input_message(POP_R+14, "Enter test #");
    int n = read_value(POP_R+14, POP_C+2+12, 2, 0);
    if (n < 0 || n >= NUM_TEST_PATTERNS) {
        display_error_message(POP_R+14, "Invalid test number");
        return false;
    }
    test_list[n].enabled = add;
    display_enabled(POP_R+12, n, add);
    clear_popup_row(POP_R+14);
    return true;
}

static bool add_test_range()
{
    display_input_message(POP_R+14, "Enter first test #");
    int n1 = read_value(POP_R+14, POP_C+2+18, 2, 0);
    if (n1 < 0 || n1 >= NUM_TEST_PATTERNS) {
        display_error_message(POP_R+14, "Invalid test number");
        return false;
    }
    display_input_message(POP_R+14, "Enter last test #");
    int n2 = read_value(POP_R+14, POP_C+2+17, 2, 0);
    if (n2 < n1 || n2 >= NUM_TEST_PATTERNS) {
        display_error_message(POP_R+14, "Invalid test range");
        return false;
    }
    for (int i = n1; i <= n2; i++) {
        test_list[i].enabled = true;
        display_enabled(POP_R+12, i, true);
    }
    clear_popup_row(POP_R+14);
    return true;
}

static void test_selection_menu(void)
{
    clear_screen_region(POP_REGION);
    prints(POP_R+1, POP_C+2, "Test Selection:");
    prints(POP_R+3, POP_C+4, "<F1>  Clear selection");
    prints(POP_R+4, POP_C+4, "<F2>  Remove one test");
    prints(POP_R+5, POP_C+4, "<F3>  Add one test");
    prints(POP_R+6, POP_C+4, "<F4>  Add test range");
    prints(POP_R+7, POP_C+4, "<F5>  Add all tests");
    prints(POP_R+8, POP_C+4, "<F10> Exit menu");

    display_selection_header(POP_R+10, NUM_TEST_PATTERNS - 1);
    for (int i = 0; i < NUM_TEST_PATTERNS; i++) {
        display_enabled(POP_R+12, i, test_list[i].enabled);
    }

    bool exit_menu = false;
    while (!exit_menu) {
        bool changed = false;
        switch (get_key()) {
          case '1':
            changed = set_all_tests(false);
            break;
          case '2':
            changed = add_or_remove_test(false);
            break;
          case '3':
            changed = add_or_remove_test(true);
            break;
          case '4':
            changed = add_test_range();
            break;
          case '5':
            changed = set_all_tests(true);
            break;
          case '0': {
            clear_popup_row(POP_R+14);
            int num_selected = 0;
            for (int i = 0; i < NUM_TEST_PATTERNS; i++) {
                if (test_list[i].enabled) {
                    num_selected++;
                }
            }
            if (num_selected > 0) {
                exit_menu = true;
            } else {
                display_error_message(POP_R+14, "You must select at least one test");
            }
          } break;
          default:
            usleep(1000);
            break;
        }
        if (changed) {
            restart = true;
            changed = false;
        }
    }
    clear_screen_region(POP_REGION);
}

static void address_range_menu(void)
{
    clear_screen_region(POP_REGION);
    prints(POP_R+1, POP_C+2, "Address Range:");
    prints(POP_R+3, POP_C+4, "<F1>  Set lower limit");
    prints(POP_R+4, POP_C+4, "<F2>  Set upper limit");
    prints(POP_R+5, POP_C+4, "<F3>  Test all memory");
    prints(POP_R+6, POP_C+4, "<F10> Exit menu");
    printf(POP_R+8, POP_C+2, "Current range: %kB - %kB", pm_limit_lower << 2, pm_limit_upper << 2);

    bool exit_menu = false;
    while (!exit_menu) {
        bool changed = false;
        switch (get_key()) {
          case '1': {
            display_input_message(POP_R+10, "Enter lower limit: ");
            uintptr_t page = read_value(POP_R+10, POP_C+2+19, 15, -PAGE_SHIFT);
            if (page < pm_limit_upper) {
                clear_popup_row(POP_R+10);
                pm_limit_lower = page;
                changed = true;
            } else {
                display_error_message(POP_R+10, "Lower must be less than upper");
            }
          } break;
          case '2': {
            display_input_message(POP_R+10, "Enter upper limit: ");
            uintptr_t page = read_value(POP_R+10, POP_C+2+19, 15, -PAGE_SHIFT);
            if (page > pm_limit_lower) {
                clear_popup_row(POP_R+10);
                pm_limit_upper = page;
                changed = true;
            } else {
                display_error_message(POP_R+10, "Upper must be greater than lower");
            }
          } break;
          case '3':
            clear_popup_row(POP_R+10);
            pm_limit_lower = 0;
            pm_limit_upper = pm_map[pm_map_size - 1].end;
            changed = true;
            break;
          case '0':
            exit_menu = true;
            break;
          default:
            usleep(1000);
            break;
        }
        if (changed) {
            clear_popup_row(POP_R+8);
            printf(POP_R+8, POP_C+2, "Current range: %kB - %kB", pm_limit_lower << 2, pm_limit_upper << 2);
            update_num_pages_to_test();
            restart = true;
            changed = false;
        }
    }

    clear_screen_region(POP_REGION);
}

static void cpu_mode_menu(void)
{
    clear_screen_region(POP_REGION);
    prints(POP_R+1, POP_C+2, "CPU Sequencing Mode:");
    prints(POP_R+3, POP_C+4, "<F1>  Parallel    (All)");
    prints(POP_R+4, POP_C+4, "<F2>  Sequential  (Seq)");
    prints(POP_R+5, POP_C+4, "<F3>  Round robin (RR)");
    prints(POP_R+6, POP_C+4, "<F10> Exit menu");
    printc(POP_R+3+cpu_mode, POP_C+2, '*');

    bool exit_menu = false;
    while (!exit_menu) {
        int ch = get_key();
        switch (ch) {
          case '1':
          case '2':
          case '3':
            printc(POP_R+3+cpu_mode, POP_C+2, ' ');
            cpu_mode = ch - '1';
            printc(POP_R+3+cpu_mode, POP_C+2, '*');
            break;
          case '0':
            exit_menu = true;
            break;
          default:
            usleep(1000);
            break;
        }
    }

    clear_screen_region(POP_REGION);
}

static void error_mode_menu(void)
{
    clear_screen_region(POP_REGION);
    prints(POP_R+1, POP_C+2, "Error Reporting Mode:");
    prints(POP_R+3, POP_C+4, "<F1>  Error counts only");
    prints(POP_R+4, POP_C+4, "<F2>  Error summary");
    prints(POP_R+5, POP_C+4, "<F3>  Individual errors");
    prints(POP_R+6, POP_C+4, "<F4>  BadRAM patterns");
    prints(POP_R+7, POP_C+4, "<F10> Exit menu");
    printc(POP_R+3+error_mode, POP_C+2, '*');

    bool exit_menu = false;
    while (!exit_menu) {
        int ch = get_key();
        switch (ch) {
          case '1':
          case '2':
          case '3':
          case '4':
            printc(POP_R+3+error_mode, POP_C+2, ' ');
            error_mode = ch - '1';
            printc(POP_R+3+error_mode, POP_C+2, '*');
            break;
          case '0':
            exit_menu = true;
            break;
          default:
            usleep(1000);
            break;
        }
    }

    clear_screen_region(POP_REGION);
}

static bool set_all_cpus(bool enabled)
{
    clear_popup_row(POP_R+14);
    for (int i = 1; i < num_pcpus; i++) {
        enable_pcpu[i] = enabled;
        display_enabled(POP_R+12, i, enabled);
    }
    return true;
}

static bool add_or_remove_cpu(bool add)
{
    
    display_input_message(POP_R+14, "Enter CPU #");
    int n = read_value(POP_R+14, POP_C+2+11, 2, 0);
    if (n < 1 || n >= num_pcpus) {
        display_error_message(POP_R+14, "Invalid CPU number");
        return false;
    }
    enable_pcpu[n] = add;
    display_enabled(POP_R+12, n, add);
    clear_popup_row(POP_R+14);
    return true;
}

static bool add_cpu_range()
{
    display_input_message(POP_R+14, "Enter first CPU #");
    int n1 = read_value(POP_R+14, POP_C+2+17, 2, 0);
    if (n1 < 1 || n1 >= num_pcpus) {
        display_error_message(POP_R+14, "Invalid CPU number");
        return false;
    }
    display_input_message(POP_R+14, "Enter last CPU #");
    int n2 = read_value(POP_R+14, POP_C+2+16, 2, 0);
    if (n2 < n1 || n2 >= num_pcpus) {
        display_error_message(POP_R+14, "Invalid CPU range");
        return false;
    }
    for (int i = n1; i <= n2; i++) {
        enable_pcpu[i] = true;
        display_enabled(POP_R+12, i, true);
    }
    clear_popup_row(POP_R+14);
    return true;
}

static void cpu_selection_menu(void)
{
    clear_screen_region(POP_REGION);
    prints(POP_R+1, POP_C+2, "CPU Selection:");
    prints(POP_R+3, POP_C+4, "<F1>  Clear selection");
    prints(POP_R+4, POP_C+4, "<F2>  Remove one CPU");
    prints(POP_R+5, POP_C+4, "<F3>  Add one CPU");
    prints(POP_R+6, POP_C+4, "<F4>  Add CPU range");
    prints(POP_R+7, POP_C+4, "<F5>  Add all CPUs");
    prints(POP_R+8, POP_C+4, "<F10> Exit menu");

    display_selection_header(POP_R+10, num_pcpus - 1);
    printc(POP_R+12, POP_C+2, 'B');
    for (int i = 1; i < num_pcpus; i++) {
        display_enabled(POP_R+12, i, enable_pcpu[i]);
    }

    bool exit_menu = false;
    while (!exit_menu) {
        bool changed = false;
        switch (get_key()) {
          case '1':
            changed = set_all_cpus(false);
            break;
          case '2':
            changed = add_or_remove_cpu(false);
            break;
          case '3':
            changed = add_or_remove_cpu(true);
            break;
          case '4':
            changed = add_cpu_range();
            break;
          case '5':
            changed = set_all_cpus(true);
            break;
          case '0':
            clear_popup_row(POP_R+14);
            exit_menu = true;
            break;
          default:
            usleep(1000);
            break;
        }
        if (changed) {
            restart = true;
            changed = false;
        }
    }
    clear_screen_region(POP_REGION);
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void config_init(void)
{
    pm_limit_lower = 0;
    pm_limit_upper = pm_map[pm_map_size - 1].end;

    update_num_pages_to_test();

    cpu_mode = PAR;

    error_mode = ERROR_MODE_ADDRESS;

    for (int i = 0; i < MAX_PCPUS; i++) {
        enable_pcpu[i] = true;
    }

    enable_temperature = !no_temperature;

    const boot_params_t *boot_params = (boot_params_t *)boot_params_addr;

    uintptr_t cmd_line_addr = boot_params->cmd_line_ptr;
    if (cmd_line_addr != 0) {
        int cmd_line_size = boot_params->cmd_line_size;
        if (cmd_line_size == 0) cmd_line_size = 255;
        parse_command_line((char *)cmd_line_addr, cmd_line_size);
    }
}

void config_menu(bool initial)
{
    save_screen_region(POP_REGION, popup_save_buffer);
    set_background_colour(BLACK);
    clear_screen_region(POP_REGION);

    cpu_mode_t   old_cpu_mode   = cpu_mode;

    bool exit_menu = false;
    while (!exit_menu) {
        prints(POP_R+1,  POP_C+2, "Settings:");
        prints(POP_R+3,  POP_C+4, "<F1>  Test selection");
        prints(POP_R+4,  POP_C+4, "<F2>  Address range");
        prints(POP_R+5,  POP_C+4, "<F3>  CPU sequencing mode");
        prints(POP_R+6,  POP_C+4, "<F4>  Error reporting mode");
        if (initial) {
            if (num_pcpus < 2)  set_foreground_colour(BOLD+BLACK);
            prints(POP_R+7,  POP_C+4, "<F5>  CPU selection");
            if (num_pcpus < 2)  set_foreground_colour(WHITE);
            if (no_temperature) set_foreground_colour(BOLD+BLACK);
            printf(POP_R+8,  POP_C+4, "<F6>  Temperature %s", enable_temperature ? "disable" : "enable ");
            if (no_temperature) set_foreground_colour(WHITE);
            printf(POP_R+9,  POP_C+4, "<F7>  Boot trace %s",  enable_trace  ? "disable" : "enable ");
            prints(POP_R+10, POP_C+4, "<F10> Exit menu");
        } else {
            prints(POP_R+7,  POP_C+4, "<F5>  Skip current test");
            prints(POP_R+8 , POP_C+4, "<F10> Exit menu");
        }

        switch (get_key()) {
          case '1':
            test_selection_menu();
            break;
          case '2':
            address_range_menu();
            break;
          case '3':
            cpu_mode_menu();
            break;
          case '4':
            error_mode_menu();
            break;
          case '5':
            if (initial) {
                if (num_pcpus > 1) {
                    cpu_selection_menu();
                }
            } else {
                bail = true;
            }
            break;
          case '6':
            if (initial) {
                if (!no_temperature) {
                    enable_temperature = !enable_temperature;
                }
            }
            break;
          case '7':
            if (initial) {
                enable_trace = !enable_trace;
            }
            break;
          case '0':
            exit_menu = true;
            break;
          default:
            usleep(1000);
            break;
        }
    }

    restore_screen_region(POP_REGION, popup_save_buffer);
    set_background_colour(BLUE);

    if (cpu_mode != old_cpu_mode) {
        display_cpu_mode(cpu_mode_str[cpu_mode]);
        restart = true;
    }

    if (restart) {
        bail = true;
    }
}

void initial_config(void)
{
    display_initial_notice();

    bool smp_init_done = false;
    if (pause_at_start) {
        bool got_key = false;
        for (int i = 0; i < 5000 && !got_key; i++) {
            usleep(1000);
            switch (get_key()) {
              case ESC:
                clear_message_area();
                display_notice("Rebooting...");
                reboot();
                break;
              case '1':
                smp_init(smp_enabled);
                smp_init_done = true;
                config_menu(true);
                got_key = true;
                break;
              case '2':
                smp_enabled = !smp_enabled;
                display_initial_notice();
                i = 0;
                break;
              case ' ':
                toggle_scroll_lock();
                break;
              case '\n':
                got_key = true;
                break;
              default:
                break;
            }
        }
    }
    if (!smp_init_done) {
        smp_init(smp_enabled);
    }
}
