// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2021-2022 Martin Whitaker.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memrw32.h"
#include "memsize.h"
#include "pci.h"
#include "pmem.h"
#include "usb.h"

#include "string.h"
#include "unistd.h"

#include "ehci.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// Values defined by the EHCI specification.

// Basic limits

#define EHCI_MAX_PFL_LENGTH     1024            // Maximum number of entries in periodic frame list

// Extended capability IDs

#define EHCI_EXT_CAP_OS_HANDOFF 0x01

// Host Controller Structural Parameters

#define EHCI_HCS_PPC            0x00000010      // Port Power Control

// USB Command register

#define EHCI_USBCMD_R_S         0x00000001      // Run/Stop
#define EHCI_USBCMD_HCR         0x00000002      // Host Controller Reset
#define EHCI_USBCMD_PSE         0x00000010      // Periodic Schedule Enable
#define EHCI_USBCMD_ASE         0x00000020      // Asynchronous Schedule Enable

#define EHCI_USBCMD_FLS_1024    (0 << 2)        // Frame List Size = 1024
#define EHCI_USBCMD_FLS_512     (1 << 2)        // Frame List Size = 512
#define EHCI_USBCMD_FLS_256     (2 << 2)        // Frame List Size = 256

#define EHCI_USBCMD_ITC(n)      ((n) << 16)     // Interrupt Threshold Control = n (n = 1,2,4,8,16,32,64)

// USB Status register 

#define EHCI_USBSTS_INT         0x00000001      // Interrupt
#define EHCI_USBSTS_ERR         0x00000002      // Error interrupt
#define EHCI_USBSTS_PCD         0x00000004      // Port Change Detect
#define EHCI_USBSTS_FLR         0x00000008      // Frame List Rollover
#define EHCI_USBSTS_HSE         0x00000010      // Host System Error
#define EHCI_USBSTS_AAI         0x00000020      // Asynchronous Advance Interrupt
#define EHCI_USBSTS_HCH         0x00001000      // Host Controller Halted
#define EHCI_USBSTS_ASE         0x00002000      // Asynchronous Schedule Empty
#define EHCI_USBSTS_PSS         0x00004000      // Periodic Schedule Status
#define EHCI_USBSTS_ASS         0x00008000      // Asynchronous Schedule Status

// Port Status and Control register 

#define EHCI_PORT_SC_CCS        0x00000001      // Current Connect Status
#define EHCI_PORT_SC_CCSC       0x00000002      // Current Connect Status Change
#define EHCI_PORT_SC_PED        0x00000004      // Port Enable/Disable
#define EHCI_PORT_SC_PEDC       0x00000008      // Port Enable/Disable Change
#define EHCI_PORT_SC_OCA        0x00000010      // Over-Current Active
#define EHCI_PORT_SC_OCAC       0x00000020      // Over-Current Active Change
#define EHCI_PORT_SC_PR         0x00000100      // Port Reset
#define EHCI_PORT_SC_PP         0x00001000      // Port Power
#define EHCI_PORT_SC_PO         0x00002000      // Port Owner

#define EHCI_PORT_SC_LS_MASK    0x00000c00      // Line Status
#define EHCI_PORT_SC_LS_SE0     0x00000000      // Line Status is SE0
#define EHCI_PORT_SC_LS_K       0x00000400      // Line Status is K-state
#define EHCI_PORT_SC_LS_J       0x00000800      // Line Status is J-state
#define EHCI_PORT_SC_LS_U       0x00000c00      // Line Status is undefined

// Link Pointer 

#define EHCI_LP_TERMINATE       0x00000001      // Terminate (T) bit

#define EHCI_LP_TYPE_ITD        (0 << 1)        // Type is Isochronous Transfer Descriptor
#define EHCI_LP_TYPE_QH         (1 << 1)        // Type is Queue Head
#define EHCI_LP_TYPE_SITD       (2 << 1)        // Type is Split Transaction Isochronous Transfer Descriptor
#define EHCI_LP_TYPE_FSTN       (3 << 1)        // Type is Frame Span Traversal Node

// Queue Element Transfer Descriptor data structure

// - status member (8 bits)

#define EHCI_QTD_PS             0x01            // Ping State
#define EHCI_QTD_STS            0x02            // Split Transaction State
#define EHCI_QTD_MMF            0x04            // Missed Micro-Frame
#define EHCI_QTD_TR_ERR         0x08            // Transaction Error
#define EHCI_QTD_BABBLE         0x10            // Babble Detected
#define EHCI_QTD_DB_ERR         0x20            // Data Buffer Error
#define EHCI_QTD_HALTED         0x40            // Halted
#define EHCI_QTD_ACTIVE         0x80            // Active

// - control member (8 bits)

#define EHCI_QTD_PID_OUT        (0 << 0)        // PID Code is OUT
#define EHCI_QTD_PID_IN         (1 << 0)        // PID Code is IN
#define EHCI_QTD_PID_SETUP      (2 << 0)        // PID Code is SETUP

#define EHCI_QTD_CERR(n)        ((n) << 2)      // Error Counter = n (n = 1..3)

#define EHCI_QTD_CPAGE(n)       ((n) << 4)      // Current Page = n (n = 0..7)

#define EHCI_QTD_IOC_N          (0 << 7)        // Interrupt On Completion is off
#define EHCI_QTD_IOC_Y          (1 << 7)        // Interrupt On Completion is on

// - data_length member (16 bits)

#define EHCI_QTD_DT_MASK        0x8000

#define EHCI_QTD_DT(n)          ((n) << 15)     // Data Toggle = n (n = 0,1)

// Queue head data structure

#define EHCI_QH_HRL             0x00008000      // Head of Reclamation List flag
#define EHCI_QH_CTRL_EP         0x08000000      // Control Endpoint flag

#define EHCI_QH_DTC(n)          ((n) << 14)     // Data Toggle Control (n = 0,1)

#define EHCI_QH_HBPM(n)         ((n) << 30)     // High Bandwidth Pipe Multiplier = n (n = 1..3)

// Port Speed values

#define EHCI_FULL_SPEED         0
#define EHCI_LOW_SPEED          1
#define EHCI_HIGH_SPEED         2

// Values specific to this driver.

#define MAX_KEYBOARDS           8                   // per host controller

#define WS_QHD_SIZE             (1 + MAX_KEYBOARDS) // Queue Head Descriptors
#define WS_QTD_SIZE             (3 + MAX_KEYBOARDS) // Queue Transfer Descriptors

#define MILLISEC                1000                // in microseconds

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

// Register sets defined by the EHCI specification.

typedef struct {
    uint8_t             cap_length;
    uint8_t             reserved;
    uint16_t            hci_version;
    uint32_t            hcs_params;
    uint32_t            hcc_params;
    uint64_t            port_route;
} ehci_cap_regs_t;

typedef volatile struct {
    uint32_t            usb_command;
    uint32_t            usb_status;
    uint32_t            usb_interrupt;
    uint32_t            fr_index;
    uint32_t            ctrl_ds_segment;
    uint32_t            periodic_list_base;
    uint32_t            async_list_addr;
    uint32_t            reserved[9];
    uint32_t            config_flag;
    uint32_t            port_sc[];
} ehci_op_regs_t;

// Data structures defined by the EHCI specification.

// NOTE: The ehci_qtd_t structure supports both the 32-bit and 64-bit variants. But we always allocate data
// buffers in the first 4GB, so we can simply initialise the buffer pointer extensions to zero.
typedef volatile struct {
    uint32_t            next_qtd_ptr;
    uint32_t            alt_next_qtd_ptr;
    uint8_t             status;
    uint8_t             control;
    uint16_t            data_length;
    uint32_t            buffer_ptr[5];
    uint32_t            ext_buffer_ptr[5];
    uint32_t            padding[3];
} ehci_qtd_t  __attribute__ ((aligned (32)));

typedef volatile struct {
    uint32_t            next_qhd_ptr;
    uint32_t            epcc[2];
    uint32_t            current_qtd_ptr;
    uint32_t            next_qtd_ptr;
    uint32_t            alt_next_qtd_ptr;
    uint8_t             status;
    uint8_t             control;
    uint16_t            data_length;
    uint32_t            buffer_ptr[5];
    uint32_t            ext_buffer_ptr[5];
    uint32_t            padding[7];
} ehci_qhd_t  __attribute__ ((aligned (32)));

// Data structures specific to this implementation.

typedef struct {
    hcd_workspace_t     base_ws;

    // System memory data structures used by the host controller.
    ehci_qhd_t          qhd[WS_QHD_SIZE]  __attribute__ ((aligned (32)));
    ehci_qtd_t          qtd[WS_QTD_SIZE]  __attribute__ ((aligned (32)));

    // Keyboard data transfer buffers.
    hid_kbd_rpt_t       kbd_rpt[MAX_KEYBOARDS];

    // Saved keyboard reports.
    hid_kbd_rpt_t       prev_kbd_rpt[MAX_KEYBOARDS];

    // Pointer to the host controller registers.
    ehci_op_regs_t      *op_regs;

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

static int usb_to_ehci_speed(usb_speed_t usb_speed)
{
    switch (usb_speed) {
      case USB_SPEED_LOW:
        return EHCI_LOW_SPEED;
      case USB_SPEED_FULL:
        return EHCI_FULL_SPEED;
      case USB_SPEED_HIGH:
        return EHCI_HIGH_SPEED;
      default:
        return 0;
    }
}

static int num_ehci_ports(uint32_t hcs_params)
{
    return (hcs_params >> 0) & 0xf;
}

static int num_ehci_companions(uint32_t hcs_params)
{
    return (hcs_params >> 12) & 0xf;
}

static int ehci_ext_cap_ptr(uint32_t hcc_params)
{
    return (hcc_params >> 8) & 0xff;
}

static bool reset_host_controller(ehci_op_regs_t *op_regs)
{
    write32(&op_regs->usb_command, read32(&op_regs->usb_command) | EHCI_USBCMD_HCR);

    usleep(1*MILLISEC);  // some controllers need time to recover from reset

    return wait_until_clr(&op_regs->usb_command, EHCI_USBCMD_HCR, 1000*MILLISEC);
}

static bool start_host_controller(ehci_op_regs_t *op_regs)
{
    write32(&op_regs->usb_command, EHCI_USBCMD_R_S | EHCI_USBCMD_FLS_1024 | EHCI_USBCMD_ITC(8));
    return wait_until_clr(&op_regs->usb_status, EHCI_USBSTS_HCH, 1000*MILLISEC);
}

static bool halt_host_controller(ehci_op_regs_t *op_regs)
{
    write32(&op_regs->usb_command, read32(&op_regs->usb_command) & ~EHCI_USBCMD_R_S);
    return wait_until_set(&op_regs->usb_status, EHCI_USBSTS_HCH, 1000*MILLISEC);
}

static void enable_periodic_schedule(ehci_op_regs_t *op_regs)
{
    write32(&op_regs->usb_command, read32(&op_regs->usb_command) | EHCI_USBCMD_PSE);
}

static void enable_async_schedule(ehci_op_regs_t *op_regs)
{
    write32(&op_regs->usb_command, read32(&op_regs->usb_command) | EHCI_USBCMD_ASE);
}

static bool disable_async_schedule(ehci_op_regs_t *op_regs)
{
    write32(&op_regs->usb_command, read32(&op_regs->usb_command) & ~EHCI_USBCMD_ASE);
    return wait_until_clr(&op_regs->usb_status, EHCI_USBSTS_ASS, 1000*MILLISEC);
}

static bool reset_ehci_port(ehci_op_regs_t *op_regs, int port_idx)
{
    uint32_t port_status = read32(&op_regs->port_sc[port_idx]) & ~EHCI_PORT_SC_PED;
    flush32(&op_regs->port_sc[port_idx], port_status |  EHCI_PORT_SC_PR);

    usleep(50*MILLISEC);  // USB port reset time

    write32(&op_regs->port_sc[port_idx], port_status & ~EHCI_PORT_SC_PR);
    return wait_until_clr(&op_regs->port_sc[port_idx], EHCI_PORT_SC_PR, 5*MILLISEC);
}

static void disable_ehci_port(ehci_op_regs_t *op_regs, int port_idx)
{
    uint32_t port_status = read32(&op_regs->port_sc[port_idx]);
    write32(&op_regs->port_sc[port_idx], port_status & ~EHCI_PORT_SC_PED);
    (void)wait_until_clr(&op_regs->port_sc[port_idx], EHCI_PORT_SC_PED, 1000*MILLISEC);
}

static void release_ehci_port(ehci_op_regs_t *op_regs, int port_idx)
{
    uint32_t port_status = read32(&op_regs->port_sc[port_idx]);
    write32(&op_regs->port_sc[port_idx], port_status | EHCI_PORT_SC_PO);
}

static void build_ehci_qtd(ehci_qtd_t *this_qtd, const ehci_qtd_t *final_qtd, uint8_t control, uint16_t dt,
                           const void *buffer, size_t length)
{
    memset((void *)this_qtd, 0, sizeof(ehci_qtd_t));

    if (this_qtd != final_qtd) {
        this_qtd->next_qtd_ptr     = (uintptr_t)(this_qtd + 1);
        this_qtd->alt_next_qtd_ptr = (uintptr_t)(final_qtd);
    } else {
        this_qtd->next_qtd_ptr     = EHCI_LP_TERMINATE;
        this_qtd->alt_next_qtd_ptr = EHCI_LP_TERMINATE;
    }
    this_qtd->status        = EHCI_QTD_ACTIVE;
    this_qtd->control       = EHCI_QTD_CPAGE(0) | EHCI_QTD_CERR(3) | control;
    this_qtd->data_length   = dt | (length & 0x7fff);
    this_qtd->buffer_ptr[0] = (uintptr_t)buffer;
}

static void build_ehci_qhd(ehci_qhd_t *qhd, const ehci_qtd_t *qtd, const usb_ep_t *ep, bool is_interrupt)
{
    memset((void *)qhd, 0, sizeof(ehci_qhd_t));

    uint32_t endpoint_speed = usb_to_ehci_speed(ep->device_speed);

    uint32_t parent_addr = (ep->driver_data >> 0) & 0x7f;
    uint32_t parent_port = (ep->driver_data >> 8) & 0x7f;

    qhd->next_qhd_ptr = (uintptr_t)qhd | EHCI_LP_TYPE_QH;

    qhd->epcc[0] = ep->max_packet_size << 16
                 | EHCI_QH_DTC(1)
                 | endpoint_speed      << 12
                 | ep->endpoint_num    << 8
                 | ep->device_id       << 0;

    qhd->epcc[1] = EHCI_QH_HBPM(1)
                 | parent_port << 23
                 | parent_addr << 16;

    if (ep->device_speed < USB_SPEED_HIGH && ep->endpoint_num == 0) {
        qhd->epcc[0] |= EHCI_QH_CTRL_EP;
    }
    if (is_interrupt) {
        qhd->epcc[1] |= 0x01;   // s_mask
        if (ep->device_speed < USB_SPEED_HIGH) {
            qhd->epcc[1] |= 0x1c << 8;   // c_mask
        }
    } else {
        qhd->epcc[0] |= EHCI_QH_HRL;
    }

    qhd->next_qtd_ptr = (uintptr_t)qtd;
}

static bool do_async_transfer(const workspace_t *ws, int num_tds)
{
    // Rely on the controller to timeout if the device doesn't respond.

    bool ok = true;
    enable_async_schedule(ws->op_regs);
    for (int td_idx = 0; td_idx < num_tds; td_idx++) {
        const ehci_qtd_t *qtd = &ws->qtd[td_idx];
        while (qtd->status & EHCI_QTD_ACTIVE) {
            usleep(10);
        }
        if (qtd->status & (EHCI_QTD_HALTED | EHCI_QTD_DB_ERR | EHCI_QTD_BABBLE | EHCI_QTD_TR_ERR | EHCI_QTD_MMF | EHCI_QTD_PS)) {
            ok = false;
            break;
        }
    }
    disable_async_schedule(ws->op_regs);
    return ok;
}

//------------------------------------------------------------------------------
// Driver Methods
//------------------------------------------------------------------------------

static bool reset_root_hub_port(const usb_hcd_t *hcd, int port_num)
{
    const workspace_t *ws = (const workspace_t *)hcd->ws;

    return reset_ehci_port(ws->op_regs, port_num - 1);
}

static bool assign_address(const usb_hcd_t *hcd, const usb_hub_t *hub, int port_num,
                    usb_speed_t device_speed, int device_id, usb_ep_t *ep0)
{
    // Store the extra information needed by build_ehci_qhd().
    ep0->driver_data = port_num << 8;
    if (hub->level > 0) {
        ep0->driver_data |= hub->ep0->device_id;
    }
    if (!assign_usb_address(hcd, hub, port_num, device_speed, device_id, ep0)) {
        return false;
    }

    return true;
}

static bool setup_request(const usb_hcd_t *hcd, const usb_ep_t *ep, const usb_setup_pkt_t *setup_pkt)
{
    workspace_t *ws = (workspace_t *)hcd->ws;

    build_ehci_qtd(&ws->qtd[0], &ws->qtd[1], EHCI_QTD_PID_SETUP, EHCI_QTD_DT(0), setup_pkt, sizeof(usb_setup_pkt_t));
    build_ehci_qtd(&ws->qtd[1], &ws->qtd[1], EHCI_QTD_PID_IN,    EHCI_QTD_DT(1), 0, 0);
    build_ehci_qhd(&ws->qhd[0], &ws->qtd[0], ep, false);
    return do_async_transfer(ws, 2);
}

static bool get_data_request(const usb_hcd_t *hcd, const usb_ep_t *ep, const usb_setup_pkt_t *setup_pkt,
                             const void *buffer, size_t length)
{
    workspace_t *ws = (workspace_t *)hcd->ws;

    build_ehci_qtd(&ws->qtd[0], &ws->qtd[2], EHCI_QTD_PID_SETUP, EHCI_QTD_DT(0), setup_pkt, sizeof(usb_setup_pkt_t));
    build_ehci_qtd(&ws->qtd[1], &ws->qtd[2], EHCI_QTD_PID_IN,    EHCI_QTD_DT(1), buffer, length);
    build_ehci_qtd(&ws->qtd[2], &ws->qtd[2], EHCI_QTD_PID_OUT,   EHCI_QTD_DT(1), 0, 0);
    build_ehci_qhd(&ws->qhd[0], &ws->qtd[0], ep, false);
    return do_async_transfer(ws, 3);
}

static void poll_keyboards(const usb_hcd_t *hcd)
{
    workspace_t *ws = (workspace_t *)hcd->ws;

    for (int kbd_idx = 0; kbd_idx < ws->num_keyboards; kbd_idx++) {
        ehci_qtd_t *kbd_qtd = &ws->qtd[3 + kbd_idx];

        uint8_t status = kbd_qtd->status;
        if (status & EHCI_QTD_ACTIVE) continue;

        hid_kbd_rpt_t *kbd_rpt = &ws->kbd_rpt[kbd_idx];

        uint8_t error_mask = EHCI_QTD_HALTED | EHCI_QTD_DB_ERR | EHCI_QTD_BABBLE | EHCI_QTD_TR_ERR | EHCI_QTD_MMF | EHCI_QTD_PS;
        if (~status & error_mask) {
            hid_kbd_rpt_t *prev_kbd_rpt = &ws->prev_kbd_rpt[kbd_idx];
            if (process_usb_keyboard_report(hcd, kbd_rpt, prev_kbd_rpt)) {
                *prev_kbd_rpt = *kbd_rpt;
            }
        }

        ehci_qhd_t *kbd_qhd = &ws->qhd[1 + kbd_idx];

        uint16_t dt = kbd_qhd->data_length & EHCI_QTD_DT_MASK;
        build_ehci_qtd(kbd_qtd, kbd_qtd, EHCI_QTD_PID_IN, dt, kbd_rpt, sizeof(hid_kbd_rpt_t));

        kbd_qhd->next_qtd_ptr = (uintptr_t)kbd_qtd;
    }
}

//------------------------------------------------------------------------------
// Driver Method Table
//------------------------------------------------------------------------------

static const hcd_methods_t methods = {
    .reset_root_hub_port = reset_root_hub_port,
    .allocate_slot       = NULL,
    .release_slot        = NULL,
    .assign_address      = assign_address,
    .configure_hub_ep    = NULL,
    .configure_kbd_ep    = NULL,
    .setup_request       = setup_request,
    .get_data_request    = get_data_request,
    .poll_keyboards      = poll_keyboards
};

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

bool ehci_init(int bus, int dev, int func, uintptr_t base_addr, usb_hcd_t *hcd)
{
    ehci_cap_regs_t *cap_regs = (ehci_cap_regs_t *)base_addr;

    // Walk the extra capabilities list.
    int ext_cap_ptr = ehci_ext_cap_ptr(read32(&cap_regs->hcc_params));
    while (ext_cap_ptr != 0) {
        uint8_t ext_cap_id = pci_config_read8(bus, dev, func, ext_cap_ptr + 0);
        if (ext_cap_id == EHCI_EXT_CAP_OS_HANDOFF) {
            // Take ownership from the SMM if necessary.
            int timer = 1000;
            pci_config_write8(bus, dev, func, ext_cap_ptr + 3, 1);
            while (pci_config_read8(bus, dev, func, ext_cap_ptr + 2) & 1) {
                if (timer == 0) return false;
                usleep(1*MILLISEC);
                timer--;
            }
        }
        ext_cap_ptr = pci_config_read8(bus, dev, func, ext_cap_ptr + 1);
    }

    ehci_op_regs_t *op_regs = (ehci_op_regs_t *)(base_addr + cap_regs->cap_length);

    // Ensure the controller is halted and then reset it.
    if (!halt_host_controller(op_regs)) return false;
    if (!reset_host_controller(op_regs)) return false;

    // Allocate and initialise a periodic frame list. This needs to be aligned on a 4K page boundary. Some controllers
    // don't support a programmable list length, so we just use the default length.
    pm_map[0].end -= num_pages(EHCI_MAX_PFL_LENGTH * sizeof(uint32_t));
    uintptr_t pfl_addr = pm_map[0].end << PAGE_SHIFT;
    uint32_t *pfl = (uint32_t *)pfl_addr;
    for (int i = 0; i < EHCI_MAX_PFL_LENGTH; i++) {
        pfl[i] = EHCI_LP_TERMINATE;
    }

    // Allocate and initialise a workspace for this controller. This needs to be permanently mapped into virtual memory,
    // so allocate it in the first segment.
    // TODO: check for segment overflow.
    pm_map[0].end -= num_pages(sizeof(workspace_t));
    uintptr_t workspace_addr = pm_map[0].end << PAGE_SHIFT;
    workspace_t *ws = (workspace_t *)workspace_addr;

    memset(ws, 0, sizeof(workspace_t));

    ws->op_regs = op_regs;

    // Initialise the driver object for this controller.
    hcd->methods = &methods;
    hcd->ws      = &ws->base_ws;

    // Initialise the host controller.
    write32(&op_regs->fr_index, 0);
    write32(&op_regs->ctrl_ds_segment, 0);
    write32(&op_regs->periodic_list_base, pfl_addr);
    write32(&op_regs->async_list_addr, (uintptr_t)(ws->qhd));
    if (!start_host_controller(op_regs)) {
        pm_map[0].end += num_pages(sizeof(workspace_t));
        pm_map[0].end += num_pages(EHCI_MAX_PFL_LENGTH * sizeof(uint32_t));
        return false;
    }
    flush32(&op_regs->config_flag, 1);

    uint32_t hcs_params = read32(&cap_regs->hcs_params);

    // Construct a hub descriptor for the root hub.
    usb_hub_t root_hub;
    root_hub.ep0            = NULL;
    root_hub.level          = 0;
    root_hub.route          = 0;
    root_hub.num_ports      = num_ehci_ports(hcs_params);
    root_hub.power_up_delay = 10;  // 20ms

    // Power up all the ports.
    if (hcs_params & EHCI_HCS_PPC) {
        bool port_power_changed = false;
        for (int port_idx = 0; port_idx < root_hub.num_ports; port_idx++) {
            uint32_t port_status = read32(&op_regs->port_sc[port_idx]);
            if (~port_status & EHCI_PORT_SC_PP) {
                flush32(&op_regs->port_sc[port_idx], port_status | EHCI_PORT_SC_PP);
                port_power_changed = true;
            }
        }
        if (port_power_changed) {
            usleep(20*MILLISEC);  // EHCI maximum port power-up time
        }
    }

    usleep(100*MILLISEC);  // USB maximum device attach time

    bool i_have_companions = (num_ehci_companions(hcs_params) > 0);

    // Scan the ports, looking for hubs and keyboards.
    usb_ep_t keyboards[MAX_KEYBOARDS];
    int num_keyboards = 0;
    int num_ls_devices = 0;
    int num_hs_devices = 0;
    for (int port_idx = 0; port_idx < root_hub.num_ports; port_idx++) {
        // If we've filled the keyboard info table, abort now.
        if (num_keyboards >= MAX_KEYBOARDS) break;

        uint32_t port_status = read32(&op_regs->port_sc[port_idx]);

        // Check the port is powered up.
        if (~port_status & EHCI_PORT_SC_PP) continue;

        // Check if anything is connected to this port.
        if (~port_status & EHCI_PORT_SC_CCS) continue;

        // Check for low speed device.
        if ((port_status & EHCI_PORT_SC_LS_MASK) == EHCI_PORT_SC_LS_K) {
            if (i_have_companions) {
                release_ehci_port(op_regs, port_idx);
            }
            num_ls_devices++;
            continue;
        }

        // Reset the port.
        if (!reset_ehci_port(op_regs, port_idx)) continue;

        usleep(10*MILLISEC);  // USB reset recovery time

        port_status = read32(&op_regs->port_sc[port_idx]);

        // Check for full speed device.
        if (~port_status & EHCI_PORT_SC_PED) {
            if (i_have_companions) {
                release_ehci_port(op_regs, port_idx);
            }
            num_ls_devices++;
            continue;
        }

        num_hs_devices++;

        // Look for keyboards attached directly or indirectly to this port.
        if (find_attached_usb_keyboards(hcd, &root_hub, 1 + port_idx, USB_SPEED_HIGH, num_hs_devices,
                                        &num_hs_devices, keyboards, MAX_KEYBOARDS, &num_keyboards)) {
            continue;
        }

        // If we didn't find any keyboard interfaces, we can disable the port.
        disable_ehci_port(op_regs, port_idx);
    }

    print_usb_info(" Found %i low/full speed device%s, %i high speed device%s, %i keyboard%s",
                   num_ls_devices, num_ls_devices != 1 ? "s" : "",
                   num_hs_devices, num_hs_devices != 1 ? "s" : "",
                   num_keyboards,  num_keyboards  != 1 ? "s" : "");
    if (num_ls_devices > 0 && i_have_companions) {
        print_usb_info(" Handed over low/full speed devices to companion controllers");
    }

    if (num_keyboards == 0) {
        // Halt the host controller.
        (void)halt_host_controller(op_regs);

        // Deallocate the workspace for this controller.
        pm_map[0].end += num_pages(sizeof(workspace_t));

        // Deallocate the periodic frame list.
        pm_map[0].end += num_pages(EHCI_MAX_PFL_LENGTH * sizeof(uint32_t));

        return false;
    }

    ws->num_keyboards = num_keyboards;

    // Initialise the interrupt QHD and QTD for each keyboard interface and find the minimum interval.
    int min_interval = EHCI_MAX_PFL_LENGTH;
    uint32_t first_qhd_ptr = EHCI_LP_TERMINATE;
    for (int kbd_idx = 0; kbd_idx < num_keyboards; kbd_idx++) {
        usb_ep_t *kbd = &keyboards[kbd_idx];

        ehci_qhd_t *kbd_qhd = &ws->qhd[1 + kbd_idx];
        ehci_qtd_t *kbd_qtd = &ws->qtd[3 + kbd_idx];

        hid_kbd_rpt_t *kbd_rpt = &ws->kbd_rpt[kbd_idx];

        build_ehci_qtd(kbd_qtd, kbd_qtd, EHCI_QTD_PID_IN, EHCI_QTD_DT(0), kbd_rpt, sizeof(hid_kbd_rpt_t));
        build_ehci_qhd(kbd_qhd, kbd_qtd, kbd, true);

        kbd_qhd->next_qhd_ptr = first_qhd_ptr;
        first_qhd_ptr = (uintptr_t)kbd_qhd | EHCI_LP_TYPE_QH;

        if (kbd->interval < min_interval) {
            min_interval = kbd->interval;
        }
    }

    // Initialise the periodic frame list and enable the periodic schedule.
    for (int i = 0; i < EHCI_MAX_PFL_LENGTH; i += min_interval) {
        pfl[i] = first_qhd_ptr;
    }
    enable_periodic_schedule(op_regs);

    return true;
}
