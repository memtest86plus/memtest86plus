// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2024 Martin Whitaker.
//
// Derived from an extract of memtest86+ lib.c:
//
// lib.c - MemTest-86  Version 3.4
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "ctype.h"
#include "keyboard.h"
#include "print.h"
#include "serial.h"
#include "unistd.h"

#include "read.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

uintptr_t read_value(int row, int col, int field_width, int shift)
{
    char buffer[1 + field_width];

    for (int i = 0; i < field_width; i++ ) {
        buffer[i] = ' ';
    }
    buffer[field_width] = '\0';

    int n = 0;
    int base = 10;
    bool done = false;
    bool tty_update = enable_tty;
    bool got_suffix = false;
    while (!done) {
        char c = get_key();

        if (tty_update) {
            tty_send_region(row, col, row, col+10);
        }

        tty_update = enable_tty;

        switch (c) {
          case '\n':
            if (n > 0) {
                done = true;
            }
            break;
          case '\b':
            if (n > 0) {
                got_suffix = false;
                buffer[--n] = ' ';
            }
            break;
          case '0':
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9':
            if (n < field_width && base >= 10 && !got_suffix) {
                buffer[n] = c;
            }
            break;
          case 'a':
          case 'b':
          case 'c':
          case 'd':
          case 'e':
          case 'f':
            if (n < field_width && base >= 16 && !got_suffix) {
                buffer[n] = c;
            }
            break;
          case 'k':
          case 'p':
          case 'm':
          case 'g':
          case 't':
            if (n > 0 && n < field_width && buffer[n-1] != 'x') {
                got_suffix = true;
                buffer[n] = toupper(c);
            }
            break;
          case 'x':
            /* Only allow 'x' after an initial 0 */
            if (n == 1 && n < field_width && buffer[0] == '0') {
                buffer[n] = 'x';
            }
            break;
        default:
            usleep(1000);
            tty_update = false;
            break;
        }
        if (n < field_width && buffer[n] != ' ') {
            n++;
        }
        prints(row, col, buffer);

        if (buffer[0] == '0' && buffer[1] == 'x') {
            base = 16;
        } else {
            base = 10;
        }
    }

    if (got_suffix) {
        switch (buffer[n-1]) {
          case 'T': /* tera */ shift += 40; n--; break;
          case 'G': /* gig  */ shift += 30; n--; break;
          case 'M': /* meg  */ shift += 20; n--; break;
          case 'P': /* page */ shift += 12; n--; break;
          case 'K': /* kilo */ shift += 10; n--; break;
        }
    }

    uintptr_t value = 0;
    for (int i = (base == 16) ? 2 : 0; i < n; i++) {
        value *= base;
        if (buffer[i] >= 'a') {
            value += buffer[i] - 'a' + 10;
        } else {
            value += buffer[i] - '0';
        }
    }

    return shift < 0 ? value >> -shift : value << shift;
}
