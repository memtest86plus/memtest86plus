// SPDX-License-Identifier: GPL-2.0
#ifndef USBHCD_H
#define USBHCD_H
/**
 * \file
 *
 * Provides the base USB host controller driver for USB keyboard support.
 *
 * This is an object-oriented design. The hcd_methods_t structure defines
 * a set of virtual methods that will be implemented by the subclasses.
 * The hcd_workspace_t structure defines the base class properties. The
 * usb_hcd_t structure represents a driver object. The non-virtual and
 * default base class methods are defined as separate functions, taking
 * a usb_hcd_t pointer as their first parameter.
 *
 * The find_usb_keyboards function instantiates a driver object of the
 * appropriate subclass for each USB controller it finds and stores it
 * in the private usb_controllers table, where it can subsequently used
 * to poll the keyboards for key presses.
 *
 *//*
 * Copyright (C) 2021-2022 Martin Whitaker.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usb.h"

/**
 * The size of the data transfer buffer in a host controller driver workspace.
 */
#define HCD_DATA_BUFFER_SIZE    512  // bytes

/**
 * The size of the key code buffer in a host controller driver workspace.
 */
#define HCD_KC_BUFFER_SIZE      8  // keycodes

/**
 * A USB device speed (used internally by the various HCI drivers).
 */
typedef enum  __attribute__ ((packed)) {
    USB_SPEED_UNKNOWN   = 0,
    USB_SPEED_LOW       = 1,
    USB_SPEED_FULL      = 2,
    USB_SPEED_HIGH      = 3
} usb_speed_t;

/**
 * A USB endpoint descriptor (used internally by the various HCI drivers).
 */
typedef struct __attribute__ ((packed)) {
    uintptr_t       driver_data;
    usb_speed_t     device_speed;
    uint8_t         device_id;
    uint8_t         interface_num;
    uint8_t         endpoint_num;
    uint16_t        max_packet_size;
    uint8_t         interval;
    uint8_t         reserved;
} usb_ep_t;

/**
 * A USB hub descriptor (used internally by the various HCI drivers).
 */
typedef struct __attribute__ ((packed)) {
    const usb_ep_t  *ep0;
    uint32_t        route;
    uint8_t         level;
    uint8_t         num_ports;
    uint8_t         tt_think_time;
    uint8_t         power_up_delay;
} usb_hub_t;

/**
 * A USB host controller driver object reference.
 */
typedef const struct usb_hcd_s *usb_hcd_r;

/**
 * A USB host controller driver method table.
 */
typedef struct {
    bool    (*reset_root_hub_port)  (usb_hcd_r, int);
    int     (*allocate_slot)        (usb_hcd_r);
    bool    (*release_slot)         (usb_hcd_r, int);
    bool    (*assign_address)       (usb_hcd_r, const usb_hub_t *, int, usb_speed_t, int, usb_ep_t *);
    bool    (*configure_hub_ep)     (usb_hcd_r, const usb_ep_t *, const usb_hub_t *);
    bool    (*configure_kbd_ep)     (usb_hcd_r, const usb_ep_t *, int);
    bool    (*setup_request)        (usb_hcd_r, const usb_ep_t *, const usb_setup_pkt_t *);
    bool    (*get_data_request)     (usb_hcd_r, const usb_ep_t *, const usb_setup_pkt_t *, const void *, size_t);
    void    (*poll_keyboards)       (usb_hcd_r);
} hcd_methods_t;

/**
 * A USB host controller driver workspace. This is extended by each HCI driver
 * to append its private data.
 */
typedef struct __attribute__((packed)) {
    uint8_t         data_buffer[HCD_DATA_BUFFER_SIZE];
    size_t          data_length;
    uint8_t         kc_buffer[HCD_KC_BUFFER_SIZE];
    int8_t          kc_index_i;
    int8_t          kc_index_o;
} hcd_workspace_t;

/**
 * A USB host controller driver object.
 */
typedef struct usb_hcd_s {
    const hcd_methods_t *methods;
    hcd_workspace_t     *ws;
} usb_hcd_t;

/**
 * A set of USB device initialisation options.
 */
typedef enum {
    USB_DEFAULT_INIT    = 0,
    USB_EXTRA_RESET     = 1 << 0,
    USB_IGNORE_EHCI     = 1 << 1,
    USB_DEBUG           = 1 << 2
} usb_init_options_t;

/**
 * The selected USB device initialisation options.
 *
 * Used internally by the various HCI drivers.
 */
extern usb_init_options_t usb_init_options;

/**
 * Constructs a USB setup packet in buffer using the provided values.
 *
 * Used internally by the various HCI drivers.
 */
static inline void build_setup_packet(usb_setup_pkt_t *pkt, int type, int request, int value, int index, int length)
{
    pkt->type    = type;
    pkt->request = request;
    pkt->value   = value;
    pkt->index   = index;
    pkt->length  = length;
}

/* Returns the default maximum packet size for a USB device running at the
 * given speed.
 *
 * Used internally by the various HCI drivers.
 */
static inline int default_max_packet_size(usb_speed_t device_speed)
{
    switch (device_speed) {
      case USB_SPEED_LOW:
        return 8;
      case USB_SPEED_FULL:
        return 64;
      case USB_SPEED_HIGH:
        return 64;
      default:
        return 0;
    }
}

/**
 * Returns true if size is a valid value for the maximum packet size for a
 * USB device running at the given speed.
 *
 * Used internally by the various HCI drivers.
 */
static inline bool valid_usb_max_packet_size(int size, usb_speed_t speed)
{
    return (size == 8) || ((speed != USB_SPEED_LOW) && (size == 16 || size == 32 || size == 64));
}

/**
 * Returns true if buffer appears to contain a valid USB device descriptor.
 *
 * Used internally by the various HCI drivers.
 */
static inline bool valid_usb_device_descriptor(const uint8_t *buffer)
{
    usb_desc_header_t *desc = (usb_desc_header_t *)buffer;

    return desc->length == sizeof(usb_device_desc_t) && desc->type == USB_DESC_DEVICE;
}

/**
 * Returns true if buffer appears to contain a valid USB configuration
 * descriptor.
 *
 * Used internally by the various HCI drivers.
 */
static inline bool valid_usb_config_descriptor(const uint8_t *buffer)
{
    usb_desc_header_t *desc = (usb_desc_header_t *)buffer;

    return desc->length == sizeof(usb_config_desc_t) && desc->type == USB_DESC_CONFIGURATION;
}

/**
 * Returns the USB route to the device attached to the hub port specified by
 * hub and port_num. The top 8 bits of the returned value contain the root
 * port number and the bottom 20 bits contain the USB3 route string.
 *
 * Used internally by the various HCI drivers.
 */
uint32_t usb_route(const usb_hub_t *hub, int port_num);

/**
 * Waits for all the bits set in bit_mask to be cleared in the register pointed
 * to by reg or for max_time microseconds to elapse.
 *
 * Used internally by the various HCI drivers.
 */
bool wait_until_clr(const volatile uint32_t *reg, uint32_t bit_mask, int max_time);

/**
 * Waits for all the bits set in bit_mask to also be set in the register pointed
 * to by reg or for max_time microseconds to elapse.
 *
 * Used internally by the various HCI drivers.
 */
bool wait_until_set(const volatile uint32_t *reg, uint32_t bit_mask, int max_time);

/**
 * Displays an informational message, scrolling the screen if necessary.
 * Takes the same arguments as the printf function.
 *
 * Used internally by the various HCI drivers.
 */
void print_usb_info(const char *fmt, ...);

/**
 * Resets the specified USB hub port.
 *
 * Used internally by the various HCI drivers.
 */
bool reset_usb_hub_port(const usb_hcd_t *hcd, const usb_hub_t *hub, int port_num);

/**
 * Sets the device address for the device attached to the specified hub port
 * (thus moving the device to Address state), fills in the descriptor for the
 * device's default control endpoint (ep0), and leaves the device descriptor
 * in the driver's data transfer buffer. Returns true if all actions are
 * successfully completed.
 *
 * This is the default implementation of the HCD assign_address method.
 */
bool assign_usb_address(const usb_hcd_t *hcd, const usb_hub_t *hub, int port_num,
                        usb_speed_t device_speed, int device_id, usb_ep_t *ep0);

/**
 * Scans the specified USB device to detect whether it has any HID keyboards
 * attached to it (directly or indirectly). If so, the keyboard device(s)
 * are initialised and configured, as are any intermediate USB hubs, and the
 * table defined by keyboards and max_keyboards is updated accordingly and
 * num_keyboards is updated to match. Returns true if any keyboards were found
 * and added to the table.
 *
 * Used internally by the various HCI drivers.
 */
bool find_attached_usb_keyboards(const usb_hcd_t *hcd, const usb_hub_t *hub, int port_num,
                                 usb_speed_t device_speed, int device_id, int *num_devices,
                                 usb_ep_t keyboards[], int max_keyboards, int *num_keyboards);

/**
 * Scans the latest keyboard report from a HID keyboard for key presses that
 * weren't present in the previous report from that keyboard. Appends the HID
 * key code for each new key press to the driver's key code buffer. Returns
 * false if the report signals the phantom condition, otherwise returns true.
 *
 * Used internally by the various HCI drivers.
 */
bool process_usb_keyboard_report(const usb_hcd_t *hcd, const hid_kbd_rpt_t *report, const hid_kbd_rpt_t *prev_report);

/**
 * Scans the attached USB devices and initialises all HID keyboard devices
 * it finds (subject to implementation limits on the number of devices).
 * Records the information needed to subsequently poll those devices for
 * key presses.
 *
 * Used internally by keyboard.c.
 */
void find_usb_keyboards(bool pause_if_none);

/**
 * Polls the keyboards discovered by find_usb_keyboards. Consumes and returns
 * the HID key code for the first key press it detects. Returns zero if no key
 * has been pressed.
 *
 * Used internally by keyboard.c.
 */
uint8_t get_usb_keycode(void);

#endif // USBHCD_H
