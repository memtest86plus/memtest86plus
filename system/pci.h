// SPDX-License-Identifier: GPL-2.0
#ifndef PCI_H
#define PCI_H
/**
 * \file
 *
 * Provides functions to perform PCI configuration space reads and writes.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 * Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.
 */

#include <stdint.h>

#define PCI_VID_REG          0x00
#define PCI_DID_REG          0x02
#define PCI_SUB_VID_REG      0x2C
#define PCI_SUB_DID_REG      0x2E

/* Vendor IDs */
#define PCI_VID_LOONGSON     0x0014
#define PCI_VID_ATI          0x1002
#define PCI_VID_AMD          0x1022
#define PCI_VID_SIS          0x1039
#define PCI_VID_ASUS         0x1043
#define PCI_VID_EFAR         0x1055
#define PCI_VID_ALI          0x10B9
#define PCI_VID_NVIDIA       0x10DE
#define PCI_VID_VIA          0x1106
#define PCI_VID_SERVERWORKS  0x1166
#define PCI_VID_SUPERMICRO   0x15D9
#define PCI_VID_HYGON        0x1D94
#define PCI_VID_INTEL        0x8086

#define PCI_MAX_BUS     256
#define PCI_MAX_DEV     32
#define PCI_MAX_FUNC    8

/**
 * Initialises the PCI access support.
 */
void pci_init(void);

/**
 * Returns an 8 bit value read from the specified bus+device+function+register
 * address in the PCI configuration address space.
 */
uint8_t pci_config_read8(int bus, int dev, int func, int reg);

/**
 * Returns a 16 bit value read from the specified bus+device+function+register
 * address in the PCI configuration address space. The address must be 16-bit
 * aligned.
 */
uint16_t pci_config_read16(int bus, int dev, int func, int reg);

/**
 * Returns a 32 bit value read from the specified bus+device+function+register
 * address in the PCI configuration address space. The address must be 32-bit
 * aligned.
 */
uint32_t pci_config_read32(int bus, int dev, int func, int reg);

/**
 * Writes an 8 bit value to the specified bus+device+function+register address
 * in the PCI configuration address space.
 */
void pci_config_write8(int bus, int dev, int func, int reg, uint8_t value);

/**
 * Writes a 16 bit value to the specified bus+device+function+register address
 * in the PCI configuration address space. The address must be 16-bit aligned.
 */
void pci_config_write16(int bus, int dev, int func, int reg, uint16_t value);

/**
 * Writes a 32 bit value to the specified bus+device+function+register address
 * in the PCI configuration address space. The address must be 32-bit aligned.
 */
void pci_config_write32(int bus, int dev, int func, int reg, uint32_t value);


/**
 * Basic LPC Functions
 */

void lpc_outb(uint8_t cmd, uint8_t data);
uint8_t lpc_inb(uint8_t reg);


/**
 * Read & Write to AMD SNM
 */
uint32_t amd_smn_read(uint32_t adr);
void amd_smn_write(uint32_t adr, uint32_t data);

#endif // PCI_H
