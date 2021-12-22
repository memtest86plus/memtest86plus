// SPDX-License-Identifier: GPL-2.0
#ifndef USB_H
#define USB_H
/*
 * Provides definitions of various values and data structures defined by the
 * USB specification.
 *
 * Copyright (C) 2021 Martin Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

#define USB_DESC_DEVICE     0x01
#define USB_DESC_CONFIG     0x02
#define USB_DESC_INTERFACE  0x04
#define USB_DESC_ENDPOINT   0x05

typedef volatile struct __attribute__((packed)) {
    uint8_t     type;
    uint8_t     request;
    uint16_t    value;
    uint16_t    index;
    uint16_t    length;
} usb_setup_pkt_t;

typedef volatile struct __attribute__((packed)) {
    uint8_t     length;
    uint8_t     type;
} usb_desc_header_t;

typedef volatile struct __attribute__((packed)) {
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

typedef volatile struct __attribute__((packed)) {
    uint8_t     length;
    uint8_t     type;
    uint16_t    total_length;
    uint8_t     num_interfaces;
    uint8_t     config_num;
    uint8_t     config_str;
    uint8_t     attributes;
    uint8_t     max_power;
} usb_config_desc_t;

typedef volatile struct __attribute__((packed)) {
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

typedef volatile struct __attribute__((packed)) {
    uint8_t     length;
    uint8_t     type;
    uint8_t     address;
    uint8_t     attributes;
    uint16_t    max_packet_size;
    uint8_t     interval;
} usb_endpoint_desc_t;

typedef volatile struct __attribute__((packed)) {
    uint8_t     modifiers;
    uint8_t     reserved;
    uint8_t     key_code[6];
} hid_kbd_rpt_t;

#endif // USB_H
