// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"
#include "bootparams.h"

#include "cpuid.h"
#include "io.h"

#include "mmio.h"

#include "pci.h"
#include "unistd.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define NB_PCI_MMIO_TYPE0_BASE 0x0EFE00000000
#define NB_PCI_MMIO_TYPE1_BASE 0x0EFE10000000

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void pci_init(void)
{
    return;
}

uint64_t pci_config_type0_addr(int bus, int dev, int func, int reg)
{
    uint64_t addr = NB_PCI_MMIO_TYPE0_BASE
                  | (reg  & 0xf00) << 16
                  | (bus  & 0xff)  << 16
                  | (dev  & 0x1f)  << 11
                  | (func & 0x07)  << 8
                  | (reg  & 0xff);
    return (addr);
}

uint64_t pci_config_type1_addr(int bus, int dev, int func, int reg)
{
    uint64_t addr = NB_PCI_MMIO_TYPE1_BASE
                  | (reg  & 0xf00) << 16
                  | (bus  & 0xff)  << 16
                  | (dev  & 0x1f)  << 11
                  | (func & 0x07)  << 8
                  | (reg  & 0xff);
    return (addr);
}

uint8_t pci_config_read8(int bus, int dev, int func, int reg)
{
    if (bus == 0) {
        return mmio_read8((uint8_t *)pci_config_type0_addr(bus, dev, func, reg));
    } else {
        return mmio_read8((uint8_t *)pci_config_type1_addr(bus, dev, func, reg));
    }
}

uint16_t pci_config_read16(int bus, int dev, int func, int reg)
{
    if (bus == 0) {
        return mmio_read16((uint16_t *)pci_config_type0_addr(bus, dev, func, reg));
    } else {
        return mmio_read16((uint16_t *)pci_config_type1_addr(bus, dev, func, reg));
    }
}

uint32_t pci_config_read32(int bus, int dev, int func, int reg)
{
    if (bus == 0) {
        return mmio_read32((uint32_t *)pci_config_type0_addr(bus, dev, func, reg));
    } else {
        return mmio_read32((uint32_t *)pci_config_type1_addr(bus, dev, func, reg));
    }
}

void pci_config_write8(int bus, int dev, int func, int reg, uint8_t value)
{
    if (bus == 0) {
        mmio_write8((uint8_t *)pci_config_type0_addr(bus, dev, func, reg), value);
    } else {
        mmio_write8((uint8_t *)pci_config_type1_addr(bus, dev, func, reg), value);
    }
}

void pci_config_write16(int bus, int dev, int func, int reg, uint16_t value)
{
    if (bus == 0) {
        mmio_write16((uint16_t *)pci_config_type0_addr(bus, dev, func, reg), value);
    } else {
        mmio_write16((uint16_t *)pci_config_type1_addr(bus, dev, func, reg), value);
    }
}

void pci_config_write32(int bus, int dev, int func, int reg, uint32_t value)
{
    if (bus == 0) {
        mmio_write32((uint32_t *)pci_config_type0_addr(bus, dev, func, reg), value);
    } else {
        mmio_write32((uint32_t *)pci_config_type1_addr(bus, dev, func, reg), value);
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
