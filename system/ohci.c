// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2021 Martin Whitaker.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memrw32.h"
#include "memsize.h"
#include "pmem.h"

#include "memory.h"
#include "unistd.h"
#include "usbkbd.h"
#include "usb.h"

#include "ohci.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// Values defined by the OHCI specification.

// HcRevision register

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
#define OHCI_ED_SPD_LOW         0x00001000      // Speed is Low Speed
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

// Values specific to this implementation.

#define MAX_KEYBOARDS           8                   // per host controller

#define WS_ED_SIZE              (1 + MAX_KEYBOARDS) // Endpoint Descriptors
#define WS_TD_SIZE              (3 + MAX_KEYBOARDS) // Transfer Descriptors
#define WS_DATA_SIZE            1024                // bytes
#define WS_KC_BUFFER_SIZE       8                   // keycodes

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
    // System memory data structures used by the host controller.
    ohci_hcca_t         hcca;
    ohci_ed_t           ed [WS_ED_SIZE];
    ohci_td_t           td [WS_TD_SIZE];

    // Data transfer buffers.
    union {
      volatile uint8_t  data    [WS_DATA_SIZE];
      hid_kbd_rpt_t     kbd_rpt [MAX_KEYBOARDS];
    };

    // Pointer to the host controller registers.
    ohci_op_regs_t      *op_regs;

    // Transient values used during device enumeration and configuration.
    size_t              data_length;

    // Circular buffer for received keycodes.
    uint8_t             kc_buffer [WS_KC_BUFFER_SIZE];
    int                 kc_index_i;
    int                 kc_index_o;
} workspace_t  __attribute__ ((aligned (256)));

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static size_t min(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

static size_t num_pages(size_t size)
{
    return (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

static bool reset_ohci_port(ohci_op_regs_t *regs, int port_idx)
{
    // The OHCI reset lasts for 10ms, but the USB specification calls for 50ms (but not necessarily continuously).
    // So do it 5 times.
    for (int i = 0; i < 5; i++) {
        write32(&regs->rh_port_status[port_idx], OHCI_PORT_CONNECT_CHG | OHCI_PORT_RESET_CHG);
        write32(&regs->rh_port_status[port_idx], OHCI_SET_PORT_RESET);
        if (!wait_until_set(&regs->rh_port_status[port_idx], OHCI_PORT_RESET_CHG, 500*MILLISEC)) {
            return false;
        }
    }
    write32(&regs->rh_port_status[port_idx], OHCI_PORT_RESET_CHG);

    usleep(10*MILLISEC);  // USB reset recovery time

    // Check the port is now active.
    uint32_t status = read32(&regs->rh_port_status[port_idx]);
    if ( status & OHCI_PORT_RESETING)  return false;
    if (~status & OHCI_PORT_CONNECTED) return false;
    if (~status & OHCI_PORT_ENABLED)   return false;

    return true;
}

static ohci_td_t *get_ohci_done_head(workspace_t *ws)
{
    if (!wait_until_set(&ws->op_regs->interrupt_status, OHCI_INTR_WDH, 10*MILLISEC)) {
        return NULL;
    }
    uintptr_t done_head = ws->hcca.done_head & 0xfffffffe;
    write32(&ws->op_regs->interrupt_status, OHCI_INTR_WDH);
    return (ohci_td_t *)done_head;
}

static bool wait_for_ohci_done(workspace_t *ws, int td_expected)
{
    int td_completed = 0;

    ohci_td_t *td = get_ohci_done_head(ws);
    while (td != NULL) {
        td_completed++;
        if ((td->control & OHCI_TD_CC) != OHCI_TD_CC_NO_ERR) {
            return false;
        }
        td = (ohci_td_t *)((uintptr_t)td->next_td);
    }

    return td_completed == td_expected;
}

static void build_ohci_td(ohci_td_t *td, uint32_t control, const volatile void *buffer, size_t length)
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

static uint32_t ohci_ed_control(const usb_ep_info_t *ep)
{
    uint32_t control = OHCI_ED_FMT_GEN
                     | OHCI_ED_DIR_TD
                     | ep->device_speed
                     | ep->max_packet_size << 16
                     | ep->endpoint_num    << 7
                     | ep->device_addr;
    return control;
}

static bool send_setup_request(workspace_t *ws, const usb_ep_info_t *ep, const usb_setup_pkt_t *setup_pkt)
{
    build_ohci_td(&ws->td[0], OHCI_TD_DP_SETUP | OHCI_TD_DT_USE_TD | OHCI_TD_DT_0 | OHCI_TD_DI_NO_INT, setup_pkt, sizeof(usb_setup_pkt_t));
    build_ohci_td(&ws->td[1], OHCI_TD_DP_IN    | OHCI_TD_DT_USE_TD | OHCI_TD_DT_1 | OHCI_TD_DI_NO_DLY, 0, 0);
    build_ohci_ed(&ws->ed[0], ohci_ed_control(ep), &ws->td[0], &ws->td[2]);
    write32(&ws->op_regs->command_status, OHCI_CMD_CLF);
    return wait_for_ohci_done(ws, 2);
}

static bool send_get_data_request(workspace_t *ws, const usb_ep_info_t *ep, const usb_setup_pkt_t *setup_pkt,
                                  const volatile void *buffer, size_t length)
{
    build_ohci_td(&ws->td[0], OHCI_TD_DP_SETUP | OHCI_TD_DT_USE_TD | OHCI_TD_DT_0 | OHCI_TD_DI_NO_INT, setup_pkt, sizeof(usb_setup_pkt_t));
    build_ohci_td(&ws->td[1], OHCI_TD_DP_IN    | OHCI_TD_DT_USE_TD | OHCI_TD_DT_1 | OHCI_TD_DI_NO_INT, buffer, length);
    build_ohci_td(&ws->td[2], OHCI_TD_DP_OUT   | OHCI_TD_DT_USE_TD | OHCI_TD_DT_1 | OHCI_TD_DI_NO_DLY, 0, 0);
    build_ohci_ed(&ws->ed[0], ohci_ed_control(ep), &ws->td[0], &ws->td[3]);
    write32(&ws->op_regs->command_status, OHCI_CMD_CLF);
    return wait_for_ohci_done(ws, 3);
}

static bool initialise_device(workspace_t *ws, int port_idx, int device_speed, int device_addr, usb_ep_info_t *ep0)
{
    usb_setup_pkt_t setup_pkt;

    // Initialise the endpoint descriptor for the default control pipe (endpoint 0).
    ep0->device_speed    = device_speed;
    ep0->device_addr     = 0;
    ep0->interface_num   = 0;
    ep0->endpoint_num    = 0;
    ep0->max_packet_size = 8;
    ep0->interval        = 0;

    // The device should currently be in Default state. We first fetch the first 8 bytes of the device descriptor to
    // discover the maximum packet size for the control endpoint. We then set the device address, which moves the
    // device into Addressed state, and fetch the full device descriptor. We don't currently make use of any of the
    // other fields of the device descriptor, but some USB devices may not work correctly if we don't fetch it.
    size_t fetch_length = 8;
    goto fetch_device_descriptor;

  set_address:
    build_setup_packet(&setup_pkt, 0x00, 0x05, device_addr, 0, 0);
    if (!send_setup_request(ws, ep0, &setup_pkt)) {
        return false;
    }
    ep0->device_addr = device_addr;
    usleep(2*MILLISEC);  // USB set address recovery time.

  fetch_device_descriptor:
    build_setup_packet(&setup_pkt, 0x80, 0x06, USB_DESC_DEVICE << 8, 0, fetch_length);
    if (!send_get_data_request(ws, ep0, &setup_pkt, ws->data, fetch_length)
    ||  !valid_usb_device_descriptor(ws->data)) {
        return false;
    }

    if (fetch_length == 8) {
        usb_device_desc_t *device = (usb_device_desc_t *)ws->data;
        ep0->max_packet_size = device->max_packet_size;
        if (!valid_usb_max_packet_size(ep0->max_packet_size, ep0->device_speed == OHCI_ED_SPD_LOW)) {
            return false;
        }
        if (usb_init_options & USB_EXTRA_RESET) {
            if (!reset_ohci_port(ws->op_regs, port_idx)) {
                return false;
            }
        }
        fetch_length = sizeof(usb_device_desc_t);
        goto set_address;
    }

    // Fetch the first configuration descriptor and the associated interface and endpoint descriptors. Start by
    // requesting just the configuration descriptor. Then read the descriptor to determine whether we need to fetch
    // more data.

    fetch_length = sizeof(usb_config_desc_t);
  fetch_config_descriptor:
    build_setup_packet(&setup_pkt, 0x80, 0x06, USB_DESC_CONFIG << 8, 0, fetch_length);
    if (!send_get_data_request(ws, ep0, &setup_pkt, ws->data, fetch_length)
    ||  !valid_usb_config_descriptor(ws->data)) {
        return false;
    }
    usb_config_desc_t *config = (usb_config_desc_t *)ws->data;
    size_t total_length = min(config->total_length, WS_DATA_SIZE);
    if (total_length > fetch_length) {
        fetch_length = total_length;
        goto fetch_config_descriptor;
    }
    ws->data_length = total_length;

    return true;
}

static bool configure_keyboard(workspace_t *ws, const usb_ep_info_t *ep0, const usb_ep_info_t *kbd)
{
    usb_setup_pkt_t setup_pkt;

    // Set the device configuration.
    build_setup_packet(&setup_pkt, 0x00, 0x09, 1, 0, 0);
    if (!send_setup_request(ws, ep0, &setup_pkt)) {
        return false;
    }

    // Set the idle duration to infinite.
    build_setup_packet(&setup_pkt, 0x21, 0x0a, 0, kbd->interface_num, 0);
    if (!send_setup_request(ws, ep0, &setup_pkt)) {
        return false;
    }

    // Select the boot protocol.
    build_setup_packet(&setup_pkt, 0x21, 0x0b, 0, kbd->interface_num, 0);
    if (!send_setup_request(ws, ep0, &setup_pkt)) {
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void *ohci_init(uintptr_t base_addr)
{
    ohci_op_regs_t *regs = (ohci_op_regs_t *)base_addr;

    // Check the host controller revison.
    if ((read32(&regs->revision) & 0xff) != 0x10) {
        return NULL;
    }

    // Take ownership from the SMM if necessary.
    if (read32(&regs->control) & OHCI_CTRL_IR) {
        write32(&regs->interrupt_enable, OHCI_INTR_OC);
        flush32(&regs->command_status, OHCI_CMD_OCR);
        if (!wait_until_clr(&regs->control, OHCI_CTRL_IR, 1000*MILLISEC)) {
            return NULL;
        }
    }

    // Preserve the FM interval set by the SMM or BIOS.
    // If not set, use the default value.
    uint32_t fm_interval = read32(&regs->fm_interval) & 0x3fff;
    if (fm_interval == 0) {
        fm_interval = 0x2edf;
    }

    // Prepare for host controller setup (see section 5.1.1.3 of the OHCI spec.).
    switch (read32(&regs->control) & OHCI_CTRL_HCFS) {
      case OHCI_CTRL_HCFS_RST:
        usleep(50*MILLISEC);
        break;
      case OHCI_CTRL_HCFS_SUS:
      case OHCI_CTRL_HCFS_RES:
        flush32(&regs->control, OHCI_CTRL_HCFS_SUS);
        usleep(10*MILLISEC);
        break;
      default: // operational
        break;
    }

    // Reset the host controller.
    write32(&regs->command_status, OHCI_CMD_HCR);
    if (!wait_until_clr(&regs->command_status, OHCI_CMD_HCR, 30)) {
        return NULL;
    }

    // Check we are now in SUSPEND state.
    if ((read32(&regs->control) & OHCI_CTRL_HCFS) != OHCI_CTRL_HCFS_SUS) {
        return NULL;
    }

    // Allocate and initialise a workspace for this controller. This needs to be permanently mapped into virtual
    // memory, so allocate it in the first segment.
    // TODO: check for segment overflow.
    pm_map[0].end -= num_pages(sizeof(workspace_t));
    uintptr_t workspace_addr = pm_map[0].end << PAGE_SHIFT;
    workspace_t *ws = (workspace_t *)workspace_addr;

    memset(ws, 0, sizeof(workspace_t));

    // Initialise the control list ED.
    ws->ed[0].control = OHCI_ED_SKIP;
    ws->ed[0].next_ed = 0;

    // Initialise the pointer to the device registers.
    ws->op_regs = regs;

    // Initialise the keycode buffer.
    ws->kc_index_i = 0;
    ws->kc_index_o = 0;

    // Initialise the host controller.
    uint32_t max_packet_size = ((fm_interval - 210) * 6) / 7;
    write32(&regs->fm_interval, 1 << 31 | max_packet_size << 16 | fm_interval);
    write32(&regs->periodic_start, (fm_interval * 9) / 10);
    write32(&regs->hcca, (uintptr_t)(&ws->hcca));
    write32(&regs->ctrl_head_ed, (uintptr_t)(&ws->ed[0]));
    write32(&regs->bulk_head_ed, 0);
    write32(&regs->ctrl_current_ed, 0);
    write32(&regs->bulk_current_ed, 0);
    write32(&regs->control, OHCI_CTRL_HCFS_RUN | OHCI_CTRL_CLE | OHCI_CTRL_CBSR0);
    flush32(&regs->interrupt_status, ~0);

    // Power up the ports.
    uint32_t rh_descriptor_a = read32(&regs->rh_descriptor_a);
    uint32_t rh_descriptor_b = read32(&regs->rh_descriptor_b);
    if (~rh_descriptor_a & OHCI_RHDA_NPS) {
        // If we have individual port power control, clear the port power control mask to allow us to power up all
        // ports now.
        if (rh_descriptor_a & OHCI_RHDA_PSM) {
            write32(&regs->rh_descriptor_b, rh_descriptor_b & OHCI_RHDB_DR);
        }

        // Power up all ports.
        flush32(&regs->rh_status, OHCI_RHS_LPSC);
        int port_power_up_delay = (rh_descriptor_a >> 24) * 2*MILLISEC;
        usleep(port_power_up_delay);

        usleep(100*MILLISEC);  // USB maximum device attach time.
    }

    // Scan the ports, looking for keyboards.
    usb_ep_info_t keyboard_info[MAX_KEYBOARDS];
    int num_keyboards = 0;
    int num_devices = 0;
    int device_addr = 0;
    int min_interval = OHCI_MAX_INTERVAL;
    int num_ports = rh_descriptor_a & 0xf;
    for (int port_idx = 0; port_idx < num_ports; port_idx++) {
        if (num_keyboards >= MAX_KEYBOARDS) continue;

        uint32_t port_status = read32(&regs->rh_port_status[port_idx]);

        // Check the port is powered up.
        if (~port_status & OHCI_PORT_POWERED) continue;

        // Check if anything is connected to this port.
        if (~port_status & OHCI_PORT_CONNECTED) continue;
        num_devices++;

        // Reset the port.
        if (!reset_ohci_port(regs, port_idx)) continue;

        // Now the port has been reset, we can determine the device speed.
        port_status = read32(&regs->rh_port_status[port_idx]);
        uint32_t device_speed = (port_status & OHCI_PORT_LOW_SPEED) ? OHCI_ED_SPD_LOW : OHCI_ED_SPD_FULL;

        // Initialise the USB device. If successful, this leaves a set of configuration descriptors in the workspace
        // data buffer.
        usb_ep_info_t ep0;
        if (!initialise_device(ws, port_idx, device_speed, ++device_addr, &ep0)) {
            goto disable_port;
        }

        // Scan the descriptors to see if this device has one or more keyboard interfaces and if so, record that
        // information in the keyboard info table.
        usb_ep_info_t *new_keyboard_info = &keyboard_info[num_keyboards];
        int new_keyboards = get_usb_keyboard_info_from_descriptors(ws->data, ws->data_length, new_keyboard_info,
                                                                   MAX_KEYBOARDS - num_keyboards);

        // Complete the new entries in the keyboard info table and configure the keyboard interfaces.
        for (int kbd_idx = 0; kbd_idx < new_keyboards; kbd_idx++) {
            usb_ep_info_t *kbd = &new_keyboard_info[kbd_idx];
            kbd->device_speed = device_speed;
            kbd->device_addr  = device_addr;
            if (kbd->interval < min_interval) {
                min_interval = kbd->interval;
            }
            configure_keyboard(ws, &ep0, kbd);

            print_usb_info(" Keyboard found on port %i interface %i endpoint %i",
                           1 + port_idx, kbd->interface_num, kbd->endpoint_num);
        }
        if (new_keyboards > 0) {
            num_keyboards += new_keyboards;
            continue;
        }

        // If we didn't find any keyboard interfaces, we can disable the port.
      disable_port:
        write32(&regs->rh_port_status[port_idx], OHCI_CLR_PORT_ENABLE);
    }

    print_usb_info("  Found %i device%s, %i keyboard%s",
                   num_devices,   num_devices   != 1 ? "s" : "",
                   num_keyboards, num_keyboards != 1 ? "s" : "");

    if (num_keyboards == 0) {
        // Shut down the host controller and the root hub.
        flush32(&regs->control, OHCI_CTRL_HCFS_RST);

        // Delay to allow the controller to reset.
        usleep(10);

        // Deallocate the workspace for this controller.
        pm_map[0].end += num_pages(sizeof(workspace_t));

        return NULL;
    }

    // Initialise the interrupt ED and TD for each keyboard interface.
    ohci_ed_t *last_kbd_ed = NULL;
    for (int kbd_idx = 0; kbd_idx < num_keyboards; kbd_idx++) {
        usb_ep_info_t *kbd = &keyboard_info[kbd_idx];

        ohci_ed_t *kbd_ed = &ws->ed[1 + kbd_idx];
        ohci_td_t *kbd_td = &ws->td[3 + kbd_idx];

        hid_kbd_rpt_t *kbd_rpt = &ws->kbd_rpt[kbd_idx];

        build_ohci_td(kbd_td, OHCI_TD_DP_IN | OHCI_TD_DT_USE_TD | OHCI_TD_DT_0 | OHCI_TD_DI_NO_DLY, kbd_rpt, sizeof(hid_kbd_rpt_t));
        build_ohci_ed(kbd_ed, ohci_ed_control(kbd), kbd_td+0, kbd_td+1);

        kbd_ed->next_ed = (uintptr_t)last_kbd_ed;
        last_kbd_ed = kbd_ed;
    }

    // Initialise the interrupt table.
    for (int i = 0; i < OHCI_MAX_INTERVAL; i += min_interval) {
        ws->hcca.intr_head_ed[i] = (uintptr_t)last_kbd_ed;
    }
    write32(&regs->control, OHCI_CTRL_HCFS_RUN | OHCI_CTRL_CLE | OHCI_CTRL_PLE | OHCI_CTRL_CBSR0);
    flush32(&regs->interrupt_status, ~0);

    return ws;
}

uint8_t ohci_get_keycode(void *workspace)
{
    workspace_t *ws = (workspace_t *)workspace;

    ohci_td_t *td = get_ohci_done_head(ws);
    while (td != NULL) {
        int kbd_idx = td - ws->td - 3;

        hid_kbd_rpt_t *kbd_rpt = &ws->kbd_rpt[kbd_idx];

        if ((td->control & OHCI_TD_CC) == OHCI_TD_CC_NO_ERR) {
            uint8_t keycode = kbd_rpt->key_code[0];
            if (keycode != 0) {
                int kc_index_i = ws->kc_index_i;
                int kc_index_n = (kc_index_i + 1) % WS_KC_BUFFER_SIZE;
                if (kc_index_n != ws->kc_index_o) {
                    ws->kc_buffer[kc_index_i] = keycode;
                    ws->kc_index_i = kc_index_n;
                }
            }
        }

        ohci_td_t *next_td = (ohci_td_t *)((uintptr_t)td->next_td);

        ohci_ed_t *ed = &ws->ed[1 + kbd_idx];
        build_ohci_td(td, td->control & ~OHCI_TD_CC, kbd_rpt, sizeof(hid_kbd_rpt_t));
        build_ohci_ed(ed, ed->control, td+0, td+1);

        td = next_td;
    }

    int kc_index_o = ws->kc_index_o;
    if (kc_index_o != ws->kc_index_i) {
        ws->kc_index_o = (kc_index_o + 1) % WS_KC_BUFFER_SIZE;
        return ws->kc_buffer[kc_index_o];
    }

    return 0;
}
