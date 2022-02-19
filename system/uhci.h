// SPDX-License-Identifier: GPL-2.0
#ifndef UHCI_H
#define UHCI_H
/**
 * \file
 *
 * Provides support for USB keyboards connected via an UHCI controller.
 *
 * Copyright (C) 2021-2022 Martin Whitaker.
 */

#include <stdint.h>

#include "usbhcd.h"

/**
 * Initialises the UHCI device found at I/O address io_base, scans all the
 * attached USB devices, and configures any HID USB keyboard devices it
 * finds to generate periodic interrupt transfers that report key presses.
 * Initialises hcd and returns true if the device was successfully
 * initialised and one or more keyboards were found.
 */
bool uhci_init(uint16_t io_base, usb_hcd_t *hcd);

#endif // UHCI_H
