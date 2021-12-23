// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2021 Martin Whitaker.

#include <stddef.h>

#include "keyboard.h"
#include "memrw32.h"
#include "pci.h"
#include "screen.h"
#include "usb.h"
#include "vmem.h"

#include "ohci.h"
#include "xhci.h"

#include "print.h"
#include "unistd.h"

#include "usbkbd.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAX_USB_CONTROLLERS     8

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef enum {
    UHCI            = 0,
    OHCI            = 1,
    EHCI            = 2,
    XHCI            = 3,
    MAX_HCI_TYPE    = 4
} hci_type_t;

typedef struct {
    hci_type_t      type;
    void            *workspace;
} usb_controller_info_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static const char *hci_type_str[MAX_HCI_TYPE] = { "UHCI", "OHCI", "EHCI", "XHCI" };

static usb_controller_info_t usb_controllers[MAX_USB_CONTROLLERS];

static int num_usb_controllers = 0;

static int print_row = 0;

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

usb_init_options_t usb_init_options = USB_DEFAULT_INIT;

//------------------------------------------------------------------------------
// Shared Functions (used by controller drivers)
//------------------------------------------------------------------------------

bool wait_until_clr(const volatile uint32_t *reg, uint32_t bit_mask, int max_time)
{
    int timer = max_time >> 3;
    while (read32(reg) & bit_mask) {
        if (timer == 0) return false;
        usleep(8);
        timer--;
    }
    return true;
}

bool wait_until_set(const volatile uint32_t *reg, uint32_t bit_mask, int max_time)
{
    int timer = max_time >> 3;
    while (~read32(reg) & bit_mask) {
        if (timer == 0) return false;
        usleep(8);
        timer--;
    }
    return true;
}

int get_usb_keyboard_info_from_descriptors(const volatile uint8_t *desc_buffer, int desc_length,
                                           usb_keyboard_info_t keyboard_info[], int keyboard_info_size)
{
    int num_keyboards = 0;
    usb_keyboard_info_t *kbd = NULL;
    const volatile uint8_t *curr_ptr = desc_buffer + sizeof(usb_config_desc_t);
    const volatile uint8_t *tail_ptr = desc_buffer + desc_length;
    while (curr_ptr < tail_ptr) {
        // If we've filled the keyboard info table, abort now.
        if (num_keyboards >= keyboard_info_size) break;

        const usb_desc_header_t *header = (const usb_desc_header_t *)curr_ptr;
        const volatile uint8_t *next_ptr = curr_ptr + header->length;

        // Basic checks for validity.
        if (next_ptr < (curr_ptr + 2) || next_ptr > tail_ptr) break;

        if (header->type == USB_DESC_INTERFACE && header->length == sizeof(usb_interface_desc_t)) {
            const usb_interface_desc_t *ifc = (const usb_interface_desc_t *)curr_ptr;
#if 0
            print_usb_info("interface %i class %i subclass %i protocol %i",
                           ifc->interface_num, ifc->class, ifc->subclass, ifc->protocol);
#endif
            if (ifc->class == 3 && ifc->subclass == 1 && ifc->protocol == 1) {
                kbd = &keyboard_info[num_keyboards];
                kbd->interface_num = ifc->interface_num;
            } else {
                kbd = NULL;
            }
        } else if (header->type == USB_DESC_ENDPOINT && header->length == sizeof(usb_endpoint_desc_t)) {
            usb_endpoint_desc_t *endpoint = (usb_endpoint_desc_t *)curr_ptr;
#if 0
            print_usb_info("endpoint addr 0x%02x attr 0x%02x",
                           (uintptr_t)endpoint->address, (uintptr_t)endpoint->attributes);
#endif
            if (kbd && (endpoint->address & 0x80) && (endpoint->attributes & 0x3) == 0x3) {
                kbd->endpoint_num    = endpoint->address & 0xf;
                kbd->max_packet_size = endpoint->max_packet_size;
                kbd->interval        = endpoint->interval;
                num_keyboards++;
                kbd = NULL;
            }
        }
        curr_ptr = next_ptr;
    }
    return num_keyboards;
}

void print_usb_info(const char *fmt, ...)
{
    if (print_row == SCREEN_HEIGHT) {
        scroll_screen_region(0, 0, SCREEN_HEIGHT - 1, SCREEN_WIDTH - 1);
        print_row--;
    }

    va_list args;

    va_start(args, fmt);
    (void)vprintf(print_row++, 0, fmt, args);
    va_end(args);
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void find_usb_keyboards(bool pause_at_end)
{
    clear_screen();
    print_usb_info("Scanning for USB keyboards...");

    num_usb_controllers = 0;
    for (int bus = 0; bus < PCI_MAX_BUS; bus++) {
        for (int dev = 0; dev < PCI_MAX_DEV; dev++) {
            for (int func = 0; func < PCI_MAX_FUNC; func++) {
                uint16_t vendor_id = pci_config_read16(bus, dev, func, 0x00);

                // Test for device/function present.
                if (vendor_id != 0xffff) {
                    uint16_t device_id  = pci_config_read16(bus, dev, func, 0x02);
                    uint16_t pci_status = pci_config_read16(bus, dev, func, 0x06);
                    uint16_t class_code = pci_config_read16(bus, dev, func, 0x0a);
                    uint8_t  hdr_type   = pci_config_read8 (bus, dev, func, 0x0e);

                    // Test for a USB controller.
                    if (class_code == 0x0c03) {
                        // Disable the device while we probe it.
                        uint16_t control = pci_config_read16(bus, dev, func, 0x04);
                        pci_config_write16(bus, dev, func, 0x04, control & ~0x0007);

                        hci_type_t controller_type = pci_config_read8(bus, dev, func, 0x09) >> 4;
                        if (controller_type >= MAX_HCI_TYPE) break;

                        int bar = (controller_type == UHCI) ? 0x20 : 0x10;
                        uintptr_t base_addr = pci_config_read32(bus, dev, func, bar);
                        pci_config_write32(bus, dev, func, bar, 0xffffffff);
                        uintptr_t mmio_size = pci_config_read32(bus, dev, func, bar);
                        pci_config_write32(bus, dev, func, bar, base_addr);
#ifdef __x86_64__
                        if (base_addr & 0x4) {
                            base_addr += (uintptr_t)pci_config_read32(bus, dev, func, bar + 4) << 32;
                            pci_config_write32(bus, dev, func, bar + 4, 0xffffffff);
                            mmio_size += (uintptr_t)pci_config_read32(bus, dev, func, bar + 4) << 32;
                            pci_config_write32(bus, dev, func, bar + 4, base_addr >> 32);
                        } else {
                            mmio_size += (uintptr_t)0xffffffff << 32;
                        }
#endif
                        base_addr &= ~(uintptr_t)0xf;
                        mmio_size &= ~(uintptr_t)0xf;
                        mmio_size = ~mmio_size + 1;

                        print_usb_info("Found %s controller %04x:%04x at %08x size %08x", hci_type_str[controller_type],
                                       (uintptr_t)vendor_id, (uintptr_t)device_id, base_addr, mmio_size);

                        base_addr = map_device(base_addr, mmio_size);
                        if (base_addr == 0) {
                            print_usb_info("  Failed to map device into virtual memory");
                            break;
                        }

                        // Search for power management capability.
                        //uint8_t pm_cap_ptr;
                        if (pci_status & 0x10) {
                            uint8_t cap_ptr = pci_config_read8(bus, dev, func, 0x34) & 0xfe;
                            while (cap_ptr != 0) {
                                uint8_t cap_id = pci_config_read8(bus, dev, func, cap_ptr);
                                if (cap_id == 1) {
                                    uint16_t pm_status = pci_config_read16(bus, dev, func, cap_ptr+2);
                                    // Power on if necessary.
                                    if ((pm_status & 0x3) != 0) {
                                        pci_config_write16(bus, dev, func, cap_ptr+2, 0x8000);
                                        usleep(10000);
                                    }
                                    //pm_cap_ptr = cap_ptr;
                                    break;
                                }
                                cap_ptr = pci_config_read8(bus, dev, func, cap_ptr+1) & 0xfe;
                            }
                        }

                        // Enable the device.
                        pci_config_write16(bus, dev, func, 0x04, control | 0x0007);

                        // Initialise the device according to its type.
                        usb_controller_info_t *new_controller = &usb_controllers[num_usb_controllers];
                        new_controller->type = controller_type;
                        new_controller->workspace = NULL;

                        if (controller_type == UHCI) {
                            print_usb_info("  This controller type is not supported yet");
                        }
                        if (controller_type == OHCI) {
                            new_controller->workspace = ohci_init(base_addr);
                        }
                        if (controller_type == EHCI) {
                            print_usb_info("  This controller type is not supported yet");
                        }
                        if (controller_type == XHCI) {
                            new_controller->workspace = xhci_init(base_addr);
                        }
                        if (new_controller->workspace != NULL) {
                            num_usb_controllers++;
                            // If we've filled the controller table, abort now.
                            if (num_usb_controllers == MAX_USB_CONTROLLERS) {
                                return;
                            }
                        }
                    }
                    // Break out if this is a single function device.
                    if (func == 0 && (hdr_type & 0x80) == 0) {
                        break;
                    }
                } else {
                    // Break out if no device is present.
                    if (func == 0) {
                        break;
                    }
                }
            }
        }
    }

    if (pause_at_end) {
        print_usb_info("Press any key to continue...");
        while (get_key() == 0) {}
    }
}

uint8_t get_usb_keycode(void)
{
    for (int i = 0; i < num_usb_controllers; i++) {
        uint8_t keycode = 0;
        switch (usb_controllers[i].type) {
          case OHCI:
            keycode = ohci_get_keycode(usb_controllers[i].workspace);
            break;
          case XHCI:
            keycode = xhci_get_keycode(usb_controllers[i].workspace);
            break;
          default:
            break;
        }
        if (keycode != 0) return keycode;
    }
    return 0;
}
