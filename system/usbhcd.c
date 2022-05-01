// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2021-2022 Martin Whitaker.

#include "keyboard.h"
#include "memrw32.h"
#include "pci.h"
#include "screen.h"
#include "usb.h"
#include "vmem.h"

#include "ehci.h"
#include "ohci.h"
#include "uhci.h"
#include "xhci.h"

#include "print.h"
#include "unistd.h"

#include "usbhcd.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAX_USB_CONTROLLERS     8       // an arbitrary limit - must match the initialisation of usb_controllers

#define PAUSE_IF_NONE_TIME      10      // seconds

#define MILLISEC                1000    // in microseconds

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef enum {
    NOT_HCI         = -1,
    UHCI            = 0,
    OHCI            = 1,
    EHCI            = 2,
    XHCI            = 3,
    MAX_HCI_TYPE    = 4
} hci_type_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static const char *hci_name[MAX_HCI_TYPE] = { "UHCI", "OHCI", "EHCI", "XHCI" };

static const hcd_methods_t methods = {
    .reset_root_hub_port = NULL,
    .allocate_slot       = NULL,
    .release_slot        = NULL,
    .assign_address      = NULL,
    .configure_hub_ep    = NULL,
    .configure_kbd_ep    = NULL,
    .setup_request       = NULL,
    .get_data_request    = NULL,
    .poll_keyboards      = NULL
};

// All entries in this array must be initialised in order to generate the necessary relocation records.
static usb_hcd_t usb_controllers[MAX_USB_CONTROLLERS] = {
    { &methods, NULL },
    { &methods, NULL },
    { &methods, NULL },
    { &methods, NULL },
    { &methods, NULL },
    { &methods, NULL },
    { &methods, NULL },
    { &methods, NULL }
};

static int num_usb_controllers = 0;

static int print_row = 0;
static int print_col = 0;

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

usb_init_options_t usb_init_options = USB_DEFAULT_INIT;

//------------------------------------------------------------------------------
// Macro Functions
//------------------------------------------------------------------------------

#define MIN(a, b)   (((a) < (b)) ? (a) : (b))

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static usb_endpoint_desc_t *find_hub_endpoint_descriptor(const uint8_t *desc_buffer, int desc_length)
{
    const uint8_t *curr_ptr = desc_buffer + sizeof(usb_config_desc_t);
    const uint8_t *tail_ptr = desc_buffer + desc_length;
    while (curr_ptr < tail_ptr) {
        const usb_desc_header_t *header = (const usb_desc_header_t *)curr_ptr;
        const uint8_t *next_ptr = curr_ptr + header->length;

        // Basic checks for validity.
        if (next_ptr < (curr_ptr + 2) || next_ptr > tail_ptr) break;

        if (header->type == USB_DESC_ENDPOINT && header->length == sizeof(usb_endpoint_desc_t)) {
            usb_endpoint_desc_t *endpoint = (usb_endpoint_desc_t *)curr_ptr;
#if 0
            print_usb_info("endpoint addr 0x%02x attr 0x%02x",
                           (uintptr_t)endpoint->address, (uintptr_t)endpoint->attributes);
#endif
            if ((endpoint->address & 0x80) && (endpoint->attributes & 0x3) == 0x3) {
                return endpoint;
            }
        }
        curr_ptr = next_ptr;
    }
    return NULL;
}

static bool build_hub_info(const usb_hcd_t *hcd, const usb_hub_t *parent, int port_num, const usb_ep_t *ep0,
                           usb_hub_t *hub, usb_ep_t *ep1)
{
    usb_setup_pkt_t setup_pkt;
    usb_hub_desc_t  hub_desc;

    build_setup_packet(&setup_pkt, USB_REQ_FROM_DEVICE | USB_REQ_CLASS, HUB_GET_DESCRIPTOR,
                                   HUB_DESC_DEVICE << 8, 0, sizeof(hub_desc));
    if (!hcd->methods->get_data_request(hcd, ep0, &setup_pkt, &hub_desc, sizeof(hub_desc))) {
        return false;
    }

    hub->ep0            = ep0;
    hub->level          = parent->level + 1;
    hub->route          = usb_route(parent, port_num);
    hub->num_ports      = hub_desc.num_ports;
    hub->tt_think_time  = hub_desc.characteristics & 0x0060 >> 5;
    hub->power_up_delay = hub_desc.power_up_delay;
    usb_endpoint_desc_t *ep1_desc = find_hub_endpoint_descriptor(hcd->ws->data_buffer, hcd->ws->data_length);
    if (ep1_desc == NULL) {
        return false;
    }

    ep1->driver_data     = ep0->driver_data;
    ep1->device_speed    = ep0->device_speed;
    ep1->device_id       = ep0->device_id;
    ep1->interface_num   = 0;
    ep1->endpoint_num    = ep1_desc->address & 0xf;
    ep1->max_packet_size = ep1_desc->max_packet_size;
    ep1->interval        = ep1_desc->interval;

    return true;
}

static bool get_hub_port_status(const usb_hcd_t *hcd, const usb_hub_t *hub, int port_num, uint32_t *port_status)
{
    usb_setup_pkt_t setup_pkt;

    build_setup_packet(&setup_pkt, USB_REQ_FROM_HUB_PORT | USB_REQ_CLASS, HUB_GET_STATUS,
                                   0, port_num, sizeof(uint32_t));
    return hcd->methods->get_data_request(hcd, hub->ep0, &setup_pkt, port_status, sizeof(uint32_t));
}

static int get_configuration_descriptors(const usb_hcd_t *hcd, const usb_ep_t *ep0, int config_idx)
{
    // Fetch the descriptors for the specified configuration. Start by requesting just the configuration descriptor.
    // Then read the descriptor to determine how much more data we need to fetch.

    usb_setup_pkt_t setup_pkt;

    uint8_t *data_buffer = hcd->ws->data_buffer;

    size_t fetch_length = sizeof(usb_config_desc_t);

  get_descriptor:
    build_setup_packet(&setup_pkt, USB_REQ_FROM_DEVICE, USB_GET_DESCRIPTOR,
                                   USB_DESC_CONFIGURATION << 8 | config_idx, 0, fetch_length);
    if (!hcd->methods->get_data_request(hcd, ep0, &setup_pkt, data_buffer, fetch_length)
    ||  !valid_usb_config_descriptor(data_buffer)) {
        return 0;
    }
    usb_config_desc_t *config = (usb_config_desc_t *)data_buffer;
    size_t total_length = MIN(config->total_length, HCD_DATA_BUFFER_SIZE);
    if (total_length > fetch_length) {
        fetch_length = total_length;
        goto get_descriptor;
    }

    hcd->ws->data_length = fetch_length;

    return config->config_num;
}

static void get_keyboard_info_from_descriptors(const uint8_t *desc_buffer, int desc_length, usb_ep_t keyboards[],
                                               int max_keyboards, int *num_keyboards)
{
    usb_ep_t *kbd = NULL;
    const uint8_t *curr_ptr = desc_buffer + sizeof(usb_config_desc_t);
    const uint8_t *tail_ptr = desc_buffer + desc_length;
    while (curr_ptr < tail_ptr) {
        // If we've filled the keyboard info table, abort now.
        if (*num_keyboards >= max_keyboards) break;

        const usb_desc_header_t *header = (const usb_desc_header_t *)curr_ptr;
        const uint8_t *next_ptr = curr_ptr + header->length;

        // Basic checks for validity.
        if (next_ptr < (curr_ptr + 2) || next_ptr > tail_ptr) break;

        if (header->type == USB_DESC_INTERFACE && header->length == sizeof(usb_interface_desc_t)) {
            const usb_interface_desc_t *ifc = (const usb_interface_desc_t *)curr_ptr;
#if 0
            print_usb_info("interface %i class %i subclass %i protocol %i",
                           ifc->interface_num, ifc->class, ifc->subclass, ifc->protocol);
#endif
            if (ifc->class == 3 && ifc->subclass == 1 && ifc->protocol == 1) {
                kbd = &keyboards[*num_keyboards];
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
                kbd = NULL;

                *num_keyboards += 1;
            }
        }
        curr_ptr = next_ptr;
    }
}

static bool configure_device(const usb_hcd_t *hcd, const usb_ep_t *ep0, int config_num)
{
    usb_setup_pkt_t setup_pkt;

    build_setup_packet(&setup_pkt, USB_REQ_TO_DEVICE, USB_SET_CONFIGURATION, config_num, 0, 0);
    return hcd->methods->setup_request(hcd, ep0, &setup_pkt);
}

static bool configure_keyboard(const usb_hcd_t *hcd, const usb_ep_t *ep0, int interface_num)
{
    usb_setup_pkt_t setup_pkt;

    // Set the idle duration to infinite.
    build_setup_packet(&setup_pkt, USB_REQ_TO_INTERFACE | USB_REQ_CLASS, HID_SET_IDLE, 0, interface_num, 0);
    if (!hcd->methods->setup_request(hcd, ep0, &setup_pkt)) {
        return false;
    }

    // Select the boot protocol.
    build_setup_packet(&setup_pkt, USB_REQ_TO_INTERFACE | USB_REQ_CLASS, HID_SET_PROTOCOL, 0, interface_num, 0);
    if (!hcd->methods->setup_request(hcd, ep0, &setup_pkt)) {
        return false;
    }

    return true;
}

static bool scan_hub_ports(const usb_hcd_t *hcd, const usb_hub_t *hub, int *num_devices,
                           usb_ep_t keyboards[], int max_keyboards, int *num_keyboards)
{
    bool keyboard_found = false;

    usb_setup_pkt_t setup_pkt;

    // Power up all the ports.
    build_setup_packet(&setup_pkt, USB_REQ_TO_HUB_PORT | USB_REQ_CLASS, HUB_SET_FEATURE, HUB_PORT_POWER, 0, 0);
    for (int port_num = 1; port_num <= hub->num_ports; port_num++) {
        setup_pkt.index = port_num;
        if (!hcd->methods->setup_request(hcd, hub->ep0, &setup_pkt)) {
            return false;
        }
    }
    usleep(hub->power_up_delay * 2 * MILLISEC);

    usleep(100*MILLISEC);  // USB maximum device attach time.

    // Scan the ports, looking for hubs and keyboards.
    for (int port_num = 1; port_num <= hub->num_ports; port_num++) {
        // If we've filled the keyboard info table, abort now.
        if (*num_keyboards >= max_keyboards) break;

        uint32_t port_status;

        get_hub_port_status(hcd, hub, port_num, &port_status);

        // Check the port is powered up.
        if (~port_status & HUB_PORT_POWERED) continue;

        // Check if anything is connected to this port.
        if (~port_status & HUB_PORT_CONNECTED) continue;

        if (!reset_usb_hub_port(hcd, hub, port_num)) continue;

        get_hub_port_status(hcd, hub, port_num, &port_status);

        // Check the port is active.
        if (~port_status & HUB_PORT_CONNECTED) continue;
        if (~port_status & HUB_PORT_ENABLED)   continue;

        // Now the port has been enabled, we can determine the device speed.
        usb_speed_t device_speed;
        if        (port_status & HUB_PORT_LOW_SPEED) {
            device_speed = USB_SPEED_LOW;
        } else if (port_status & HUB_PORT_HIGH_SPEED) {
            device_speed = USB_SPEED_HIGH;
        } else {
            device_speed = USB_SPEED_FULL;
        }

        *num_devices += 1;

        // By default, using the incrementing count of devices as the device ID.
        int device_id = *num_devices;

        // Allocate a controller slot for this device (only needed for some controllers).
        if (hcd->methods->allocate_slot) {
            device_id = hcd->methods->allocate_slot(hcd);
            if (device_id == 0) break;
        }

        // Look for keyboards attached directly or indirectly to this port.
        if (find_attached_usb_keyboards(hcd, hub, port_num, device_speed, device_id, num_devices,
                                        keyboards, max_keyboards, num_keyboards)) {
            keyboard_found = true;
            continue;
        }

        // If we didn't find any keyboards, we can disable the port and release the slot.
        build_setup_packet(&setup_pkt, USB_REQ_TO_HUB_PORT | USB_REQ_CLASS, HUB_CLR_FEATURE, HUB_PORT_ENABLE, port_num, 0);
        (void)hcd->methods->setup_request(hcd, hub->ep0, &setup_pkt);

        if (hcd->methods->release_slot) {
            (void)hcd->methods->release_slot(hcd, device_id);
        }
    }

    return keyboard_found;
}

static void probe_usb_controller(int bus, int dev, int func, hci_type_t controller_type)
{
    uint16_t vendor_id  = pci_config_read16(bus, dev, func, 0x00);
    uint16_t device_id  = pci_config_read16(bus, dev, func, 0x02);
    uint16_t pci_status = pci_config_read16(bus, dev, func, 0x06);

    // Disable access to the device while we probe it.
    uint16_t pci_command = pci_config_read16(bus, dev, func, 0x04);
    pci_config_write16(bus, dev, func, 0x04, pci_command & ~0x0003);

    int bar = (controller_type == UHCI) ? 0x20 : 0x10;
    uintptr_t base_addr = pci_config_read32(bus, dev, func, bar);
    pci_config_write32(bus, dev, func, bar, 0xffffffff);
    uintptr_t mmio_size = pci_config_read32(bus, dev, func, bar);
    pci_config_write32(bus, dev, func, bar, base_addr);
    bool in_io_space = base_addr & 0x1;
#ifdef __x86_64__
    if (!in_io_space && (base_addr & 0x4)) {
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

    // Restore access to the device and set the bus master flag in case the BIOS hasn't.
    pci_config_write16(bus, dev, func, 0x04, pci_command | (in_io_space ? 0x0005 : 0x0006));

    print_usb_info("Found %s controller %04x:%04x at %08x size %08x in %s space", hci_name[controller_type],
                   (uintptr_t)vendor_id, (uintptr_t)device_id, base_addr, mmio_size, in_io_space ? "I/O" : "Mem");

    if (in_io_space) {
        if (controller_type != UHCI) {
            print_usb_info(" Unsupported address mapping for this controller type");
            return;
        }
    } else {
        if (controller_type == UHCI) {
            print_usb_info(" Unsupported address mapping for this controller type");
            return;
        }
        base_addr = map_region(base_addr, mmio_size, false);
        if (base_addr == 0) {
            print_usb_info(" Failed to map device into virtual memory");
            return;
        }
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

    // Initialise the device according to its type.
    bool keyboards_found = false;
    if (controller_type == UHCI) {
        keyboards_found = uhci_init(bus, dev, func, base_addr, &usb_controllers[num_usb_controllers]);
    }
    if (controller_type == OHCI) {
        keyboards_found = ohci_init(base_addr, &usb_controllers[num_usb_controllers]);
    }
    if (controller_type == EHCI) {
        keyboards_found = ehci_init(bus, dev, func, base_addr, &usb_controllers[num_usb_controllers]);
    }
    if (controller_type == XHCI) {
        keyboards_found = xhci_init(base_addr, &usb_controllers[num_usb_controllers]);
    }
    if (keyboards_found) {
        num_usb_controllers++;
    }
}

//------------------------------------------------------------------------------
// Shared Functions (used by all drivers)
//------------------------------------------------------------------------------

uint32_t usb_route(const usb_hub_t *hub, int port_num)
{
    if (hub->level == 0) {
        return port_num << 24;
    }
    if (hub->level > 5) {
        port_num = 0;
    } else if (port_num > 15) {
        port_num = 15;
    }
    return hub->route | (port_num << (4 * (hub->level - 1)));
}

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

void print_usb_info(const char *fmt, ...)
{
    if (print_row == SCREEN_HEIGHT) {
        scroll_screen_region(0, 0, SCREEN_HEIGHT - 1, SCREEN_WIDTH - 1);
        print_row--;
    }

    va_list args;

    va_start(args, fmt);
    (void)vprintf(print_row++, print_col, fmt, args);
    va_end(args);
}

bool reset_usb_hub_port(const usb_hcd_t *hcd, const usb_hub_t *hub, int port_num)
{
    usb_setup_pkt_t setup_pkt;

    if (hub->level > 0) {
        build_setup_packet(&setup_pkt, USB_REQ_TO_HUB_PORT | USB_REQ_CLASS, HUB_SET_FEATURE,
                                       HUB_PORT_RESET, port_num, 0);
        if (!hcd->methods->setup_request(hcd, hub->ep0, &setup_pkt)) {
            return false;
        }
        int timer = 200;
        uint32_t port_status;
        do {
            usleep(1000);
            if (--timer == 0) return false;
            if (!get_hub_port_status(hcd, hub, port_num, &port_status)) {
                return false;
            }
        } while (port_status & HUB_PORT_RESETTING);
    } else {
        if (!hcd->methods->reset_root_hub_port(hcd, port_num)) {
            return false;
        }
    }

    usleep(10*MILLISEC);  // USB reset recovery time

    return true;
}

bool assign_usb_address(const usb_hcd_t *hcd, const usb_hub_t *hub, int port_num,
                        usb_speed_t device_speed, int device_id, usb_ep_t *ep0)
{
    usb_setup_pkt_t setup_pkt;

    uint8_t *data_buffer = hcd->ws->data_buffer;

    // If we've run out of USB addresses, abort now.
    if (device_id > USB_MAX_ADDRESS) {
        return false;
    }

    // Initialise the control endpoint descriptor.

    ep0->device_speed    = device_speed;
    ep0->device_id       = 0;
    ep0->interface_num   = 0;
    ep0->endpoint_num    = 0;
    ep0->max_packet_size = default_max_packet_size(device_speed);
    ep0->interval        = 0;

    // The device should currently be in Default state. For loww and full speed devices, We first fetch the first
    // 8 bytes of the device descriptor to discover the maximum packet size for the control endpoint. We then set
    // the device address, which moves the device into Address state, and fetch the full device descriptor.

    size_t fetch_length = sizeof(usb_device_desc_t);
    if (device_speed < USB_SPEED_HIGH) {
        fetch_length = 8;
        goto fetch_descriptor;
    }

  set_address:
    build_setup_packet(&setup_pkt, USB_REQ_TO_DEVICE, USB_SET_ADDRESS, device_id, 0, 0);
    if (!hcd->methods->setup_request(hcd, ep0, &setup_pkt)) {
        return false;
    }
    ep0->device_id = device_id;

    usleep(2*MILLISEC + 1*MILLISEC);  // USB set address recovery time (plus a bit).

  fetch_descriptor:
    build_setup_packet(&setup_pkt, USB_REQ_FROM_DEVICE, USB_GET_DESCRIPTOR, USB_DESC_DEVICE << 8, 0, fetch_length);
    if (!hcd->methods->get_data_request(hcd, ep0, &setup_pkt, data_buffer, fetch_length)
    ||  !valid_usb_device_descriptor(data_buffer)) {
        return false;
    }
#if 0
    print_usb_info("%02x %02x %02x %02x %02x %02x %02x %02x",
                   (uintptr_t)data_buffer[0],
                   (uintptr_t)data_buffer[1],
                   (uintptr_t)data_buffer[2],
                   (uintptr_t)data_buffer[3],
                   (uintptr_t)data_buffer[4],
                   (uintptr_t)data_buffer[5],
                   (uintptr_t)data_buffer[6],
                   (uintptr_t)data_buffer[7]);
#endif

    if (fetch_length == 8) {
        usb_device_desc_t *device = (usb_device_desc_t *)data_buffer;
        ep0->max_packet_size = device->max_packet_size;
        if (!valid_usb_max_packet_size(device->max_packet_size, device_speed)) {
            return false;
        }
        if (usb_init_options & USB_EXTRA_RESET) {
            if (!reset_usb_hub_port(hcd, hub, port_num)) {
                return false;
            }
        }
        fetch_length = sizeof(usb_device_desc_t);
        goto set_address;
    }

    hcd->ws->data_length = fetch_length;

    return true;
}

bool find_attached_usb_keyboards(const usb_hcd_t *hcd, const usb_hub_t *hub, int port_num, 
                                 usb_speed_t device_speed, int device_id, int *num_devices,
                                 usb_ep_t keyboards[], int max_keyboards, int *num_keyboards)
{
    bool keyboard_found = false;

    // Set the USB device address. If successful, this also fills in the descriptor for the default control endpoint
    // (ep0) and leaves the device descriptor in the data transfer buffer.
    usb_ep_t ep0;
    if (!hcd->methods->assign_address(hcd, hub, port_num, device_speed, device_id, &ep0)) {
        return false;
    }
    usb_device_desc_t *device = (usb_device_desc_t *)hcd->ws->data_buffer;
    bool is_hub = (device->class == USB_CLASS_HUB);

    // Fetch the descriptors for the first configuration into the data transfer buffer. In theory a keyboard device
    // may have more than one configuration and may only support the boot protocol in another configuration, but
    // this seems unlikely in practice. A hub should only ever have one configuration.
    int config_num = get_configuration_descriptors(hcd, &ep0, 0);
    if (config_num == 0) {
        return false;
    }

    if (is_hub) {
        usb_hub_t new_hub;
        usb_ep_t  ep1;
        if (!build_hub_info(hcd, hub, port_num, &ep0, &new_hub, &ep1)) {
            return false;
        }
        if (!configure_device(hcd, &ep0, config_num)) {
            return false;
        }
        if (hcd->methods->configure_hub_ep) {
            if (!hcd->methods->configure_hub_ep(hcd, &ep1, &new_hub)) {
                return false;
            }
        }
        print_usb_info(" %i port hub found on port %i", new_hub.num_ports, port_num);
        print_col += 1;
        keyboard_found = scan_hub_ports(hcd, &new_hub, num_devices, keyboards, max_keyboards, num_keyboards);
        print_col -= 1;
    } else {
        // Scan the configuration to see if this device has one or more interfaces that implement the keyboard
        // boot protocol and if so, record that information in the keyboard info table and configure the device.
        int old_num_keyboards = *num_keyboards;
        int new_num_keyboards = *num_keyboards;
        get_keyboard_info_from_descriptors(hcd->ws->data_buffer, hcd->ws->data_length,
                                           keyboards, max_keyboards, &new_num_keyboards);
        if (new_num_keyboards == old_num_keyboards) {
            return false;
        }
        if (!configure_device(hcd, &ep0, config_num)) {
            return false;
        }

        // Complete the new entries in the keyboard info table and configure the keyboard interfaces.
        for (int kbd_idx = old_num_keyboards; kbd_idx < new_num_keyboards; kbd_idx++) {
            usb_ep_t *kbd = &keyboards[kbd_idx];
            kbd->driver_data  = ep0.driver_data;
            kbd->device_speed = device_speed;
            kbd->device_id    = device_id;
            if (hcd->methods->configure_kbd_ep) {
                if (!hcd->methods->configure_kbd_ep(hcd, kbd, kbd_idx)) {
                    return false;
                }
            }
            if (!configure_keyboard(hcd, &ep0, kbd->interface_num)) break;

            print_usb_info(" Keyboard found on port %i interface %i endpoint %i",
                           port_num, kbd->interface_num, kbd->endpoint_num);

            keyboard_found = true;
            *num_keyboards += 1;
        }
    }

    return keyboard_found;
}

bool process_usb_keyboard_report(const usb_hcd_t *hcd, const hid_kbd_rpt_t *report, const hid_kbd_rpt_t *prev_report)
{
    hcd_workspace_t *ws = hcd->ws;

    int error_count = 0;
    for (int i = 0; i < 6; i++) {
        uint8_t key_code = report->key_code[i];
        if (key_code > 0x03) {
            // Check if we've already seen this key press.
            for (int j = 0; j < 6; j++) {
                if (prev_report->key_code[j] == key_code) {
                    key_code = 0;
                    break;
                }
            }
            // If not, put it in the key code buffer.
            if (key_code != 0) {
                int kc_index_i = ws->kc_index_i;
                int kc_index_n = (kc_index_i + 1) % HCD_KC_BUFFER_SIZE;
                if (kc_index_n != ws->kc_index_o) {
                    ws->kc_buffer[kc_index_i] = key_code;
                    ws->kc_index_i = kc_index_n;
                }
            }
        } else if (key_code != 0x00) {
            error_count++;
        }
    }
    return error_count < 6;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void find_usb_keyboards(bool pause_if_none)
{
    clear_screen();
    print_usb_info("Scanning for USB keyboards...");

    num_usb_controllers = 0;
    for (int bus = 0; bus < PCI_MAX_BUS; bus++) {
        for (int dev = 0; dev < PCI_MAX_DEV; dev++) {
            hci_type_t controller_type[PCI_MAX_FUNC];
            for (int func = 0; func < PCI_MAX_FUNC; func++) {
                controller_type[func] = NOT_HCI;
            }
            for (int func = 0; func < PCI_MAX_FUNC; func++) {
                // Test for device/function present.
                uint16_t vendor_id = pci_config_read16(bus, dev, func, 0x00);
                uint8_t  hdr_type  = pci_config_read8 (bus, dev, func, 0x0e);
                if (vendor_id != 0xffff) {
                    // Test for a USB controller.
                    uint16_t class_code = pci_config_read16(bus, dev, func, 0x0a);
                    if (class_code == 0x0c03) {
                        controller_type[func] = pci_config_read8(bus, dev, func, 0x09) >> 4;
                        // We need to initialise EHCI controllers before initialising any of their companion
                        // controllers, so do it now.
                        if (controller_type[func] == EHCI) {
                            if (~usb_init_options & USB_IGNORE_EHCI) {
                                probe_usb_controller(bus, dev, func, controller_type[func]);
                            }
                            // If we've filled the controller table, abort now.
                            if (num_usb_controllers == MAX_USB_CONTROLLERS) {
                                return;
                            }
                            controller_type[func] = NOT_HCI;  // prevent reprobing
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
            for (int func = 0; func < PCI_MAX_FUNC; func++) {
                if (controller_type[func] != NOT_HCI) {
                    probe_usb_controller(bus, dev, func, controller_type[func]);
                    // If we've filled the controller table, abort now.
                    if (num_usb_controllers == MAX_USB_CONTROLLERS) {
                        return;
                    }
                }
            }
        }
    }

    if (usb_init_options & USB_DEBUG) {
        print_usb_info("Press any key to continue...");
        while (get_key() == 0) {}
    } else if (pause_if_none && num_usb_controllers == 0) {
        for (int i = PAUSE_IF_NONE_TIME; i > 0; i--) {
            print_usb_info("No USB keyboards found. Continuing in %i second%c ", i, i == 1 ? ' ' : 's');
            sleep(1);
            print_row--; // overwrite message
        }
    }
}

uint8_t get_usb_keycode(void)
{
    for (int i = 0; i < num_usb_controllers; i++) {
        const usb_hcd_t *hcd = &usb_controllers[i];

        usb_controllers[i].methods->poll_keyboards(hcd);

        int kc_index_o = hcd->ws->kc_index_o;
        if (kc_index_o != hcd->ws->kc_index_i) {
            hcd->ws->kc_index_o = (kc_index_o + 1) % HCD_KC_BUFFER_SIZE;
            return hcd->ws->kc_buffer[kc_index_o];
        }
    }
    return 0;
}
