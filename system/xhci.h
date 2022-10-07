// SPDX-License-Identifier: GPL-2.0
#ifndef XHCI_H
#define XHCI_H
/**
 * \file
 *
 * Provides support for USB keyboards connected via an XHCI controller.
 *
 *//*
 * Copyright (C) 2021-2022 Martin Whitaker.
 */

#include <stdint.h>

#include "usbhcd.h"

/**
 * If necessary, takes ownership of the XHCI device at the specified base
 * address, then resets it.
 *
 * \param base_addr - the base address of the device in virtual memory
 *
 * \returns
 * true if ownership was acquired and the device was successfully reset,
 * otherwise false.
 */
bool xhci_reset(uintptr_t base_addr);

/**
 * Initialises the XHCI device at the specified base address, probes all
 * the attached USB devices, and configures any HID USB keyboard devices
 * it finds to generate periodic interrupt transfers that report key
 * presses. If successful, initialises the specified host controller
 * driver object accordingly.
 *
 * \param base_addr - the base address of the device in virtual memory
 * \param hcd       - a pointer to a pre-allocated host controller
 *                    driver object that can be used for this device
 *
 * \returns
 * true if the device was successfully initialised and one or more
 * keyboards were found, otherwise false.
 */
bool xhci_probe(uintptr_t base_addr, usb_hcd_t *hcd);

#endif // XHCI_H
