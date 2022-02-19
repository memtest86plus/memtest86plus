// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from memtest86+ error.c
//
// error.c - MemTest-86  Version 4.1
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include <limits.h>

#include "smp.h"
#include "vmem.h"

#include "badram.h"
#include "config.h"
#include "display.h"
#include "test.h"

#include "tests.h"

#include "error.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#ifndef USB_WORKAROUND
#define USB_WORKAROUND 1
#endif

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef enum { ADDR_ERROR, DATA_ERROR, PARITY_ERROR, NEW_MODE } error_type_t;

typedef struct {
    uintptr_t           page;
    uintptr_t           offset;
} page_offs_t;

typedef struct {
    page_offs_t         min_addr;
    page_offs_t         max_addr;
    testword_t          bad_bits;
    int                 min_bits;
    int                 max_bits;
    uint64_t            total_bits;
    uintptr_t           run_length;
    uintptr_t           max_run;
    uintptr_t           last_addr;
    testword_t          last_xor;
} error_info_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static error_mode_t     last_error_mode = ERROR_MODE_NONE;

static error_info_t     error_info;

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

uint64_t                error_count = 0;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static bool update_error_info(uintptr_t addr, testword_t xor)
{
    bool update_stats = false;

    // Update address range.

    testword_t page   = page_of((void *)addr);
    testword_t offset = addr & 0xFFF;

    if (error_info.min_addr.page > page) {
        error_info.min_addr.page   = page;
        error_info.min_addr.offset = offset;
        update_stats = true;
    } else if (error_info.min_addr.page == page && error_info.min_addr.offset > offset) {
        error_info.min_addr.offset = offset;
        update_stats = true;
    }
    if (error_info.max_addr.page < page) {
        error_info.max_addr.page   = page;
        error_info.max_addr.offset = offset;
        update_stats = true;
    } else if (error_info.max_addr.page == page && error_info.max_addr.offset < offset) {
        error_info.max_addr.offset = offset;
        update_stats = true;
    }

    // Update bits in error.

    int bits = 0;
    for (int i = 0; i < TESTWORD_WIDTH; i++) {
        if ((xor >> i) & 1) {
            bits++;
        }
    }
    if (bits > 0 && error_count < ERROR_LIMIT) {
        error_info.total_bits += bits;
    }
    if (bits > error_info.max_bits) {
        error_info.max_bits = bits;
        update_stats = true;
    }
    if (bits < error_info.min_bits) {
        error_info.min_bits = bits;
        update_stats = true;
    }
    if (error_info.bad_bits ^ xor) {
        update_stats = true;
    }
    error_info.bad_bits |= xor;

    // Update max contiguous range.

    if (error_info.max_run > 0) {
        if (addr == error_info.last_addr + sizeof(testword_t)
        ||  addr == error_info.last_addr - sizeof(testword_t)) {
            error_info.run_length++;
        } else {
            error_info.run_length = 1;
        }
    } else {
        error_info.run_length = 1;
    }
    if (error_info.run_length > error_info.max_run) {
        error_info.max_run = error_info.run_length;
        update_stats = true;
    }

    return update_stats;
}

static void common_err(error_type_t type, uintptr_t addr, testword_t good, testword_t bad, bool use_for_badram)
{
    spin_lock(error_mutex);

    bool new_header = (error_count == 0) || (error_mode != last_error_mode);
    if (new_header) {
        clear_message_area();
        badram_init();
    }
    last_error_mode = error_mode;

    testword_t xor = good ^ bad;

    bool new_stats = false;
    switch (type) {
      case ADDR_ERROR:
        new_stats = update_error_info(addr, 0);
        break;
      case DATA_ERROR:
        new_stats = update_error_info(addr, xor);
        break;
      case NEW_MODE:
        new_stats = (error_count > 0);
      default:
        break;
    }

    bool new_address = (type != NEW_MODE);

    bool new_badram = false;
    if (error_mode == ERROR_MODE_BADRAM && use_for_badram) {
        new_badram = badram_insert(addr);
    }

    if (new_address) {
        if (error_count < ERROR_LIMIT) {
            error_count++;
        }
        if (test_list[test_num].errors < INT_MAX) {
            test_list[test_num].errors++;
        }
    }

    switch (error_mode) {
      case ERROR_MODE_SUMMARY:
        if (type == PARITY_ERROR) {
            break;
        }
        if (new_header) {
            display_pinned_message(0, 1,  "  Lowest Error Address:");
            display_pinned_message(1, 1,  " Highest Error Address:");
            display_pinned_message(2, 1,  "    Bits in Error Mask:");
            display_pinned_message(3, 1,  " Bits in Error - Total:");
            display_pinned_message(4, 1,  " Max Contiguous Errors:");

            display_pinned_message(0, 64, "Test  Errors");
            for (int i = 0; i < NUM_TEST_PATTERNS; i++) {
                display_pinned_message(1 + i, 65, "%2i:", i);
            }

        }
        if (new_stats) {
            int bits = 0;
            for (int i = 0; i < TESTWORD_WIDTH; i++) {
                if (error_info.bad_bits >> i & 1) {
                    bits++;
                }
            }
            display_pinned_message(0, 25, "%09x%03x (%kB)", 
                                          error_info.min_addr.page,
                                          error_info.min_addr.offset,
                                          error_info.min_addr.page << 2);
            display_pinned_message(1, 25, "%09x%03x (%kB)",
                                          error_info.max_addr.page,
                                          error_info.max_addr.offset,
                                          error_info.max_addr.page << 2);
            display_pinned_message(2, 25, "%0*x", TESTWORD_DIGITS,
                                          error_info.bad_bits);
            display_pinned_message(3, 25, " %2i Min: %2i Max: %2i Avg: %2i",
                                          bits,
                                          error_info.min_bits,
                                          error_info.max_bits,
                                          (int)(error_info.total_bits / error_count));
            display_pinned_message(4, 25, "%u",
                                          error_info.max_run);

            for (int i = 0; i < NUM_TEST_PATTERNS; i++) {
                display_pinned_message(1 + i, 69, "%c%i",
                                       test_list[i].errors == INT_MAX ? '>' : ' ',
                                       test_list[i].errors);
            }

            display_error_count(error_count);
        }
        break;

      case ERROR_MODE_ADDRESS:
        // Skip duplicates.
        if (!new_header && addr == error_info.last_addr && xor == error_info.last_xor) {
            break;
        }
        if (new_header) {
#if TESTWORD_WIDTH > 32
            //                  columns:  0---------1---------2---------3---------4---------5---------6---------7---------
            display_pinned_message(0, 0, "pCPU  Pass  Test  Failing Address        Expected          Found           ");
            display_pinned_message(1, 0, "----  ----  ----  ---------------------  ----------------  ----------------");
            //                  fields:    NN   NNNN   NN   PPPPPPPPPOOO (N.NN?B)  XXXXXXXXXXXXXXXX  XXXXXXXXXXXXXXXX
#else
            //                  columns:  0---------1---------2---------3---------4---------5---------6---------7---------
            display_pinned_message(0, 0, "pCPU  Pass  Test  Failing Address        Expected  Found     Err Bits");
            display_pinned_message(1, 0, "----  ----  ----  ---------------------  --------  --------  --------"); 
            //                  fields:    NN   NNNN   NN   PPPPPPPPPOOO (N.NN?B)  XXXXXXXX  XXXXXXXX  XXXXXXXX
#endif
        }
        if (new_address) {
            check_input();
            scroll();

            uintptr_t page   = page_of((void *)addr);
            uintptr_t offset = addr & 0xFFF;

            set_foreground_colour(YELLOW);
            display_scrolled_message(0, " %2i   %4i   %2i   %09x%03x (%kB)",
                                     smp_my_cpu_num(), pass_num, test_num, page, offset, page << 2);
            if (type == PARITY_ERROR) {
                display_scrolled_message(41, "%s", "Parity error detected near this address");
            } else {
#if TESTWORD_WIDTH > 32
                display_scrolled_message(41, "%016x  %016x", good, bad);
#else
                display_scrolled_message(41, "%08x  %08x  %08x  %i", good, bad, xor, error_count);
#endif
            }
            set_foreground_colour(WHITE);

            display_error_count(error_count);
        }
        break;

      case ERROR_MODE_BADRAM:
        if (new_badram) {
            badram_display();
        }
        break;

      default:
        break;
    }

    if (type != PARITY_ERROR) {
        error_info.last_addr = addr;
        error_info.last_xor  = xor;
    }

    spin_unlock(error_mutex);
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void error_init(void)
{
    error_info.min_addr.page    = UINTPTR_MAX;
    error_info.min_addr.offset  = 0xfff;
    error_info.max_addr.page    = 0;
    error_info.max_addr.offset  = 0;
    error_info.bad_bits         = 0;
    error_info.min_bits         = 255;
    error_info.max_bits         = 0;
    error_info.total_bits       = 0;
    error_info.run_length       = 0;
    error_info.max_run          = 0;
    error_info.last_addr        = 0;
    error_info.last_xor         = 0;

    error_count = 0;
}

void addr_error(testword_t *addr1, testword_t *addr2, testword_t good, testword_t bad)
{
    common_err(ADDR_ERROR, (uintptr_t)addr1, good, bad, false); (void)addr2;
}

void data_error(testword_t *addr, testword_t good, testword_t bad, bool use_for_badram)
{
#if USB_WORKAROUND
    /* Skip any errrors that appear to be due to the BIOS using location
     * 0x4e0 for USB keyboard support.  This often happens with Intel
     * 810, 815 and 820 chipsets.  It is possible that we will skip
     * a real error but the odds are very low.
     */
    if ((uintptr_t)addr == 0x4e0 || (uintptr_t)addr == 0x410) {
        return;
    }
#endif
    common_err(DATA_ERROR, (uintptr_t)addr, good, bad, use_for_badram);
}

#if REPORT_PARITY_ERRORS
void parity_error(void)
{
    // We don't know the real address that caused the parity error,
    // so use the last recorded test address.
    common_err(PARITY_ERROR, test_addr[my_cpu_num()], 0, 0, false);
}
#endif

void error_update(void)
{
    if (error_count > 0) {
        if (error_mode != last_error_mode) {
            common_err(NEW_MODE, 0, 0, 0, false);
        }
        if (error_mode == ERROR_MODE_SUMMARY && test_list[test_num].errors > 0) {
            display_pinned_message(1 + test_num, 69, "%c%i",
                                   test_list[test_num].errors == INT_MAX ? '>' : ' ',
                                   test_list[test_num].errors);
        }
        display_error_count(error_count);
    }
}
