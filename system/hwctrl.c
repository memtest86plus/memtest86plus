// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Martin Whitaker.
//
// Derived from an extract of memtest86+ lib.c:
//
// lib.c - MemTest-86  Version 3.4
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdint.h>

#include "io.h"

#include "hwctrl.h"

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void reboot(void)
{
    // Tell the BIOS to do a warm reboot.
    *((uint16_t *)0x472) = 0x1234;

    // Pulse the system reset signal.
    outb(0xfe, 0x64);
}

void floppy_off()
{
    // Stop the floppy motor.
    outb(0x8, 0x3f2);
}

void cursor_off()
{
    // Set HW cursor off screen.
    outb(0x0f, 0x3d4);
    outb(0xff, 0x3d5);

    outb(0x0e, 0x3d4);
    outb(0xff, 0x3d5);
}
