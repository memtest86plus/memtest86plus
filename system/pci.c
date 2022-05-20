// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2021 Martin Whitaker.
//
// Derived from memtest86+ pci.c:
//
// MemTest86+ V5.00 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.x86-secret.com - http://www.memtest.org
// ----------------------------------------------------
// pci.c - MemTest-86  Version 3.2
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"
#include "bootparams.h"

#include "cpuid.h"
#include "io.h"

#include "pci.h"
#include "unistd.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define PCI_CLASS_DEVICE        0x0a

#define PCI_CLASS_BRIDGE_HOST   0x0600

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef enum {
    PCI_CONFIG_TYPE_NONE  = 0,
    PCI_CONFIG_TYPE_1     = 1,
    PCI_CONFIG_TYPE_2     = 2
} pci_config_type_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static pci_config_type_t pci_config_type = PCI_CONFIG_TYPE_NONE;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static bool pci_sanity_check(void)
{
    // Do a trivial check to make certain we can see a host bridge.
    // There are reportedly some buggy chipsets from Intel and
    // Compaq where this test does not work, I will worry about
    // that when we support them.
    return pci_config_read16(0, 0, 0, PCI_CLASS_DEVICE) == PCI_CLASS_BRIDGE_HOST;
}

static void probe_config_type(void)
{
    uint8_t  tmpCFB;
    uint32_t tmpCF8;

    if (cpuid_info.vendor_id.str[0] == 'A' && cpuid_info.version.family == 0xf) {
        pci_config_type = PCI_CONFIG_TYPE_1;
        return;
    }

    // Check if configuration type 1 works.
    pci_config_type = PCI_CONFIG_TYPE_1;
    tmpCFB = inb(0xcfb);
    outb(0x01, 0xcfb);
    tmpCF8 = inl(0xcf8);
    outl(0x80000000, 0xcf8);
    if (inl(0xcf8) == 0x80000000 && pci_sanity_check()) {
        outl(tmpCF8, 0xcf8);
        outb(tmpCFB, 0xcfb);
        return;
    }
    outl(tmpCF8, 0xcf8);

    // Check if configuration type 2 works.
    pci_config_type = PCI_CONFIG_TYPE_2;
    outb(0x00, 0xcfb);
    outb(0x00, 0xcf8);
    outb(0x00, 0xcfa);
    if (inb(0xcf8) == 0x00 && inb(0xcfa) == 0x00 && pci_sanity_check()) {
        outb(tmpCFB, 0xcfb);
        return;
    }
    outb(tmpCFB, 0xcfb);

    // Nothing worked.
    pci_config_type = PCI_CONFIG_TYPE_NONE;
}

static void set_pci_config1_addr(int bus, int dev, int func, int reg)
{
    uint32_t addr = 0x80000000
                  | (reg  & 0xf00) << 16
                  | (bus  & 0xff)  << 16
                  | (dev  & 0x1f)  << 11
                  | (func & 0x07)  << 8
                  | (reg  & 0xfc);

    outl(addr, 0xcf8);
}

static void set_pci_config2_bus_func(int bus, int func)
{
    outb(0xf0 | (func & 0x7) << 1, 0xcf8);
    outb(bus, 0xcfa);
}

static int pci_config2_access_addr(int dev, int reg)
{
    return 0xc000 | (dev & 0x1f) << 8 | (reg & 0xff);
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void pci_init(void)
{
    const boot_params_t *boot_params = (boot_params_t *)boot_params_addr;
    if (boot_params->efi_info.loader_signature) {
        // On UEFI systems we can assume configuration type 1.
        pci_config_type = PCI_CONFIG_TYPE_1;
    } else {
        probe_config_type();
    }
}

uint8_t pci_config_read8(int bus, int dev, int func, int reg)
{
    uint8_t value;

    switch (pci_config_type) {
      case PCI_CONFIG_TYPE_1:
        set_pci_config1_addr(bus, dev, func, reg);
        return inb(0xcfc + (reg & 0x3));
      case PCI_CONFIG_TYPE_2:
        set_pci_config2_bus_func(bus, func);
        value = inb(pci_config2_access_addr(dev, reg));
        outb(0, 0xcf8);
        return value;
      default:
        return 0xFF;
    }
}

uint16_t pci_config_read16(int bus, int dev, int func, int reg)
{
    uint16_t value;

    switch (pci_config_type) {
      case PCI_CONFIG_TYPE_1:
        set_pci_config1_addr(bus, dev, func, reg);
        return inw(0xcfc + (reg & 0x2));
      case PCI_CONFIG_TYPE_2:
        set_pci_config2_bus_func(bus, func);
        value = inw(pci_config2_access_addr(dev, reg));
        outb(0, 0xcf8);
        return value;
      default:
        return 0xFFFF;
    }
}

uint32_t pci_config_read32(int bus, int dev, int func, int reg)
{
    uint32_t value;

    switch (pci_config_type) {
      case PCI_CONFIG_TYPE_1:
        set_pci_config1_addr(bus, dev, func, reg);
        return inl(0xcfc);
      case PCI_CONFIG_TYPE_2:
        set_pci_config2_bus_func(bus, func);
        value = inl(pci_config2_access_addr(dev, reg));
        outb(0, 0xcf8);
        return value;
      default:
        return 0xFFFFFFFF;
    }
}

void pci_config_write8(int bus, int dev, int func, int reg, uint8_t value)
{
    switch (pci_config_type)
    {
      case PCI_CONFIG_TYPE_1:
        set_pci_config1_addr(bus, dev, func, reg);
        outb(value, 0xcfc + (reg & 0x3));
        break;
      case PCI_CONFIG_TYPE_2:
        set_pci_config2_bus_func(bus, func);
        outb(value, pci_config2_access_addr(dev, reg));
        outb(0, 0xcf8);
        break;
      default:
        break;
    }
}

void pci_config_write16(int bus, int dev, int func, int reg, uint16_t value)
{
    switch (pci_config_type)
    {
      case PCI_CONFIG_TYPE_1:
        set_pci_config1_addr(bus, dev, func, reg);
        outw(value, 0xcfc + (reg & 0x2));
        break;
      case PCI_CONFIG_TYPE_2:
        set_pci_config2_bus_func(bus, func);
        outw(value, pci_config2_access_addr(dev, reg));
        outb(0, 0xcf8);
        break;
      default:
        break;
    }
}

void pci_config_write32(int bus, int dev, int func, int reg, uint32_t value)
{
    switch (pci_config_type)
    {
      case PCI_CONFIG_TYPE_1:
        set_pci_config1_addr(bus, dev, func, reg);
        outl(value, 0xcfc);
        break;
      case PCI_CONFIG_TYPE_2:
        set_pci_config2_bus_func(bus, func);
        outl(value, pci_config2_access_addr(dev, reg));
        outb(0, 0xcf8);
        break;
      default:
        break;
    }
}


// -------------
// LPC Functions
// -------------

void lpc_outb(uint8_t cmd, uint8_t data)
{
    outb(cmd, 0x2E);
    usleep(100);
    outb(data, 0x2F);
    usleep(100);
}

uint8_t lpc_inb(uint8_t reg)
{
    outb(reg, 0x2E);
    usleep(100);
    return inb(0x2F);
}

// ---------------------------------------
// AMD System Management Network Functions
// ---------------------------------------

uint32_t amd_smn_read(uint32_t adr)
{
  pci_config_write32(0, 0, 0, 0x60, adr);

  return pci_config_read32(0, 0, 0, 0x64);
}

void amd_smn_write(uint32_t adr, uint32_t data)
{
  pci_config_write32(0, 0, 0, 0x60, adr);
  pci_config_write32(0, 0, 0, 0x64, data);
}
