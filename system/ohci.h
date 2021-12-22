// SPDX-License-Identifier: GPL-2.0
#ifndef OHCI_H
#define OHCI_H
/*
 * Provides support for USB keyboards connected via an OHCI controller.
 *
 * Copyright (C) 2021 Martin Whitaker.
 */

#include <stdint.h>

/*
 * Initialises the OHCI device found at base_addr, scans all the attached USB
 * devices, and configures any HID USB keyboard devices it finds to generate
 * periodic interrupt transfers that report key presses.
 */
void *ohci_init(uintptr_t base_addr);

/*
 * Polls the completed periodic interrupt transfers, stores the keycodes from
 * any new key press events in an internal queue, and if the keycode queue is
 * not empty, pops and returns the keycode from the front of the queue.
 */
uint8_t ohci_get_keycode(void *ws);

#endif // OHCI_H
