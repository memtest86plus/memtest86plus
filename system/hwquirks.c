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
#include "cpuinfo.h"

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

static void get_m1541_l2_cache_size(void)
{
    if (l2_cache != 0) {
        return;
    }

    // Check if L2 cache is enabled with L2CC-2 Register[0]
    if ((pci_config_read8(0, 0, 0, 0x42) & 1) == 0) {
        return;
    }

    // Get L2 Cache Size with L2CC-1 Register[3:2]
    uint8_t reg = (pci_config_read8(0, 0, 0, 0x41) >> 2) & 3;

    if (reg == 0b00) { l2_cache = 256; }
    if (reg == 0b01) { l2_cache = 512; }
    if (reg == 0b10) { l2_cache = 1024; }
}

// ---------------------
// -- Public function --
// ---------------------

void quirks_init(void)
{
    quirk.id        = QUIRK_NONE;
    quirk.type      = QUIRK_TYPE_NONE;
    quirk.root_vid  = pci_config_read16(0, 0, 0, PCI_VID_REG);
    quirk.root_did  = pci_config_read16(0, 0, 0, PCI_DID_REG);
    quirk.process   = NULL;

    //  -------------------------
    //  -- ALi Aladdin V Quirk --
    //  -------------------------
    // As on many Socket 7 Motherboards, the L2 cache is external and must
    // be detected by a proprietary way based on chipset registers
    if (quirk.root_vid == PCI_VID_ALI && quirk.root_did == 0x1541) {    // ALi Aladdin V (M1541)
        quirk.id    = QUIRK_ALI_ALADDIN_V;
        quirk.type |= QUIRK_TYPE_MEM_SIZE;
        quirk.process = get_m1541_l2_cache_size;
    }

    //  ------------------------
    //  -- ASUS TUSL2-C Quirk --
    //  ------------------------
    // This motherboard has an ASB100 ASIC with a SMBUS Mux Integrated.
    // To access SPD later in the code, we need to configure the mux.
    // PS: Detection via DMI is unreliable, so using Root PCI Registers
    if (quirk.root_vid == PCI_VID_INTEL && quirk.root_did == 0x1130) {      // Intel i815
        if (pci_config_read16(0, 0, 0, PCI_SUB_VID_REG) == PCI_VID_ASUS) {  // ASUS
            if (pci_config_read16(0, 0, 0, PCI_SUB_DID_REG) == 0x8027) {    // TUSL2-C
                quirk.id    = QUIRK_TUSL2;
                quirk.type |= QUIRK_TYPE_SMBUS;
                quirk.process = asus_tusl2_configure_mux;
            }
        }
    }

    //  -------------------------------------------------
    //  -- SuperMicro X10SDV Quirk (GitHub Issue #233) --
    //  -------------------------------------------------
    // Memtest86+ crashs on Super Micro X10SDV motherboard with SMP Enabled
    // We were unable to find a solution so far, so disable SMP by default
    if (quirk.root_vid == PCI_VID_INTEL && quirk.root_did == 0x6F00) {             // Broadwell-E (Xeon-D)
        if (pci_config_read16(0, 0, 0, PCI_SUB_VID_REG) == PCI_VID_SUPERMICRO) {   // Super Micro
                quirk.id    = QUIRK_X10SDV_NOSMP;
                quirk.type |= QUIRK_TYPE_SMP;
                quirk.process = NULL;
        }
    }
}
