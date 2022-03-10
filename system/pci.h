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
 */

#include <stdint.h>

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


/*
 * Add some SNM related function (S.DEMEULEMEESTER)
 */

#define SMN_SMUIO_THM               0x00059800
#define SMN_THM_TCON_CUR_TMP        (SMN_SMUIO_THM + 0x00)

/**
 * Read & Write to AMD Family 17h SNM
 */
uint32_t amd_smn_read(uint32_t adr);
void amd_smn_write(uint32_t adr, uint32_t data);

#endif // PCI_H
