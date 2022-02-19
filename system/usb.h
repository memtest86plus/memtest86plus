// SPDX-License-Identifier: GPL-2.0
#ifndef USB_H
#define USB_H
/**
 * \file
 *
 * Provides definitions of various values and data structures defined by the
 * USB specifications.
 *
 * Copyright (C) 2021-2022 Martin Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

// Basic limits.

#define USB_MAX_ADDRESS         127

// Request types.

#define USB_REQ_TO_DEVICE       0x00
#define USB_REQ_TO_INTERFACE    0x01
#define USB_REQ_TO_ENDPOINT     0x02
#define USB_REQ_TO_HUB_PORT     0x03

#define USB_REQ_FROM_DEVICE     0x80
#define USB_REQ_FROM_INTERFACE  0x81
#define USB_REQ_FROM_ENDPOINT   0x82
#define USB_REQ_FROM_HUB_PORT   0x83

#define USB_REQ_CLASS           0x20

// Request codes.

#define USB_GET_STATUS          0
#define USB_CLR_FEATURE         1
#define USB_SET_FEATURE         3
#define USB_SET_ADDRESS         5
#define USB_GET_DESCRIPTOR      6
#define USB_SET_DESCRIPTOR      7
#define USB_GET_CONFIGURATION   8
#define USB_SET_CONFIGURATION   9
#define USB_GET_INTERFACE       10
#define USB_SET_INTERFACE       11

#define HID_GET_REPORT          1
#define HID_GET_IDLE            2
#define HID_GET_PROTOCOL        3
#define HID_SET_REPORT          9
#define HID_SET_IDLE            10
#define HID_SET_PROTOCOL        11

#define HUB_GET_STATUS          0
#define HUB_CLR_FEATURE         1
#define HUB_SET_FEATURE         3
#define HUB_GET_DESCRIPTOR      6
#define HUB_SET_DESCRIPTOR      7

// Descriptor types.

#define USB_DESC_DEVICE         1
#define USB_DESC_CONFIGURATION  2
#define USB_DESC_INTERFACE      4
#define USB_DESC_ENDPOINT       5

#define HUB_DESC_DEVICE         0x29

// Class codes.

#define USB_CLASS_HID           3
#define USB_CLASS_HUB           9

// Hub feature selectors.

#define HUB_PORT_ENABLE         1
#define HUB_PORT_RESET          4
#define HUB_PORT_POWER          8

// Hub port status.

#define HUB_PORT_CONNECTED      0x00000001
#define HUB_PORT_ENABLED        0x00000002
#define HUB_PORT_RESETTING      0x00000010
#define HUB_PORT_POWERED        0x00000100
#define HUB_PORT_LOW_SPEED      0x00000200
#define HUB_PORT_HIGH_SPEED     0x00000400

// Data structures.

typedef struct __attribute__((packed)) {
    uint8_t     type;
    uint8_t     request;
    uint16_t    value;
    uint16_t    index;
    uint16_t    length;
} usb_setup_pkt_t;

typedef struct __attribute__((packed)) {
    uint8_t     length;
    uint8_t     type;
} usb_desc_header_t;

typedef struct __attribute__((packed)) {
    uint8_t     length;
    uint8_t     type;
    uint8_t     usb_minor;
    uint8_t     usb_major;
    uint8_t     class;
    uint8_t     subclass;
    uint8_t     protocol;
    uint8_t     max_packet_size;
    uint16_t    vendor_id;
    uint16_t    product_id;
    uint8_t     device_minor;
    uint8_t     device_major;
    uint8_t     vendor_str;
    uint8_t     product_str;
    uint8_t     serial_num_str;
    uint8_t     num_configs;
} usb_device_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t     length;
    uint8_t     type;
    uint16_t    total_length;
    uint8_t     num_interfaces;
    uint8_t     config_num;
    uint8_t     config_str;
    uint8_t     attributes;
    uint8_t     max_power;
} usb_config_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t     length;
    uint8_t     type;
    uint8_t     interface_num;
    uint8_t     alt_setting;
    uint8_t     num_endpoints;
    uint8_t     class;
    uint8_t     subclass;
    uint8_t     protocol;
    uint8_t     interface_str;
} usb_interface_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t     length;
    uint8_t     type;
    uint8_t     address;
    uint8_t     attributes;
    uint16_t    max_packet_size;
    uint8_t     interval;
} usb_endpoint_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t     length;
    uint8_t     type;
    uint8_t     num_ports;
    uint16_t    characteristics;
    uint8_t     power_up_delay;
    uint8_t     controller_current;
    uint8_t     port_flags[];
} usb_hub_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t     modifiers;
    uint8_t     reserved;
    uint8_t     key_code[6];
} hid_kbd_rpt_t;

#endif // USB_H
