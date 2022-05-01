// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2021-2022 Martin Whitaker.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "memrw32.h"
#include "memsize.h"
#include "pci.h"
#include "pmem.h"
#include "usb.h"

#include "string.h"
#include "unistd.h"

#include "uhci.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// Values defined by the UHCI specification.

// Basic limits

#define UHCI_FL_LENGTH          1024                // Maximum number of entries in periodic frame list

// Register addresses in PCI Config space

#define UHCI_LEGSUP             0xc0                // Legacy Support register

// Legacy Support register

#define UHCI_LEGSUP_TBY60R      0x0100              // Trap By 0x60 Read  Status/Clear
#define UHCI_LEGSUP_TBY60W      0x0200              // Trap By 0x60 Write Status/Clear
#define UHCI_LEGSUP_TBY64R      0x0400              // Trap By 0x64 Read  Status/Clear
#define UHCI_LEGSUP_TBY64W      0x0800              // Trap By 0x64 Write Status/Clear

#define UHCI_LEGSUP_A20PTS      0x8000              // End of A20GATE Pass Through Status/Clear

#define UHCI_LEGSUP_CLEAR       (UHCI_LEGSUP_TBY60R | UHCI_LEGSUP_TBY60W | UHCI_LEGSUP_TBY64R | UHCI_LEGSUP_TBY64W | UHCI_LEGSUP_A20PTS)

// Register addresses in I/O space

#define UHCI_USBCMD             (io_base+0x00)      // USB Command register
#define UHCI_USBSTS             (io_base+0x02)      // USB Status register
#define UHCI_USBINTR            (io_base+0x04)      // USB Interrupt Enable register
#define UHCI_FRNUM              (io_base+0x06)      // Frame Number register
#define UHCI_FLBASE             (io_base+0x08)      // Frame List Base Address register
#define UHCI_SOF                (io_base+0x0c)      // Start of Frame modify register
#define UHCI_PORT_SC(n)         (io_base+0x10+2*n)  // Port n Status and Control register (n = 0..MaxPortIdx)

// USB Command register

#define UHCI_USBCMD_R_S         0x0001              // Run/Stop
#define UHCI_USBCMD_HCR         0x0002              // Host Controller Reset
#define UHCI_USBCMD_GR          0x0004              // Global Reset
#define UHCI_USBCMD_MAXP        0x0080              // Max Packet

// USB Status register 

#define UHCI_USBSTS_INT         0x0001              // Interrupt
#define UHCI_USBSTS_ERR         0x0002              // Error interrupt
#define UHCI_USBSTS_RESUME      0x0004              // Resume Detect
#define UHCI_USBSTS_HSE         0x0008              // Host System Error
#define UHCI_USBSTS_HCPE        0x0010              // Host Controller Processor Error
#define UHCI_USBSTS_HCH         0x0020              // Host Controller Halted

// USB Interrupt Enable register

#define UHCI_USBINTR_NONE       0x0000              // No interrupts enabled

// USB SOF register

#define UHCI_SOF_DEFAULT        0x40                // Default timing (1ms with 12MHz clock)

// Port Status and Control register

#define UHCI_PORT_SC_CCS        0x0001              // Current Connect Status
#define UHCI_PORT_SC_CCSC       0x0002              // Current Connect Status Change
#define UHCI_PORT_SC_PED        0x0004              // Port Enable/Disable
#define UHCI_PORT_SC_PEDC       0x0008              // Port Enable/Disable Change
#define UHCI_PORT_SC_RESUME     0x0040              // Resume Detect
#define UHCI_PORT_SC_VALID      0x0080              // Reserved bit (always set)
#define UHCI_PORT_SC_LSDA       0x0100              // Low Speed Device Attached
#define UHCI_PORT_SC_PR         0x0200              // Port Reset
#define UHCI_PORT_SC_SUSPEND    0x1000              // Suspend

// Link Pointer

#define UHCI_LP_TERMINATE       0x00000001          // Terminate (T) bit

#define UHCI_LP_TYPE_TD         (0 << 1)            // Type is Transfer Descriptor
#define UHCI_LP_TYPE_QH         (1 << 1)            // Type is Queue Head

#define UHCI_LP_BREADTH_FIRST   (0 << 2)
#define UHCI_LP_DEPTH_FIRST     (1 << 2)

// Queue Element Transfer Descriptor data structure

// - control_status member (32 bits)

#define UHCI_TD_BS_ERR          0x00020000          // Bitstuff Error
#define UHCI_TD_CRC_TO          0x00040000          // CRC or Time Out Error
#define UHCI_TD_NAK_RX          0x00080000          // NAK received
#define UHCI_TD_BABBLE          0x00100000          // Babble Detected
#define UHCI_TD_DB_ERR          0x00200000          // Data Buffer Error
#define UHCI_TD_STALLED         0x00400000          // Halted
#define UHCI_TD_ACTIVE          0x00800000          // Active

#define UHCI_TD_IOC_N           (0 << 24)           // Interrupt On Completion is off
#define UHCI_TD_IOC_Y           (1 << 24)           // Interrupt On Completion is on

#define UHCI_TD_ISO             (1 << 25)           // Isochronous Select

#define UHCI_TD_FULL_SPEED      (0 << 26)           // Full Speed
#define UHCI_TD_LOW_SPEED       (1 << 26)           // Low Speed

#define UHCI_TD_CERR(n)         ((n) << 27)         // Error Counter = n (n = 1..3)

#define UHCI_TD_SPD             (1 << 29)           // Short Packet Detect

// - token member (32 bits)

#define UHCI_TD_PID_OUT         0x000000e1          // PID Code is OUT
#define UHCI_TD_PID_IN          0x00000069          // PID Code is IN
#define UHCI_TD_PID_SETUP       0x0000002d          // PID Code is SETUP

#define UHCI_TD_DEVICE_ADDR(n)  ((n) << 8)          // Device Address = n (n = 0..127)

#define UHCI_TD_ENDPOINT(n)     ((n) << 15)         // Endpoint = n (n = 0..15)

#define UHCI_TD_DT(n)           ((n) << 19)         // Data Toggle = n (n = 0,1)

#define UHCI_TD_LENGTH(n)       ((((n) - 1) & 0x7ff) << 21)

// Values specific to this driver.

#define MAX_UHCI_PORTS          8                   // the UHCI spec. doesn't define this, so pick a sensible number

#define MAX_KEYBOARDS           8                   // per host controller

#define MAX_PACKETS             32                  // per data transfer (must be >= MAX_KEYBOARDS)

#define WS_QH_SIZE              (1 + MAX_KEYBOARDS) // Queue Head Descriptors
#define WS_TD_SIZE              (2 + MAX_PACKETS)   // Queue Transfer Descriptors

#define MILLISEC                1000                // in microseconds

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

// Data structures defined by the UHCI specification.

typedef volatile struct {
    uint32_t            link_ptr;
    uint32_t            control_status;
    uint32_t            token;
    uint32_t            buffer_ptr;
    uint32_t            driver_data[4];
} uhci_td_t  __attribute__ ((aligned (16)));

typedef volatile struct {
    uint32_t            qh_link_ptr;
    uint32_t            qe_link_ptr;
    uint32_t            padding[2];
} uhci_qh_t  __attribute__ ((aligned (16)));

// Data structures specific to this implementation.

typedef struct {
    hcd_workspace_t     base_ws;

    // System memory data structures used by the host controller.
    uhci_qh_t           qh[WS_QH_SIZE]  __attribute__ ((aligned (16)));
    uhci_td_t           td[WS_TD_SIZE]  __attribute__ ((aligned (16)));

    // Keyboard data transfer buffers.
    hid_kbd_rpt_t       kbd_rpt[MAX_KEYBOARDS];

    // Saved keyboard reports.
    hid_kbd_rpt_t       prev_kbd_rpt[MAX_KEYBOARDS];

    // I/O base address of the host controller registers.
    uint16_t            io_base;

    // Number of keyboards detected.
    int                 num_keyboards;
} workspace_t  __attribute__ ((aligned (256)));

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static size_t num_pages(size_t size)
{
    return (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

static bool io_wait_until_clr(uint16_t io_reg, uint16_t bit_mask, int max_time)
{
    int timer = max_time >> 3;
    while (inw(io_reg) & bit_mask) {
        if (timer == 0) return false;
        usleep(8);
        timer--;
    }
    return true;
}

static bool io_wait_until_set(uint16_t io_reg, uint16_t bit_mask, int max_time)
{
    int timer = max_time >> 3;
    while (~inw(io_reg) & bit_mask) {
        if (timer == 0) return false;
        usleep(8);
        timer--;
    }
    return true;
}

static bool reset_host_controller(uint16_t io_base)
{
    outw(inw(UHCI_USBCMD) | UHCI_USBCMD_HCR, UHCI_USBCMD);

    usleep(1*MILLISEC);  // allow the controller some time to recover from reset

    return io_wait_until_clr(UHCI_USBCMD, UHCI_USBCMD_HCR, 1000*MILLISEC);
}

static bool start_host_controller(uint16_t io_base)
{
    outw(UHCI_USBCMD_R_S | UHCI_USBCMD_MAXP, UHCI_USBCMD);
    return io_wait_until_clr(UHCI_USBSTS, UHCI_USBSTS_HCH, 1000*MILLISEC);
}

static bool halt_host_controller(uint16_t io_base)
{
    outw(inw(UHCI_USBCMD) & ~UHCI_USBCMD_R_S, UHCI_USBCMD);
    return io_wait_until_set(UHCI_USBSTS, UHCI_USBSTS_HCH, 1000*MILLISEC);
}

static bool reset_uhci_port(uint16_t io_base, int port_idx)
{
    uint16_t port_status = inw(UHCI_PORT_SC(port_idx)) & ~UHCI_PORT_SC_PED;
    outw(port_status |  UHCI_PORT_SC_PR, UHCI_PORT_SC(port_idx));

    usleep(50*MILLISEC);  // USB port reset time

    outw(port_status & ~UHCI_PORT_SC_PR, UHCI_PORT_SC(port_idx));
    return io_wait_until_clr(UHCI_PORT_SC(port_idx), UHCI_PORT_SC_PR, 5*MILLISEC);
}

static bool enable_uhci_port(uint16_t io_base, int port_idx)
{
    uint16_t port_status = inw(UHCI_PORT_SC(port_idx));
    outw(port_status | UHCI_PORT_SC_PED, UHCI_PORT_SC(port_idx));
    return io_wait_until_set(UHCI_PORT_SC(port_idx), UHCI_PORT_SC_PED, 1000*MILLISEC);
}

static void disable_uhci_port(uint16_t io_base, int port_idx)
{
    uint16_t port_status = inw(UHCI_PORT_SC(port_idx));
    outw(port_status & ~UHCI_PORT_SC_PED, UHCI_PORT_SC(port_idx));
    (void)io_wait_until_clr(UHCI_PORT_SC(port_idx), UHCI_PORT_SC_PED, 1000*MILLISEC);
}

static void build_uhci_td(uhci_td_t *td, const usb_ep_t *ep, uint32_t pid, uint32_t dt, uint32_t options,
                          const void *buffer, size_t length)
{
    uint32_t device_speed = (ep->device_speed == USB_SPEED_LOW) ? UHCI_TD_LOW_SPEED : UHCI_TD_FULL_SPEED;

    if (options & UHCI_TD_IOC_Y) {
        td->link_ptr = UHCI_LP_TERMINATE;
    } else {
        td->link_ptr = (uintptr_t)(td + 1) | UHCI_LP_TYPE_TD | UHCI_LP_DEPTH_FIRST;
    }
    td->control_status = UHCI_TD_CERR(3)
                       | device_speed
                       | options
                       | UHCI_TD_ACTIVE;
    td->token          = UHCI_TD_LENGTH(length)
                       | dt
                       | UHCI_TD_ENDPOINT(ep->endpoint_num)
                       | UHCI_TD_DEVICE_ADDR(ep->device_id)
                       | pid;
    td->buffer_ptr     = (uintptr_t)buffer;
}

static uint16_t get_uhci_done(workspace_t *ws)
{
    uint16_t io_base = ws->io_base;

    uint16_t status = inw(UHCI_USBSTS) & (UHCI_USBSTS_INT | UHCI_USBSTS_ERR);
    if (status != 0) {
        if (status & UHCI_USBSTS_ERR || ws->qh[0].qe_link_ptr != UHCI_LP_TERMINATE) {
#if 1
            uintptr_t td_addr = ws->qh[0].qe_link_ptr & 0xfffffff0;
            uhci_td_t *td = (uhci_td_t *)td_addr;
            print_usb_info(" transfer failed TD %08x status %08x token %08x",
                           td_addr, (uintptr_t)td->control_status, (uintptr_t)td->token);
#endif
            write32(&ws->qh[0].qe_link_ptr, UHCI_LP_TERMINATE);
            status |= UHCI_USBSTS_ERR;
        }
        outw(UHCI_USBSTS_INT | UHCI_USBSTS_ERR, UHCI_USBSTS);
    }
    return status;
}

static bool wait_for_uhci_done(workspace_t *ws)
{
    // Rely on the controller to timeout if the device doesn't respond.
    uint16_t status = 0;
    while (true) {
        status = get_uhci_done(ws);
        if (status != 0) break;
        usleep(10);
    }

    return ~status & UHCI_USBSTS_ERR;
}

//------------------------------------------------------------------------------
// Driver Methods
//------------------------------------------------------------------------------

static bool reset_root_hub_port(const usb_hcd_t *hcd, int port_num)
{
    const workspace_t *ws = (const workspace_t *)hcd->ws;

    return reset_uhci_port(ws->io_base, port_num - 1);
}

static bool setup_request(const usb_hcd_t *hcd, const usb_ep_t *ep, const usb_setup_pkt_t *setup_pkt)
{
    workspace_t *ws = (workspace_t *)hcd->ws;

    write32(&ws->qh[0].qe_link_ptr, UHCI_LP_TERMINATE);
    build_uhci_td(&ws->td[0], ep, UHCI_TD_PID_SETUP, UHCI_TD_DT(0), UHCI_TD_IOC_N, setup_pkt, sizeof(usb_setup_pkt_t));
    build_uhci_td(&ws->td[1], ep, UHCI_TD_PID_IN,    UHCI_TD_DT(1), UHCI_TD_IOC_Y, 0, 0);
    write32(&ws->qh[0].qe_link_ptr, (uintptr_t)(&ws->td[0]) | UHCI_LP_TYPE_TD);
    return wait_for_uhci_done(ws);
}

static bool get_data_request(const usb_hcd_t *hcd, const usb_ep_t *ep, const usb_setup_pkt_t *setup_pkt,
                             const void *buffer, size_t length)
{
    workspace_t *ws = (workspace_t *)hcd->ws;

    size_t packet_size = ep->max_packet_size;
    if (length > (MAX_PACKETS * packet_size)) {
        return false;
    }

    write32(&ws->qh[0].qe_link_ptr, UHCI_LP_TERMINATE);
    int pkt_num = 0;
    build_uhci_td(&ws->td[pkt_num], ep, UHCI_TD_PID_SETUP, UHCI_TD_DT(pkt_num & 1), UHCI_TD_IOC_N, setup_pkt, sizeof(usb_setup_pkt_t)); pkt_num++;
    while (length > packet_size) {
        build_uhci_td(&ws->td[pkt_num], ep, UHCI_TD_PID_IN, UHCI_TD_DT(pkt_num & 1), UHCI_TD_SPD, buffer, packet_size); pkt_num++;
        buffer = (uint8_t *)buffer + packet_size;
        length -= packet_size;
    }
    build_uhci_td(&ws->td[pkt_num], ep, UHCI_TD_PID_IN, UHCI_TD_DT(pkt_num & 1), UHCI_TD_IOC_N, buffer, length); pkt_num++;
    build_uhci_td(&ws->td[pkt_num], ep, UHCI_TD_PID_OUT, UHCI_TD_DT(1), UHCI_TD_IOC_Y, 0, 0);
    write32(&ws->qh[0].qe_link_ptr, (uintptr_t)(&ws->td[0]) | UHCI_LP_TYPE_TD);
    return wait_for_uhci_done(ws);
}

static void poll_keyboards(const usb_hcd_t *hcd)
{
    workspace_t *ws = (workspace_t *)hcd->ws;
    uint16_t io_base = ws->io_base;

    uint32_t status = inw(UHCI_USBSTS) & (UHCI_USBSTS_INT | UHCI_USBSTS_ERR);
    if (status != 0) {
        outw(UHCI_USBSTS_INT | UHCI_USBSTS_ERR, UHCI_USBSTS);

        for (int kbd_idx = 0; kbd_idx < ws->num_keyboards; kbd_idx++) {
            uhci_qh_t *kbd_qh = &ws->qh[1 + kbd_idx];
            uhci_td_t *kbd_td = &ws->td[2 + kbd_idx];

            status = kbd_td->control_status;
            if (status & UHCI_TD_ACTIVE) continue;

            hid_kbd_rpt_t *kbd_rpt = &ws->kbd_rpt[kbd_idx];

            uint32_t error_mask = UHCI_TD_STALLED | UHCI_TD_DB_ERR | UHCI_TD_BABBLE | UHCI_TD_NAK_RX | UHCI_TD_CRC_TO | UHCI_TD_BS_ERR;
            if (~status & error_mask) {
                hid_kbd_rpt_t *prev_kbd_rpt = &ws->prev_kbd_rpt[kbd_idx];
                if (process_usb_keyboard_report(hcd, kbd_rpt, prev_kbd_rpt)) {
                    *prev_kbd_rpt = *kbd_rpt;
                }

                write32(&kbd_td->token, read32(&kbd_td->token) ^ UHCI_TD_DT(1));
            }

            // Reenable the TD.
            write32(&kbd_td->control_status, kbd_td->driver_data[0]);
            write32(&kbd_qh->qe_link_ptr, (uintptr_t)kbd_td | UHCI_LP_TYPE_TD);
        }
    }
}

//------------------------------------------------------------------------------
// Driver Method Table
//------------------------------------------------------------------------------

static const hcd_methods_t methods = {
    .reset_root_hub_port = reset_root_hub_port,
    .allocate_slot       = NULL,
    .release_slot        = NULL,
    .assign_address      = assign_usb_address,  // use default method
    .configure_hub_ep    = NULL,
    .configure_kbd_ep    = NULL,
    .setup_request       = setup_request,
    .get_data_request    = get_data_request,
    .poll_keyboards      = poll_keyboards
};

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

bool uhci_init(int bus, int dev, int func, uint16_t io_base, usb_hcd_t *hcd)
{
    // Disable PCI and SMM interrupts.
    pci_config_write16(bus, dev, func, UHCI_LEGSUP, UHCI_LEGSUP_CLEAR);

    // Ensure the controller is halted and then reset it.
    if (!halt_host_controller(io_base)) return false;
    if (!reset_host_controller(io_base)) return false;

    // Allocate and initialise the frame list. This needs to be aligned on a 4K page boundary.
    pm_map[0].end -= num_pages(UHCI_FL_LENGTH * sizeof(uint32_t));
    uintptr_t fl_addr = pm_map[0].end << PAGE_SHIFT;
    uint32_t *fl = (uint32_t *)fl_addr;

    // Allocate and initialise a workspace for this controller. This needs to be permanently mapped into virtual memory,
    // so allocate it in the first segment.
    // TODO: check for segment overflow.
    pm_map[0].end -= num_pages(sizeof(workspace_t));
    uintptr_t workspace_addr = pm_map[0].end << PAGE_SHIFT;
    workspace_t *ws = (workspace_t *)workspace_addr;

    memset(ws, 0, sizeof(workspace_t));

    ws->io_base = io_base;

    // Initialise the asynchronous queue head.
    ws->qh[0].qh_link_ptr = UHCI_LP_TERMINATE;
    ws->qh[0].qe_link_ptr = UHCI_LP_TERMINATE;

    // Initialise the frame list to execute the asynchronous queue.
    for (int i = 0; i < UHCI_FL_LENGTH; i++) {
        fl[i] = (uintptr_t)(&ws->qh[0]) | UHCI_LP_TYPE_QH;
    }

    // Initialise the driver object for this controller.
    hcd->methods = &methods;
    hcd->ws      = &ws->base_ws;

    // Initialise the host controller.
    outw(UHCI_USBINTR_NONE, UHCI_USBINTR);
    outw(0,                 UHCI_FRNUM);
    outl(fl_addr,           UHCI_FLBASE);
    outb(UHCI_SOF_DEFAULT,  UHCI_SOF);
    if (!start_host_controller(io_base)) {
        pm_map[0].end += num_pages(sizeof(workspace_t));
        pm_map[0].end += num_pages(UHCI_FL_LENGTH * sizeof(uint32_t));
        return false;
    }

    // Construct a hub descriptor for the root hub.
    usb_hub_t root_hub;
    root_hub.ep0            = NULL;
    root_hub.level          = 0;
    root_hub.route          = 0;
    root_hub.num_ports      = MAX_UHCI_PORTS;
    root_hub.power_up_delay = 0;

    usleep(100*MILLISEC);  // USB maximum device attach time

    // Scan the ports, looking for hubs and keyboards.
    usb_ep_t keyboards[MAX_KEYBOARDS];
    int num_keyboards = 0;
    int num_devices = 0;
    for (int port_idx = 0; port_idx < root_hub.num_ports; port_idx++) {
        // If we've filled the keyboard info table, abort now.
        if (num_keyboards >= MAX_KEYBOARDS) break;

        // Check if we've passed the last port.
        uint16_t port_status = inw(UHCI_PORT_SC(port_idx));
        if ((~port_status & UHCI_PORT_SC_VALID) || port_status == 0xffff) {
            root_hub.num_ports = port_idx;
            break;
        }

        // Check if anything is connected to this port.
        if (~port_status & UHCI_PORT_SC_CCS) continue;

        // Reset the port.
        if (!reset_uhci_port(io_base, port_idx)) continue;

        usleep(10*MILLISEC);  // USB reset recovery time

        // Enable the port.
        if (!enable_uhci_port(io_base, port_idx)) continue;

        port_status = inw(UHCI_PORT_SC(port_idx));

        // Check the port is active.
        if (~port_status & UHCI_PORT_SC_PED) continue;

        num_devices++;

        // Get the port speed.
        usb_speed_t port_speed = port_status & UHCI_PORT_SC_LSDA ? USB_SPEED_LOW : USB_SPEED_FULL;

        // Look for keyboards attached directly or indirectly to this port.
        if (find_attached_usb_keyboards(hcd, &root_hub, 1 + port_idx, port_speed, num_devices,
                                        &num_devices, keyboards, MAX_KEYBOARDS, &num_keyboards)) {
            continue;
        }

        // If we didn't find any keyboard interfaces, we can disable the port.
        disable_uhci_port(io_base, port_idx);
    }

    print_usb_info(" Found %i device%s, %i keyboard%s",
                   num_devices,   num_devices   != 1 ? "s" : "",
                   num_keyboards, num_keyboards != 1 ? "s" : "");

    if (num_keyboards == 0) {
        // Halt the host controller.
        (void)halt_host_controller(io_base);

        // Deallocate the workspace for this controller.
        pm_map[0].end += num_pages(sizeof(workspace_t));

        // Deallocate the periodic frame list.
        pm_map[0].end += num_pages(UHCI_FL_LENGTH * sizeof(uint32_t));

        return false;
    }

    ws->num_keyboards = num_keyboards;

    // Initialise the interrupt QH and TD for each keyboard interface and find the minimum interval.
    int min_interval = UHCI_FL_LENGTH;
    uint32_t first_qh_ptr = UHCI_LP_TERMINATE;
    for (int kbd_idx = 0; kbd_idx < num_keyboards; kbd_idx++) {
        usb_ep_t *kbd = &keyboards[kbd_idx];

        uhci_qh_t *kbd_qh = &ws->qh[1 + kbd_idx];
        uhci_td_t *kbd_td = &ws->td[2 + kbd_idx];

        hid_kbd_rpt_t *kbd_rpt = &ws->kbd_rpt[kbd_idx];

        build_uhci_td(kbd_td, kbd, UHCI_TD_PID_IN, UHCI_TD_DT(0), UHCI_TD_IOC_Y, kbd_rpt, sizeof(hid_kbd_rpt_t));
        kbd_td->driver_data[0] = kbd_td->control_status;

        kbd_qh->qe_link_ptr = (uintptr_t)kbd_td | UHCI_LP_TYPE_TD;

        kbd_qh->qh_link_ptr = first_qh_ptr;
        first_qh_ptr = (uintptr_t)kbd_qh | UHCI_LP_TYPE_QH;

        if (kbd->interval < min_interval) {
            min_interval = kbd->interval;
        }
    }

    // Re-initialise the frame list to execute the periodic schedule.
    for (int i = 0; i < UHCI_FL_LENGTH; i++) {
        write32(&fl[i], UHCI_LP_TERMINATE);
    }
    for (int i = 0; i < UHCI_FL_LENGTH; i += min_interval) {
        write32(&fl[i], first_qh_ptr);
    }

    return true;
}
