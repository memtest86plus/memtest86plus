// SPDX-License-Identifier: GPL-2.0
#ifndef USBKBD_H
#define USBKBD_H
/*
 * Provides low-level support for USB keyboards.
 *
 * Copyright (C) 2021 Martin Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

#include <usb.h>

/*
 * A USB keyboard device descriptor used internally by the various HCI drivers.
 */
typedef struct {
    int     port_speed;
    int     device_num;
    int     interface_num;
    int     endpoint_num;
    int     max_packet_size;
    int     interval;
} usb_keyboard_info_t;

/*
 * A set of USB device initialisation options.
 */
typedef enum {
    USB_DEFAULT_INIT    = 0,
    USB_EXTRA_RESET     = 1
} usb_init_options_t;

/*
 * The selected USB device initialisation options.
 *
 * Used internally by the various HCI drivers.
 */
extern usb_init_options_t usb_init_options;

/*
 * Constructs a USB setup packet in buffer using the provided values/
 *
 * Used internally by the various HCI drivers.
 */
static inline void build_setup_packet(volatile void *buffer, int type, int request, int value, int index, int length)
{
    usb_setup_pkt_t *pkt = (usb_setup_pkt_t *)buffer;

    pkt->type    = type;
    pkt->request = request;
    pkt->value   = value;
    pkt->index   = index;
    pkt->length  = length;
}

/*
 * Returns true if size is a valid value for the maximum packet size for a
 * low speed or full speed USB device or endpoint.
 *
 * Used internally by the various HCI drivers.
 */
static inline bool valid_usb_max_packet_size(int size, bool is_low_speed)
{
    return (size == 8) || (!is_low_speed && (size == 16 || size == 32 || size == 64));
}

/*
 * Returns true if buffer appears to contain a valid USB device descriptor.
 *
 * Used internally by the various HCI drivers.
 */
static inline bool valid_usb_device_descriptor(volatile uint8_t *buffer)
{
    usb_desc_header_t *desc = (usb_desc_header_t *)buffer;

    return desc->length == sizeof(usb_device_desc_t) && desc->type == USB_DESC_DEVICE;
}

/*
 * Returns true if buffer appears to contain a valid USB configuration descriptor.
 *
 * Used internally by the various HCI drivers.
 */
static inline bool valid_usb_config_descriptor(volatile uint8_t *buffer)
{
    usb_desc_header_t *desc = (usb_desc_header_t *)buffer;

    return desc->length == sizeof(usb_config_desc_t) && desc->type == USB_DESC_CONFIG;
}

/*
 * Waits for all the bits set in bit_mask to be cleared in the register pointed
 * to by reg or for max_time microseconds to elapse.
 *
 * Used internally by the various HCI drivers.
 */
bool wait_until_clr(const volatile uint32_t *reg, uint32_t bit_mask, int max_time);

/*
 * Waits for all the bits set in bit_mask to also be set in the register pointed
 * to by reg or for max_time microseconds to elapse.
 *
 * Used internally by the various HCI drivers.
 */
bool wait_until_set(const volatile uint32_t *reg, uint32_t bit_mask, int max_time);

/*
 * Scans the descriptors obtained from a USB GET_CONFIGURATION request and
 * stored in the buffer defined by desc_buffer and desc_length. Adds an entry
 * in the table defined by keyboard_info and keyboard_info_size for each
 * endpoint it finds that identifies as a HID keyboard device, filling in
 * the interface_num, endpoint_num, max_packet_size, and interval fields.
 * Returns the number of entries added. The scan is terminated early if the
 * table is full.
 *
 * Used internally by the various HCI drivers.
 */
int get_usb_keyboard_info_from_descriptors(const volatile uint8_t *desc_buffer, int desc_length,
                                           usb_keyboard_info_t keyboard_info[], int keyboard_info_size);

/*
 * Displays an informational message, scrolling the screen if necessary.
 * Takes the same arguments as the printf function.
 *
 * Used internally by the various HCI drivers.
 */
void print_usb_info(const char *fmt, ...);

/*
 * Scans the attached USB devices and initialises all HID keyboard devices
 * it finds (subject to implementation limits on the number of devices).
 * Records the information needed to subsequently poll those devices for
 * key presses.
 *
 * Used internally by keyboard.c.
 */
void find_usb_keyboards(bool pause_at_end);

/*
 * Polls the keyboards discovered by find_usb_keyboards. Consumes and returns
 * the HID key code for the first key press it detects. Returns zero if no key
 * has been pressed.
 *
 * Used internally by keyboard.c.
 */
uint8_t get_usb_keycode(void);

#endif // USBKBD_H
