// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2021-2022 Martin Whitaker.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memrw32.h"
#include "memsize.h"
#include "pmem.h"
#include "usb.h"

#include "string.h"
#include "unistd.h"

#include "ohci.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// Values defined by the OHCI specification.

// HcControl register

#define OHCI_CTRL_CBSR          0x00000003      // Control Bulk Service Ratio
#define OHCI_CTRL_CBSR0         0x00000000      // Control Bulk Service Ratio 0
#define OHCI_CTRL_CBSR1         0x00000001      // Control Bulk Service Ratio 1
#define OHCI_CTRL_CBSR2         0x00000002      // Control Bulk Service Ratio 2
#define OHCI_CTRL_CBSR3         0x00000003      // Control Bulk Service Ratio 2
#define OHCI_CTRL_PLE           0x00000004      // Periodic List Enable
#define OHCI_CTRL_IE            0x00000008      // Isochronous Enable
#define OHCI_CTRL_CLE           0x00000010      // Control List Enable
#define OHCI_CTRL_BLE           0x00000020      // Bulk List Enable
#define OHCI_CTRL_HCFS          0x000000c0      // Host Controller Functional State
#define OHCI_CTRL_HCFS_RST      0x00000000      // Host Controller Functional State is Reset
#define OHCI_CTRL_HCFS_RES      0x00000040      // Host Controller Functional State is Resume
#define OHCI_CTRL_HCFS_RUN      0x00000080      // Host Controller Functional State is Run
#define OHCI_CTRL_HCFS_SUS      0x000000c0      // Host Controller Functional State is Suspend
#define OHCI_CTRL_IR            0x00000100      // Interrupt Routing
#define OHCI_CTRL_RWC           0x00000200      // Remote Wakeup Connected
#define OHCI_CTRL_RWE           0x00000400      // Remote Wakeup Enable

// HcCommandStatus register

#define OHCI_CMD_HCR            0x00000001      // Host Controller Reset
#define OHCI_CMD_CLF            0x00000002      // Control List Filled
#define OHCI_CMD_BLF            0x00000004      // Bulk List Filled
#define OHCI_CMD_OCR            0x00000008      // Ownership Change Request

// HcInterruptStatus register

#define OHCI_INTR_SC            0x00000001      // Scheduling Overrun
#define OHCI_INTR_WDH           0x00000002      // Writeback Done Head
#define OHCI_INTR_SOF           0x00000004      // Start of Frame
#define OHCI_INTR_RD            0x00000008      // Resume Detected
#define OHCI_INTR_UE            0x00000010      // Unrecoverable Error
#define OHCI_INTR_FNO           0x00000020      // Frame Number Overflow
#define OHCI_INTR_RHSC          0x00000040      // Root Hub Status Change
#define OHCI_INTR_OC            0x40000000      // Ownership Change
#define OHCI_INTR_MIE           0x80000000      // Master Interrupt Enable

// HcFmIntervalRegister

#define OHCI_FIT                0x80000000      // Frame Interval Toggle

// HcRhDescriptorA register

#define OHCI_RHDA_PSM           0x00000100      // Power Switching Mode
#define OHCI_RHDA_NPS           0x00000200      // No Power Switching
#define OHCI_RHDA_OCPM          0x00000800      // Over Current Protection Mode
#define OHCI_RHDA_NOCP          0x00001000      // No Over Current Protection

// HcRhDescriptorB register

#define OHCI_RHDB_DR            0x0000ffff      // Device Removable
#define OHCI_RHDB_PPCM          0xffff0000      // Port Power Control Mask

// HcRhStatus register

#define OHCI_RHS_LPS            0x00000001      // Local Power Status
#define OHCI_RHS_OCI            0x00000002      // Over-Current Indicator
#define OHCI_RHS_DRWE           0x00008000      // Device Remote Wakeup Enable
#define OHCI_RHS_LPSC           0x00010000      // Local Power Status Change
#define OHCI_RHS_OCIC           0x00020000      // Over-Current Indicator Change
#define OHCI_RHS_CRWE           0x80000000      // Clear Remote Wakeup Enable

#define OHCI_SET_GLOBAL_POWER   0x00010000
#define OHCI_CLR_GLOBAL_POWER   0x00000001

// HcRhPortStatus registers

#define OHCI_PORT_CONNECTED     0x00000001
#define OHCI_PORT_ENABLED       0x00000002
#define OHCI_PORT_SUSPENDED     0x00000004
#define OHCI_PORT_OCI           0x00000008
#define OHCI_PORT_RESETING      0x00000010
#define OHCI_PORT_POWERED       0x00000100
#define OHCI_PORT_LOW_SPEED     0x00000200
#define OHCI_PORT_CONNECT_CHG   0x00010000
#define OHCI_PORT_ENABLE_CHG    0x00020000
#define OHCI_PORT_SUSPEND_CHG   0x00040000
#define OHCI_PORT_OCI_CHG       0x00080000
#define OHCI_PORT_RESET_CHG     0x00100000

#define OHCI_CLR_PORT_ENABLE    0x00000001
#define OHCI_SET_PORT_ENABLE    0x00000002
#define OHCI_SET_PORT_SUSPEND   0x00000004
#define OHCI_CLR_PORT_SUSPEND   0x00000008
#define OHCI_SET_PORT_RESET     0x00000010
#define OHCI_SET_PORT_POWER     0x00000100
#define OHCI_CLR_PORT_POWER     0x00000200

// Endpoint Descriptor data structure

#define OHCI_ED_FA              0x0000007f      // Function Address
#define OHCI_ED_EN              0x00000780      // Endpoint Number
#define OHCI_ED_DIR             0x00001800      // Direction
#define OHCI_ED_DIR_TD          0x00000000      // Direction is From TD
#define OHCI_ED_DIR_OUT         0x00000800      // Direction is OUT
#define OHCI_ED_DIR_IN          0x00001000      // Direction is IN
#define OHCI_ED_SPD             0x00002000      // Speed
#define OHCI_ED_SPD_FULL        0x00000000      // Speed is Full Speed
#define OHCI_ED_SPD_LOW         0x00002000      // Speed is Low Speed
#define OHCI_ED_SKIP            0x00004000      // Skip
#define OHCI_ED_FMT             0x00008000      // Format
#define OHCI_ED_FMT_GEN         0x00000000      // Format is General TD
#define OHCI_ED_FMT_ISO         0x00008000      // Format is Isochronous TD
#define OHCI_ED_MPS             0x07ff0000      // Max Packet Size

#define OHCI_ED_HALTED          0x00000001      // Halted flag
#define OHCI_ED_TOGGLE          0x00000002      // Toggle carry bit

// Transfer Descriptor data structure

#define OHCI_TD_BR              0x00040000      // Buffer Rounding
#define OHCI_TD_DP              0x00180000      // Direction/PID
#define OHCI_TD_DP_SETUP        0x00000000      // Direction/PID is SETUP
#define OHCI_TD_DP_OUT          0x00080000      // Direction/PID is OUT
#define OHCI_TD_DP_IN           0x00100000      // Direction/PID is IN
#define OHCI_TD_DI              0x00e00000      // Delay Interrupt
#define OHCI_TD_DI_NO_DLY       0x00000000      // Delay Interrupt is 0 (no delay)
#define OHCI_TD_DI_NO_INT       0x00e00000      // Delay Interrupt is 7 (no interrupt)
#define OHCI_TD_DT              0x03000000      // Data Toggle
#define OHCI_TD_DT_0            0x00000000      // Data Toggle LSB is 0
#define OHCI_TD_DT_1            0x01000000      // Data Toggle LSB is 1
#define OHCI_TD_DT_USE_ED       0x00000000      // Data Toggle MSB is 0
#define OHCI_TD_DT_USE_TD       0x02000000      // Data Toggle MSB is 1
#define OHCI_TD_EC              0x0c000000      // Error Count
#define OHCI_TD_CC              0xf0000000      // Condition Code
#define OHCI_TD_CC_NO_ERR       0x00000000      // Condition Code is No Error
#define OHCI_TD_CC_NEW          0xe0000000      // Condition Code is Not Accessed

// Miscellaneous values

#define OHCI_MAX_INTERVAL       32

// Values specific to this driver.

#define MAX_KEYBOARDS           8                   // per host controller

#define WS_ED_SIZE              (1 + MAX_KEYBOARDS) // Endpoint Descriptors
#define WS_TD_SIZE              (3 + MAX_KEYBOARDS) // Transfer Descriptors

#define MILLISEC                1000                // in microseconds

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

// Register sets defined by the OHCI specification.

typedef volatile struct {
    uint32_t            revision;
    uint32_t            control;
    uint32_t            command_status;
    uint32_t            interrupt_status;
    uint32_t            interrupt_enable;
    uint32_t            interrupt_disable;
    uint32_t            hcca;
    uint32_t            period_current_ed;
    uint32_t            ctrl_head_ed;
    uint32_t            ctrl_current_ed;
    uint32_t            bulk_head_ed;
    uint32_t            bulk_current_ed;
    uint32_t            done_head;
    uint32_t            fm_interval;
    uint32_t            fm_remaining;
    uint32_t            fm_number;
    uint32_t            periodic_start;
    uint32_t            ls_threshold;
    uint32_t            rh_descriptor_a;
    uint32_t            rh_descriptor_b;
    uint32_t            rh_status;
    uint32_t            rh_port_status[];
} ohci_op_regs_t;

// Data structures defined by the OHCI specification.

typedef volatile struct {
    uint32_t            intr_head_ed[32];
    uint16_t            frame_num;
    uint16_t            pad;
    uint32_t            done_head;
    uint32_t            reserved[30];
} ohci_hcca_t  __attribute__ ((aligned (256)));

typedef volatile struct {
    uint32_t            control;
    uint32_t            tail_ptr;
    uint32_t            head_ptr;
    uint32_t            next_ed;
} ohci_ed_t  __attribute__ ((aligned (16)));

typedef volatile struct {
    uint32_t            control;
    uint32_t            curr_buff;
    uint32_t            next_td;
    uint32_t            buff_end;
} ohci_td_t  __attribute__ ((aligned (16)));

// Data structures specific to this implementation.

typedef struct {
    hcd_workspace_t     base_ws;

    // System memory data structures used by the host controller.
    ohci_hcca_t         hcca            __attribute__ ((aligned (256)));
    ohci_ed_t           ed[WS_ED_SIZE]  __attribute__ ((aligned (16)));
    ohci_td_t           td[WS_TD_SIZE]  __attribute__ ((aligned (16)));

    // Keyboard data transfer buffers.
    hid_kbd_rpt_t       kbd_rpt[MAX_KEYBOARDS];

    // Saved keyboard reports.
    hid_kbd_rpt_t       prev_kbd_rpt[MAX_KEYBOARDS];

    // Pointer to the host controller registers.
    ohci_op_regs_t      *op_regs;
} workspace_t  __attribute__ ((aligned (256)));

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static size_t num_pages(size_t size)
{
    return (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

static bool reset_ohci_port(ohci_op_regs_t *op_regs, int port_idx)
{
    // The OHCI reset lasts for 10ms, but the USB specification calls for 50ms (but not necessarily continuously).
    // So do it 5 times.
    for (int i = 0; i < 5; i++) {
        write32(&op_regs->rh_port_status[port_idx], OHCI_PORT_CONNECT_CHG | OHCI_PORT_RESET_CHG);
        write32(&op_regs->rh_port_status[port_idx], OHCI_SET_PORT_RESET);
        if (!wait_until_set(&op_regs->rh_port_status[port_idx], OHCI_PORT_RESET_CHG, 1000*MILLISEC)) {
            return false;
        }
    }
    write32(&op_regs->rh_port_status[port_idx], OHCI_PORT_RESET_CHG);

    return true;
}

static ohci_td_t *get_ohci_done_head(const workspace_t *ws)
{
    ohci_op_regs_t *op_regs = ws->op_regs;

    if (~read32(&op_regs->interrupt_status) & OHCI_INTR_WDH) {
        return NULL;
    }
    uintptr_t done_head = ws->hcca.done_head & 0xfffffffe;
    write32(&op_regs->interrupt_status, OHCI_INTR_WDH);
    return (ohci_td_t *)done_head;
}

static bool wait_for_ohci_done(const workspace_t *ws, int td_expected)
{
    int td_completed = 0;

    // Rely on the controller to timeout if the device doesn't respond.
    while (true) {
        ohci_td_t *td = get_ohci_done_head(ws);
        while (td != NULL) {
            td_completed++;
            if ((td->control & OHCI_TD_CC) != OHCI_TD_CC_NO_ERR) {
                return false;
            }
            td = (ohci_td_t *)((uintptr_t)td->next_td);
        }
        if (td_completed == td_expected) break;
        usleep(10);
    }

    return true;
}

static void build_ohci_td(ohci_td_t *td, uint32_t control, const void *buffer, size_t length)
{
    td->control   = OHCI_TD_CC_NEW | control;
    td->curr_buff = (uintptr_t)buffer;
    td->buff_end  = (uintptr_t)buffer + length - 1;
    td->next_td   = (uintptr_t)(td + 1);
}

static void build_ohci_ed(ohci_ed_t *ed, uint32_t control, const ohci_td_t *head_td, const ohci_td_t *tail_td)
{
    // Set the skip flag before modifying the head and tail pointers, in case we are modifying an active ED.
    // Use write32() to make sure the compiler doesn't reorder the writes.
    write32(&ed->control, OHCI_ED_SKIP);
    ed->head_ptr = (uintptr_t)head_td;
    ed->tail_ptr = (uintptr_t)tail_td;
    write32(&ed->control, control);
}

static uint32_t ohci_ed_control(const usb_ep_t *ep)
{
    uint32_t ed_speed = (ep->device_speed == USB_SPEED_LOW) ? OHCI_ED_SPD_LOW : OHCI_ED_SPD_FULL;

    uint32_t control = OHCI_ED_FMT_GEN
                     | OHCI_ED_DIR_TD
                     | ed_speed
                     | ep->max_packet_size << 16
                     | ep->endpoint_num    << 7
                     | ep->device_id;

    return control;
}

//------------------------------------------------------------------------------
// Driver Methods
//------------------------------------------------------------------------------

static bool reset_root_hub_port(const usb_hcd_t *hcd, int port_num)
{
    const workspace_t *ws = (const workspace_t *)hcd->ws;

    return reset_ohci_port(ws->op_regs, port_num - 1);
}

static bool setup_request(const usb_hcd_t *hcd, const usb_ep_t *ep, const usb_setup_pkt_t *setup_pkt)
{
    workspace_t *ws = (workspace_t *)hcd->ws;

    build_ohci_td(&ws->td[0], OHCI_TD_DP_SETUP | OHCI_TD_DT_USE_TD | OHCI_TD_DT_0 | OHCI_TD_DI_NO_INT, setup_pkt, sizeof(usb_setup_pkt_t));
    build_ohci_td(&ws->td[1], OHCI_TD_DP_IN    | OHCI_TD_DT_USE_TD | OHCI_TD_DT_1 | OHCI_TD_DI_NO_DLY, 0, 0);
    build_ohci_ed(&ws->ed[0], ohci_ed_control(ep), &ws->td[0], &ws->td[2]);
    write32(&ws->op_regs->command_status, OHCI_CMD_CLF);
    return wait_for_ohci_done(ws, 2);
}

static bool get_data_request(const usb_hcd_t *hcd, const usb_ep_t *ep, const usb_setup_pkt_t *setup_pkt,
                             const void *buffer, size_t length)
{
    workspace_t *ws = (workspace_t *)hcd->ws;

    build_ohci_td(&ws->td[0], OHCI_TD_DP_SETUP | OHCI_TD_DT_USE_TD | OHCI_TD_DT_0 | OHCI_TD_DI_NO_INT, setup_pkt, sizeof(usb_setup_pkt_t));
    build_ohci_td(&ws->td[1], OHCI_TD_DP_IN    | OHCI_TD_DT_USE_TD | OHCI_TD_DT_1 | OHCI_TD_DI_NO_INT, buffer, length);
    build_ohci_td(&ws->td[2], OHCI_TD_DP_OUT   | OHCI_TD_DT_USE_TD | OHCI_TD_DT_1 | OHCI_TD_DI_NO_DLY, 0, 0);
    build_ohci_ed(&ws->ed[0], ohci_ed_control(ep), &ws->td[0], &ws->td[3]);
    write32(&ws->op_regs->command_status, OHCI_CMD_CLF);
    return wait_for_ohci_done(ws, 3);
}

static void poll_keyboards(const usb_hcd_t *hcd)
{
    workspace_t *ws = (workspace_t *)hcd->ws;

    ohci_td_t *td = get_ohci_done_head(ws);
    while (td != NULL) {
        int kbd_idx = td - ws->td - 3;

        hid_kbd_rpt_t *kbd_rpt = &ws->kbd_rpt[kbd_idx];

        if ((td->control & OHCI_TD_CC) == OHCI_TD_CC_NO_ERR) {
            hid_kbd_rpt_t *prev_kbd_rpt = &ws->prev_kbd_rpt[kbd_idx];
            if (process_usb_keyboard_report(hcd, kbd_rpt, prev_kbd_rpt)) {
                *prev_kbd_rpt = *kbd_rpt;
            }
        }

        ohci_td_t *next_td = (ohci_td_t *)((uintptr_t)td->next_td);

        ohci_ed_t *ed = &ws->ed[1 + kbd_idx];
        build_ohci_td(td, td->control & ~OHCI_TD_CC, kbd_rpt, sizeof(hid_kbd_rpt_t));
        build_ohci_ed(ed, ed->control, td+0, td+1);

        td = next_td;
    }
}

//------------------------------------------------------------------------------
// Driver Method Table
//------------------------------------------------------------------------------

static const hcd_methods_t methods = {
    .reset_root_hub_port = reset_root_hub_port,
    .allocate_slot       = NULL,
    .release_slot        = NULL,
    .assign_address      = assign_usb_address,  // use the base implementation for this method
    .configure_hub_ep    = NULL,
    .configure_kbd_ep    = NULL,
    .setup_request       = setup_request,
    .get_data_request    = get_data_request,
    .poll_keyboards      = poll_keyboards
};

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

bool ohci_init(uintptr_t base_addr, usb_hcd_t *hcd)
{
    ohci_op_regs_t *op_regs = (ohci_op_regs_t *)base_addr;

    // Check the host controller revison.
    if ((read32(&op_regs->revision) & 0xff) != 0x10) {
        return false;
    }

    // Take ownership from the SMM if necessary.
    if (read32(&op_regs->control) & OHCI_CTRL_IR) {
        write32(&op_regs->interrupt_enable, OHCI_INTR_OC);
        flush32(&op_regs->command_status, OHCI_CMD_OCR);
        if (!wait_until_clr(&op_regs->control, OHCI_CTRL_IR, 1000*MILLISEC)) {
            return false;
        }
    }

    // Preserve the frame interval set by the SMM or BIOS.
    // If not set, use the default value.
    uint32_t frame_interval = read32(&op_regs->fm_interval) & 0x3fff;
    if (frame_interval == 0) {
        frame_interval = 0x2edf;
    }

    // Prepare for host controller setup (see section 5.1.1.3 of the OHCI spec.).
    switch (read32(&op_regs->control) & OHCI_CTRL_HCFS) {
      case OHCI_CTRL_HCFS_RST:
        usleep(50*MILLISEC);
        break;
      case OHCI_CTRL_HCFS_SUS:
      case OHCI_CTRL_HCFS_RES:
        flush32(&op_regs->control, OHCI_CTRL_HCFS_RES);
        usleep(10*MILLISEC);
        break;
      default: // operational
        break;
    }

    // Reset the host controller.
    write32(&op_regs->command_status, OHCI_CMD_HCR);
    if (!wait_until_clr(&op_regs->command_status, OHCI_CMD_HCR, 30)) {
        return false;
    }

    // Check we are now in SUSPEND state.
    if ((read32(&op_regs->control) & OHCI_CTRL_HCFS) != OHCI_CTRL_HCFS_SUS) {
        return false;
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

    // Initialise the control list ED.
    ws->ed[0].control = OHCI_ED_SKIP;
    ws->ed[0].next_ed = 0;

    // Initialise the host controller.
    write32(&op_regs->hcca, (uintptr_t)(&ws->hcca));
    write32(&op_regs->ctrl_head_ed, (uintptr_t)(&ws->ed[0]));
    write32(&op_regs->bulk_head_ed, 0);
    write32(&op_regs->ctrl_current_ed, 0);
    write32(&op_regs->bulk_current_ed, 0);
    write32(&op_regs->control, OHCI_CTRL_HCFS_RUN | OHCI_CTRL_CLE | OHCI_CTRL_CBSR0);
    flush32(&op_regs->interrupt_status, ~0);

    // Some controllers ignore writes to these registers when in suspend state, so write them now.
    uint32_t max_packet_size = ((frame_interval - 210) * 6) / 7;
    uint32_t frame_interval_toggle = (read32(&op_regs->fm_interval) & OHCI_FIT) ^ OHCI_FIT;
    write32(&op_regs->fm_interval, frame_interval_toggle | max_packet_size << 16 | frame_interval);
    write32(&op_regs->periodic_start, (frame_interval * 9) / 10);

    uint32_t rh_descriptor_a = read32(&op_regs->rh_descriptor_a);
    uint32_t rh_descriptor_b = read32(&op_regs->rh_descriptor_b);

    // Construct a hub descriptor for the root hub.
    usb_hub_t root_hub;
    root_hub.ep0            = NULL;
    root_hub.level          = 0;
    root_hub.route          = 0;
    root_hub.num_ports      = rh_descriptor_a & 0xf;
    root_hub.power_up_delay = rh_descriptor_a >> 24;

    // Power up all the ports.
    if (~rh_descriptor_a & OHCI_RHDA_NPS) {
        // If we have individual port power control, clear the port power control mask to allow us to power up all
        // ports at once.
        if (rh_descriptor_a & OHCI_RHDA_PSM) {
            write32(&op_regs->rh_descriptor_b, rh_descriptor_b & OHCI_RHDB_DR);
        }

        // Power up all ports.
        flush32(&op_regs->rh_status, OHCI_RHS_LPSC);
        usleep(root_hub.power_up_delay * 2 * MILLISEC);
    }

    usleep(100*MILLISEC);  // USB maximum device attach time.

    // Scan the ports, looking for hubs and keyboards.
    usb_ep_t keyboards[MAX_KEYBOARDS];
    int num_keyboards = 0;
    int num_devices = 0;
    for (int port_idx = 0; port_idx < root_hub.num_ports; port_idx++) {
        // If we've filled the keyboard info table, abort now.
        if (num_keyboards >= MAX_KEYBOARDS) break;

        uint32_t port_status = read32(&op_regs->rh_port_status[port_idx]);

        // Check the port is powered up.
        if (~port_status & OHCI_PORT_POWERED) continue;

        // Check if anything is connected to this port.
        if (~port_status & OHCI_PORT_CONNECTED) continue;

        // Reset the port.
        if (!reset_ohci_port(op_regs, port_idx)) continue;

        usleep(10*MILLISEC);  // USB reset recovery time

        port_status = read32(&op_regs->rh_port_status[port_idx]);

        // Check the port is active.
        if (~port_status & OHCI_PORT_CONNECTED) continue;
        if (~port_status & OHCI_PORT_ENABLED)   continue;

        // Now the port has been enabled, we can determine the device speed.
        usb_speed_t device_speed = (port_status & OHCI_PORT_LOW_SPEED) ? USB_SPEED_LOW : USB_SPEED_FULL;

        num_devices++;

        // Look for keyboards attached directly or indirectly to this port.
        if (find_attached_usb_keyboards(hcd, &root_hub, 1 + port_idx, device_speed, num_devices,
                                        &num_devices, keyboards, MAX_KEYBOARDS, &num_keyboards)) {
            continue;
        }

        // If we didn't find any keyboard interfaces, we can disable the port.
        write32(&op_regs->rh_port_status[port_idx], OHCI_CLR_PORT_ENABLE);
    }

    print_usb_info(" Found %i device%s, %i keyboard%s",
                   num_devices,   num_devices   != 1 ? "s" : "",
                   num_keyboards, num_keyboards != 1 ? "s" : "");

    if (num_keyboards == 0) {
        // Shut down the host controller and the root hub.
        flush32(&op_regs->control, OHCI_CTRL_HCFS_RST);

        // Delay to allow the controller to reset.
        usleep(10);

        // Deallocate the workspace for this controller.
        pm_map[0].end += num_pages(sizeof(workspace_t));

        return false;
    }


    // Initialise the interrupt ED and TD for each keyboard interface and find the minimum interval.
    int min_interval = OHCI_MAX_INTERVAL;
    uint32_t intr_head_ed = 0;
    for (int kbd_idx = 0; kbd_idx < num_keyboards; kbd_idx++) {
        usb_ep_t *kbd = &keyboards[kbd_idx];

        ohci_ed_t *kbd_ed = &ws->ed[1 + kbd_idx];
        ohci_td_t *kbd_td = &ws->td[3 + kbd_idx];

        hid_kbd_rpt_t *kbd_rpt = &ws->kbd_rpt[kbd_idx];

        build_ohci_td(kbd_td, OHCI_TD_DP_IN | OHCI_TD_DT_USE_TD | OHCI_TD_DT_0 | OHCI_TD_DI_NO_DLY, kbd_rpt, sizeof(hid_kbd_rpt_t));
        build_ohci_ed(kbd_ed, ohci_ed_control(kbd), kbd_td+0, kbd_td+1);

        kbd_ed->next_ed = intr_head_ed;
        intr_head_ed = (uintptr_t)kbd_ed;

        if (kbd->interval < min_interval) {
            min_interval = kbd->interval;
        }
    }

    // Initialise the interrupt table.
    for (int i = 0; i < OHCI_MAX_INTERVAL; i += min_interval) {
        ws->hcca.intr_head_ed[i] = intr_head_ed;
    }
    write32(&op_regs->control, OHCI_CTRL_HCFS_RUN | OHCI_CTRL_CLE | OHCI_CTRL_PLE | OHCI_CTRL_CBSR0);
    flush32(&op_regs->interrupt_status, ~0);

    return true;
}
