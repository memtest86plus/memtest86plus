// SPDX-License-Identifier: GPL-2.0
#ifndef XHCI_H
#define XHCI_H
/*
 * Provides support for USB keyboards connected via an XHCI controller.
 *
 * Copyright (C) 2021 Martin Whitaker.
 */

#include <stdint.h>

/*
 * Initialises the XHCI device found at base_addr, scans all the attached USB
 * devices, and configures any HID USB keyboard devices it finds to generate
 * periodic interrupt transfers that report key presses.
 */
void *xhci_init(uintptr_t base_addr);

/*
 * Polls the completed periodic interrupt transfers, stores the keycodes from
 * any new key press events in an internal queue, and if the keycode queue is
 * not empty, pops and returns the keycode from the front of the queue.
 */
uint8_t xhci_get_keycode(void *ws);

#endif // XHCI_H
