// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2025 Martin Whitaker.

#include <stdint.h>

#include "bootparams.h"

#include "io.h"
#include "usbhcd.h"

#include "serial.h"

#include "keyboard.h"
#include "config.h"

//------------------------------------------------------------------------------
// Private Constants
//------------------------------------------------------------------------------

// The number of frame periods to wait during escape sequence parsing.
#define TTY_SEQ_WAIT_TIME   2

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

// Convert set 1 scancodes to characters.
static const char legacy_keymap[] = {
    /* 0x00 */   0,
    /* 0x01 */ ESC,
    /* 0x02 */ '1',
    /* 0x03 */ '2',
    /* 0x04 */ '3',
    /* 0x05 */ '4',
    /* 0x06 */ '5',
    /* 0x07 */ '6',
    /* 0x08 */ '7',
    /* 0x09 */ '8',
    /* 0x0a */ '9',
    /* 0x0b */ '0',
    /* 0x0c */ '-',
    /* 0x0d */ '+',
    /* 0x0e */ '\b',
    /* 0x0f */ '\t',
    /* 0x10 */ 'q',
    /* 0x11 */ 'w',
    /* 0x12 */ 'e',
    /* 0x13 */ 'r',
    /* 0x14 */ 't',
    /* 0x15 */ 'y',
    /* 0x16 */ 'u',
    /* 0x17 */ 'i',
    /* 0x18 */ 'o',
    /* 0x19 */ 'p',
    /* 0x1a */ '[',
    /* 0x1b */ ']',
    /* 0x1c */ '\n',
    /* 0x1d */   0,
    /* 0x1e */ 'a',
    /* 0x1f */ 's',
    /* 0x20 */ 'd',
    /* 0x21 */ 'f',
    /* 0x22 */ 'g',
    /* 0x23 */ 'h',
    /* 0x24 */ 'j',
    /* 0x25 */ 'k',
    /* 0x26 */ 'l',
    /* 0x27 */ ';',
    /* 0x28 */ '\'',
    /* 0x29 */ '`',
    /* 0x2a */   0,
    /* 0x2b */ '\\',
    /* 0x2c */ 'z',
    /* 0x2d */ 'x',
    /* 0x2e */ 'c',
    /* 0x2f */ 'v',
    /* 0x30 */ 'b',
    /* 0x31 */ 'n',
    /* 0x32 */ 'm',
    /* 0x33 */ ',',
    /* 0x34 */ '.',
    /* 0x35 */ '/',
    /* 0x36 */   0,
    /* 0x37 */ '*',  // keypad
    /* 0x38 */   0,
    /* 0x39 */ ' ',
    /* 0x3a */   0,
    /* 0x3b */ '1',  // F1
    /* 0x3c */ '2',  // F2
    /* 0x3d */ '3',  // F3
    /* 0x3e */ '4',  // F4
    /* 0x3f */ '5',  // F5
    /* 0x40 */ '6',  // F6
    /* 0x41 */ '7',  // F7
    /* 0x42 */ '8',  // F8
    /* 0x43 */ '9',  // F9
    /* 0x44 */ '0',  // F10
    /* 0x45 */   0,
    /* 0x46 */   0,
    /* 0x47 */   0,  // keypad
    /* 0x48 */ 'u',  // keypad
    /* 0x49 */   0,  // keypad
    /* 0x4a */ '-',  // keypad
    /* 0x4b */ 'l',  // keypad
    /* 0x4c */   0,  // keypad
    /* 0x4d */ 'r',  // keypad
    /* 0x4e */ '+',  // keypad
    /* 0x4f */   0,  // keypad
    /* 0x50 */ 'd',  // keypad
    /* 0x51 */   0,  // keypad
    /* 0x52 */   0,  // keypad
    /* 0x53 */   0,  // keypad
};

// Convert USB HID keycodes to characters.
static const char usb_hid_keymap[] = {
    /* 0x00 */   0,
    /* 0x01 */   0,
    /* 0x02 */   0,
    /* 0x03 */   0,
    /* 0x04 */ 'a',
    /* 0x05 */ 'b',
    /* 0x06 */ 'c',
    /* 0x07 */ 'd',
    /* 0x08 */ 'e',
    /* 0x09 */ 'f',
    /* 0x0a */ 'g',
    /* 0x0b */ 'h',
    /* 0x0c */ 'i',
    /* 0x0d */ 'j',
    /* 0x0e */ 'k',
    /* 0x0f */ 'l',
    /* 0x10 */ 'm',
    /* 0x11 */ 'n',
    /* 0x12 */ 'o',
    /* 0x13 */ 'p',
    /* 0x14 */ 'q',
    /* 0x15 */ 'r',
    /* 0x16 */ 's',
    /* 0x17 */ 't',
    /* 0x18 */ 'u',
    /* 0x19 */ 'v',
    /* 0x1a */ 'w',
    /* 0x1b */ 'x',
    /* 0x1c */ 'y',
    /* 0x1d */ 'z',
    /* 0x1e */ '1',
    /* 0x1f */ '2',
    /* 0x20 */ '3',
    /* 0x21 */ '4',
    /* 0x22 */ '5',
    /* 0x23 */ '6',
    /* 0x24 */ '7',
    /* 0x25 */ '8',
    /* 0x26 */ '9',
    /* 0x27 */ '0',
    /* 0x28 */ '\n',
    /* 0x29 */ ESC,
    /* 0x2a */ '\b',
    /* 0x2b */ '\t',
    /* 0x2c */ ' ',
    /* 0x2d */ '-',
    /* 0x2e */ '+',
    /* 0x2f */ '[',
    /* 0x30 */ ']',
    /* 0x31 */ '\\',
    /* 0x32 */ '#',
    /* 0x33 */ ';',
    /* 0x34 */ '\'',
    /* 0x35 */ '`',
    /* 0x36 */ ',',
    /* 0x37 */ '.',
    /* 0x38 */ '/',
    /* 0x39 */   0,  // Caps Lock
    /* 0x3a */ '1',  // F1
    /* 0x3b */ '2',  // F2
    /* 0x3c */ '3',  // F3
    /* 0x3d */ '4',  // F4
    /* 0x3e */ '5',  // F5
    /* 0x3f */ '6',  // F6
    /* 0x40 */ '7',  // F7
    /* 0x41 */ '8',  // F8
    /* 0x42 */ '9',  // F9
    /* 0x43 */ '0',  // F10
    /* 0x44 */   0,  // F11
    /* 0x45 */   0,  // F12
    /* 0x46 */   0,  // Print Screen
    /* 0x47 */   0,  // Scroll Lock
    /* 0x48 */   0,  // Pause
    /* 0x49 */   0,  // Insert
    /* 0x4a */   0,  // Delete
    /* 0x4b */   0,  // Home
    /* 0x4c */   0,  // Page Up
    /* 0x4d */   0,  // Delete
    /* 0x4e */   0,  // Page Down
    /* 0x4f */ 'r',  // Cursor Right
    /* 0x50 */ 'l',  // Cursor Left
    /* 0x51 */ 'd',  // Cursor Down
    /* 0x52 */ 'u',  // Cursor Up
    /* 0x53 */   0,  // Number Lock
    /* 0x54 */ '/',  // keypad
    /* 0x55 */ '*',  // keypad
    /* 0x56 */ '-',  // keypad
    /* 0x57 */ '+',  // keypad
    /* 0x58 */ '\n', // keypad
    /* 0x59 */   0,  // keypad
    /* 0x5a */ 'd',  // keypad
    /* 0x5b */   0,  // keypad
    /* 0x5c */ 'l',  // keypad
    /* 0x5d */   0,  // keypad
    /* 0x5e */ 'r',  // keypad
    /* 0x5f */   0,  // keypad
    /* 0x60 */ 'u',  // keypad
    /* 0x61 */   0,  // keypad
    /* 0x62 */   0,  // keypad
    /* 0x63 */   0,  // keypad
    /* 0x64 */ '\\', // Non-US
};

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

keyboard_types_t keyboard_types = KT_NONE;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

// This function is called when an ESC O prefix has been detected.
static char get_vt220_sequence1(void)
{
    switch (tty_get_char(TTY_SEQ_WAIT_TIME)) {
      case 'P':
        return '1';  // VT100/VT220 PF1 (F1 in terminal emulators)
      case 'Q':
        return '2';  // VT100/VT220 PF2 (F2 in terminal emulators)
      case 'R':
        return '3';  // VT100/VT220 PF3 (F3 in terminal emulators)
      case 'S':
        return '4';  // VT100/VT220 PF4 (F4 in terminal emulators)
      case 'T':
        return '5';  // F5 in some terminal emulators
      case 'U':
        return '6';  // F6 in some terminal emulators
      case 'V':
        return '7';  // F7 in some terminal emulators
      case 'W':
        return '8';  // F8 in some terminal emulators
      case 'X':
        return '9';  // F9 in some terminal emulators
      case 'Y':
        return '0';  // F10 in some terminal emulators
      default:
        return '\0'; // unrecognised key
        break;
    }
}

// This function is called when an ESC [ prefix has been detected.
static char get_vt220_sequence2(void)
{
    char ch1;
    char ch2;

    ch1 = tty_get_char(TTY_SEQ_WAIT_TIME);
    switch (ch1) {
      case 'A':
        return 'u';  // VT100/VT220 cursor up
      case 'B':
        return 'd';  // VT100/VT220 cursor down
      case 'C':
        return 'r';  // VT100/VT220 cursor right
      case 'D':
        return 'l';  // VT100/VT220 cursor left
      default:
        break;
    }
    if ((ch1 < '1') || (ch1 > '6')) {
        return '\0';  // unrecognised sequence - give up
    }
    ch2 = tty_get_char(TTY_SEQ_WAIT_TIME);
    if (ch2 == '~') {
        return '\0';  // VT100/VT220 editing key - ignore it
    }
    switch (ch1) {
      case '1':
        switch (ch2) {
          case '1':
            ch1 = '1'; break;  // F1 in terminal emulators
          case '2':
            ch1 = '2'; break;  // F2 in terminal emulators
          case '3':
            ch1 = '3'; break;  // F3 in terminal emulators
          case '4':
            ch1 = '4'; break;  // F4 in terminal emulators
          case '5':
            ch1 = '5'; break;  // F5 in terminal emulators
          case '7':
            ch1 = '6'; break;  // VT220 F6
          case '8':
            ch1 = '7'; break;  // VT220 F7
          case '9':
            ch1 = '8'; break;  // VT220 F8
          default:
            ch1 = '\0'; break; // unrecognised key
        }
        break;
      case '2':
        switch (ch2) {
          case '0':
            ch1 = '9'; break;  // VT220 F9
          case '1':
            ch1 = '0'; break;  // VT220 F10
          default:
            ch1 = '\0'; break; // unrecognised key
        }
        break;
      default:
        ch1 = '\0';  // unrecognised key
        break;
    }
    ch2 = tty_get_char(TTY_SEQ_WAIT_TIME);
    if (ch2 == '~') {
        return ch1;  // valid key sequence - return decoded key
    }
    return '\0';  // invalid key sequence - ignore it
}

// This function is called when an ESC has been detected.
static char get_tty_special_key(void)
{
    switch (tty_get_char(TTY_SEQ_WAIT_TIME)) {
      case 'A':
        return 'u';  // VT52 cursor up
      case 'B':
        return 'd';  // VT52 cursor down
      case 'C':
        return 'r';  // VT52 cursor right
      case 'D':
        return 'l';  // VT52 cursor left
      case 'O':
        return get_vt220_sequence1();
      case '[':
        return get_vt220_sequence2();
      case '\0':
        return ESC;  // timeout - return ESC character
      default:
        return '\0'; // unrecognised sequence - give up
    }
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void keyboard_init(void)
{
    if (keyboard_types == KT_NONE) {
        // No command line option was found, so set the default according to
        // how we were booted.
        const boot_params_t *boot_params = (boot_params_t *)boot_params_addr;
        if (boot_params->efi_info.loader_signature != 0) {
            keyboard_types = KT_USB|KT_LEGACY;
        } else {
            keyboard_types = KT_LEGACY;
        }
    }
    if (keyboard_types & KT_USB) {
        find_usb_keyboards(keyboard_types == KT_USB);
    }
}

char get_key(void)
{
    if (enable_tty) {
        char c = tty_get_char(0);
        switch (c) {
          case '\0':
            break;
          case '\r':
            return '\n';
          case ESC:
            return get_tty_special_key();
          default:
            return c;
        }
    }

    if (keyboard_types & KT_USB) {
        uint8_t c = get_usb_keycode();
        if (c > 0 && c < sizeof(usb_hid_keymap)) {
            return usb_hid_keymap[c];
        }
    }

    static bool escaped = false;
    if (keyboard_types & KT_LEGACY) {
        uint8_t status = inb(0x64);
        if (status & 0x01) {
            uint8_t c = inb(0x60);
            if (status & 0x20) {
                // Ignore mouse events.
                return '\0';
            }
            if (escaped) {
                escaped = false;
                switch (c) {
                  case 0x48 : return 'u';
                  case 0x4b : return 'l';
                  case 0x4d : return 'r';
                  case 0x50 : return 'd';
                  default   : return 0;
                }
            }
            if (c < sizeof(legacy_keymap)) {
                return legacy_keymap[c];
            }
            escaped = (c == 0xe0);

            // Ignore keys we don't recognise and key up codes
        }
    }

    return '\0';
}
