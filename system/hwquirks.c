// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2022 Samuel Demeulemeester
//
// ------------------------
// This file is used to detect quirks on specific hardware
// that require proprietary init here *OR* different code path
// later in various part of the code.
//
// Please add a quick comment for every quirk added to the list.

#include "hwquirks.h"
#include "io.h"
#include "pci.h"
#include "unistd.h"

quirk_t quirk;

// --------------------------------------
// -- Private quirk-specific functions --
// --------------------------------------

static void asus_tusl2_configure_mux(void)
{
    uint8_t muxreg;

    // Enter ASB100 Config Mode
    outb(0x87, 0x2E);
    outb(0x87, 0x2E);
    usleep(200);

    // Write LPC Command to access Config Mode Reg
    lpc_outb(0x7, 0x8);

    // Read Config Mode Register
    muxreg = lpc_inb(0xF1);

    // Change Smbus Mux Channel & Write Config Mode Register
    muxreg &= 0xE7;
    muxreg |= 0x10;
    lpc_outb(0xF1, muxreg);
    usleep(200);

    // Leave Config Mode
    outb(0xAA, 0x2E);
}

// ---------------------
// -- Public function --
// ---------------------

void quirks_init(void)
{
    quirk.id        = QUIRK_NONE;
    quirk.type      = QUIRK_TYPE_NONE;
    quirk.root_vid  = pci_config_read16(0, 0, 0, 0);
    quirk.root_did  = pci_config_read16(0, 0, 0, 2);
    quirk.process   = NULL;

    //  ------------------------
    //  -- ASUS TUSL2-C Quirk --
    //  ------------------------
    // This motherboard has an ASB100 ASIC with a SMBUS Mux Integrated.
    // To access SPD later in the code, we need to configure the mux.
    // PS: Detection via DMI is unreliable, so using Root PCI Registers
    if (quirk.root_vid == 0x8086 && quirk.root_did == 0x1130) {     // Intel i815
        if (pci_config_read16(0, 0, 0, 0x2C) == 0x1043) {           // ASUS
            if (pci_config_read16(0, 0, 0, 0x2E) == 0x8027) {       // TUSL2-C
                quirk.id    = QUIRK_TUSL2;
                quirk.type |= QUIRK_TYPE_SMBUS;
                quirk.process = asus_tusl2_configure_mux;
            }
        }
    }
}
