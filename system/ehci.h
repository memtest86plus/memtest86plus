// SPDX-License-Identifier: GPL-2.0
#ifndef EHCI_H
#define EHCI_H
/**
 * \file
 *
 * Provides support for USB keyboards connected via an EHCI controller.
 *
 * Copyright (C) 2021-2022 Martin Whitaker.
 */

#include <stdint.h>

#include "usbhcd.h"

/**
 * Initialises the EHCI device identified by bus, dev, func, and base_addr,
 * scans all the attached USB devices, and configures any HID USB keyboard
 * devices it finds to generate periodic interrupt transfers that report key
 * presses. Initialises hcd and returns true if the device was successfully
 * initialised and one or more keyboards were found.
 */
bool ehci_init(int bus, int dev, int func, uintptr_t base_addr, usb_hcd_t *hcd);

#endif // EHCI_H
