// SPDX-License-Identifier: GPL-2.0
#ifndef PCI_H
#define PCI_H
/*
 * Provides functions to perform PCI config space reads and writes.
 *
 * Copyright (C) 2020 Martin Whitaker.
 */

#include <stdint.h>

int pci_init(void);

int pci_conf_read(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned len, uint32_t *value);

int pci_conf_write(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned len, uint32_t value);

#endif // PCI_H
