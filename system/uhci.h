// SPDX-License-Identifier: GPL-2.0
#ifndef UHCI_H
#define UHCI_H
/**
 * \file
 *
 * Provides support for USB keyboards connected via an UHCI controller.
 *
 *//*
 * Copyright (C) 2021-2022 Martin Whitaker.
 */

#include <stdint.h>

#include "usbhcd.h"

/**
 * If necessary, takes ownership of the UHCI device at the specified base
 * address, then resets it.
 *
 * \param bus       - the PCI bus number for accessing the device
 * \param dev       - the PCI device number for accessing the device
 * \param func      - the PCI function number for accessing the device
 * \param io_base   - the base address of the device in I/O space
 *
 * \returns
 * true if ownership was acquired and the device was successfully reset,
 * otherwise false.
 */
bool uhci_reset(int bus, int dev, int func, uint16_t io_base);

/**
 * Initialises the UHCI device at the specified base address, probes all
 * the attached USB devices, and configures any HID USB keyboard devices
 * it finds to generate periodic interrupt transfers that report key
 * presses. If successful, initialises the specified host controller
 * driver object accordingly.
 *
 * \param io_base   - the base address of the device in I/O space
 * \param hcd       - a pointer to a pre-allocated host controller
 *                    driver object that can be used for this device
 *
 * \returns
 * true if the device was successfully initialised and one or more
 * keyboards were found, otherwise false.
 */
bool uhci_probe(uint16_t io_base, usb_hcd_t *hcd);

#endif // UHCI_H
