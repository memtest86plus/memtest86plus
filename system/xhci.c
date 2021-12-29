// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2021 Martin Whitaker.

#include <stdbool.h>
#include <stdint.h>

#include "memrw32.h"
#include "memsize.h"
#include "pmem.h"

#include "memory.h"
#include "unistd.h"
#include "usbkbd.h"
#include "usb.h"

#include "xhci.h"

#define QEMU_WORKAROUND 1

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// Values defined by the XHCI specification.

// Basic limits

#define XHCI_MAX_PORTS                  255
#define XHCI_MAX_SLOTS                  255

#define XHCI_MAX_CONTEXT_SIZE           64

#define XHCI_MAX_IP_CONTEXT_SIZE        (33 * XHCI_MAX_CONTEXT_SIZE)
#define XHCI_MAX_OP_CONTEXT_SIZE        (32 * XHCI_MAX_CONTEXT_SIZE)

// Extended capability ID values

#define XHCI_EXT_CAP_LEGACY_SUPPORT     1
#define XHCI_EXT_CAP_SUPPORTED_PROTOCOL 2

// USB Command register

#define XHCI_USBCMD_R_S                 0x00000001      // Run/Stop
#define XHCI_USBCMD_HCRST               0x00000002      // Host Controller Reset
#define XHCI_USBCMD_INTE                0x00000004      // Interrupter Enable
#define XHCI_USBCMD_HSEE                0x00000008      // Host System Error Enable

// USB Status register

#define XHCI_USBSTS_HCH                 0x00000001      // Host Controller Halted
#define XHCI_USBSTS_HSE                 0x00000004      // Host System Error
#define XHCI_USBSTS_CNR                 0x00000800      // Controller Not Ready

// Port Status and Control register

#define XHCI_PORT_SC_CCS                0x00000001      // Current Connect Status
#define XHCI_PORT_SC_PED                0x00000002      // Port Enable/Disable
#define XHCI_PORT_SC_OCA                0x00000004      // Over-Current
#define XHCI_PORT_SC_PR                 0x00000010      // Port Reset
#define XHCI_PORT_SC_PLS                0x000001e0      // Port Link State
#define XHCI_PORT_SC_PP                 0x00000200      // Port Power
#define XHCI_PORT_SC_PS                 0x00003c00      // Port Speed
#define XHCI_PORT_SC_PRSC               0x00200000      // Port Reset

#define XHCI_PORT_SC_PS_OFFSET          10              // first bit of Port Speed

// Transfer Request Block data structure

#define XHCI_TRB_ENT                    (1 << 1)        // Evaluate Next TRB
#define XHCI_TRB_TC                     (1 << 1)        // Toggle Cycle
#define XHCI_TRB_ISP                    (1 << 2)        // Interrupt on Short Packet
#define XHCI_TRB_NS                     (1 << 3)        // No Snoop
#define XHCI_TRB_CH                     (1 << 4)        // Chain bit
#define XHCI_TRB_IOC                    (1 << 5)        // Interrupt on Completion
#define XHCI_TRB_IDT                    (1 << 6)        // Immediate Data
#define XHCI_TRB_BEI                    (1 << 9)        // Block Event Interrupt
#define XHCI_TRB_BSR                    (1 << 9)        // Block Set Address Request

#define XHCI_TRB_TYPE                   (63 << 10)
#define XHCI_TRB_NORMAL                 (1  << 10)
#define XHCI_TRB_SETUP_STAGE            (2  << 10)
#define XHCI_TRB_DATA_STAGE             (3  << 10)
#define XHCI_TRB_STATUS_STAGE           (4  << 10)
#define XHCI_TRB_LINK                   (6  << 10)
#define XHCI_TRB_ENABLE_SLOT            (9  << 10)
#define XHCI_TRB_DISABLE_SLOT           (10 << 10)
#define XHCI_TRB_ADDRESS_DEVICE         (11 << 10)
#define XHCI_TRB_CONFIGURE_ENDPOINT     (12 << 10)
#define XHCI_TRB_EVALUATE_CONTEXT       (13 << 10)
#define XHCI_TRB_NOOP                   (23 << 10)
#define XHCI_TRB_TRANSFER_EVENT         (32 << 10)
#define XHCI_TRB_COMMAND_COMPLETE       (33 << 10)

#define XHCI_TRB_TRT_NO_DATA            (0 << 16)       // Transfer Type (Setup Stage TRB)
#define XHCI_TRB_TRT_OUT                (2 << 16)       // Transfer Type (Setup Stage TRB)
#define XHCI_TRB_TRT_IN                 (3 << 16)       // Transfer Type (Setup Stage TRB)

#define XHCI_TRB_DIR_OUT                (0 << 16)       // Direction (Data/Status Stage TRB)
#define XHCI_TRB_DIR_IN                 (1 << 16)       // Direction (Data/Status Stage TRB)

// Add Context flags

#define XHCI_CONTEXT_A0                 (1 << 0)
#define XHCI_CONTEXT_A1                 (1 << 1)

// Port Speed values

#define XHCI_FULL_SPEED                 1
#define XHCI_LOW_SPEED                  2
#define XHCI_HIGH_SPEED                 3

// Endpoint Type values

#define XHCI_EP_NOT_VALID               0
#define XHCI_EP_ISOCH_OUT               1
#define XHCI_EP_BULK_OUT                2
#define XHCI_EP_INTERRUPT_OUT           3
#define XHCI_EP_CONTROL                 4
#define XHCI_EP_ISOCH_IN                5
#define XHCI_EP_BULK_IN                 6
#define XHCI_EP_INTERRUPT_IN            7

// Event Completion Code values

#define	XHCI_EVENT_CC_SUCCESS		1
#define	XHCI_EVENT_CC_TIMEOUT		191     // specific to this implementation

// Values specific to this implementation.

#define PORT_TYPE_PST_MASK              0x1f    // Protocol Slot Type mask
#define PORT_TYPE_USB2                  0x40
#define PORT_TYPE_USB3                  0x80

#define MAX_KEYBOARDS                   8       // per host controller

#define WS_CR_SIZE                      8       // TRBs    (multiple of 4 to maintain 64 byte alignment)
#define WS_ER_SIZE                      16      // TRBs    (multiple of 4 to maintain 64 byte alignment)
#define WS_ERST_SIZE                    4       // entries (multiple of 4 to maintain 64 byte alignment)
#define WS_DATA_SIZE                    1024    // bytes
#define WS_KC_BUFFER_SIZE               8       // keycodes

#define EP_TR_SIZE                      8       // TRBs    (multiple of 4 to maintain 64 byte alignment)

#define MILLISEC                        1000    // in microseconds

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

// Register sets defined by the XHCI specification.

typedef struct {
    uint8_t             cap_length;
    uint8_t             reserved;
    uint16_t            hci_version;
    uint32_t            hcs_params1;
    uint32_t            hcs_params2;
    uint32_t            hcs_params3;
    uint32_t            hcc_params1;
    uint32_t            db_offset;
    uint32_t            rts_offset;
    uint32_t            hcc_params2;
} xhci_cap_regs_t;

typedef volatile struct {
    uint32_t            sc;
    uint32_t            pmsc;
    uint32_t            li;
    uint32_t            hlpmc;
} xhci_port_regs_t;

typedef volatile struct {
    uint32_t            usb_command;
    uint32_t            usb_status;
    uint32_t            page_size;
    uint32_t            reserved1[2];
    uint32_t            dn_control;
    uint64_t            cr_control;
    uint32_t            reserved2[4];
    uint64_t            dcbaap;
    uint32_t            config;
    uint32_t            reserved3[241];
    xhci_port_regs_t    port_regs[];
} xhci_op_regs_t;

typedef volatile struct {
    uint32_t            management;
    uint32_t            moderation;
    uint32_t            erst_size;
    uint32_t            reserved;
    uint64_t            erst_addr;
    uint64_t            erdp;
} xhci_int_regs_t;

typedef volatile struct {
    uint32_t            mf_index;
    uint32_t            reserved[7];
    xhci_int_regs_t     ir[];
} xhci_rt_regs_t;

typedef volatile uint32_t xhci_db_reg_t;

// Extended capability structures defined by the XHCI specification.

typedef struct {
    uint8_t             id;
    uint8_t             next_offset;
    uint8_t             id_specific[2];
} xhci_ext_cap_t;

typedef struct {
    uint8_t             id;
    uint8_t             next_offset;
    uint8_t             bios_owns;
    uint8_t             host_owns;
    uint32_t            ctrl_stat;
} xhci_legacy_support_t;

typedef struct {
    uint8_t             id;
    uint8_t             next_offset;
    uint8_t             revision_minor;
    uint8_t             revision_major;
    uint32_t            name_string;
    uint8_t             port_offset;
    uint8_t             port_count;
    uint16_t            params1;
    uint32_t            params2;
    uint32_t            speed_id[];
} xhci_supported_protocol_t;

// Data structures defined by the XHCI specification.

typedef struct {
    uint32_t            drop_context_flags;
    uint32_t            add_context_flags;
    uint32_t            reserved1[5];
    uint8_t             config_value;
    uint8_t             interface_num;
    uint8_t             alternate_setting;
    uint8_t             reserved2;
} xhci_ctrl_context_t  __attribute__ ((aligned (16)));

typedef struct {
    uint32_t            params1;
    uint16_t            max_exit_latency;
    uint8_t             root_hub_port_num;
    uint8_t             num_ports;
    uint8_t             tt_hub_slot_id;
    uint8_t             tt_port_num;
    uint16_t            params2;
    uint8_t             usb_dev_addr;
    uint8_t             reserved1;
    uint8_t             reserved2;
    uint8_t             slot_state;
    uint32_t            reserved3[4];
} xhci_slot_context_t  __attribute__ ((aligned (16)));

typedef struct {
    uint8_t             state;
    uint8_t             params1;
    uint8_t             interval;
    uint8_t             max_esit_payload_h;
    uint8_t             params2;
    uint8_t             max_burst_size;
    uint16_t            max_packet_size;
    uint64_t            tr_dequeue_ptr;
    uint16_t            average_trb_length;
    uint16_t            max_esit_payload_l;
    uint32_t            reserved[3];
} xhci_ep_context_t  __attribute__ ((aligned (16)));

typedef struct {
    xhci_ctrl_context_t ctrl;
    xhci_slot_context_t slot;
    xhci_ep_context_t   ep[31];
} xhci_input_context_t  __attribute__ ((aligned (16)));

typedef volatile struct {
    uint64_t            params1;
    uint32_t            params2;
    uint32_t            control;
} xhci_trb_t  __attribute__ ((aligned (16)));

typedef volatile struct {
    uint64_t            segment_addr;
    uint16_t            segment_size;
    uint16_t            reserved1;
    uint32_t            reserved2;
} xhci_erst_entry_t  __attribute__ ((aligned (16)));

// Data structures specific to this implementation.

typedef volatile struct {
    xhci_trb_t          tr [EP_TR_SIZE];
    uint32_t            enqueue_state;
    uint32_t            padding[15];
} ep_tr_t  __attribute__ ((aligned (64)));

typedef struct {
    // System memory data structures used by the host controller.
    xhci_trb_t          cr   [WS_CR_SIZE];      // command ring
    xhci_trb_t          er   [WS_ER_SIZE];      // event ring
    xhci_erst_entry_t   erst [WS_ERST_SIZE];    // event ring segment table

    // Data transfer buffers.
    union {
      volatile uint8_t  data    [WS_DATA_SIZE];
      hid_kbd_rpt_t     kbd_rpt [MAX_KEYBOARDS];
    };

    // Keyboard data transfer rings
    ep_tr_t             kbd_tr  [MAX_KEYBOARDS];

    // Pointers to the host controller registers.
    xhci_op_regs_t      *op_regs;
    xhci_rt_regs_t      *rt_regs;
    xhci_db_reg_t       *db_regs;

    // Device context information.
    uint64_t            *device_context_index;
    size_t              context_size;

    // Host controller TRB ring enqueue cycle and index.
    uint32_t            cr_enqueue_state;
    uint32_t            er_dequeue_state;

    // Transient values used during device enumeration and configuration.
    size_t              data_length;
    uintptr_t           input_context_addr;
    uintptr_t           output_context_addr;
    uintptr_t           control_ep_tr_addr;

    // Keyboard slot ID lookup table
    uint8_t             kbd_slot_id [MAX_KEYBOARDS];

    // Keyboard endpoint ID lookup table
    uint8_t             kbd_ep_id   [MAX_KEYBOARDS];

    // Circular buffer for received keycodes.
    uint8_t             kc_buffer [WS_KC_BUFFER_SIZE];
    int32_t             kc_index_i;
    int32_t             kc_index_o;

} workspace_t  __attribute__ ((aligned (64)));

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

// The heap segment of the physical memory map is used when allocating memory
// to the controller that we don't need to access during normal operation.
// Any memory we do need to access during normal operation is allocated from
// segment 0, which is permanently mapped into the virtual memory address space.

static int heap_segment = -1;

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

static size_t round_up(size_t size, size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

// The read64 and write64 functions provided here provide compatibility with both
// 32-bit and 64-bit hosts and with both 32-bit and 64-bit XHCI controllers.

static uint64_t read64(const volatile uint64_t *ptr)
{
    uint32_t val_l = read32((const volatile uint32_t *)ptr + 0);
    uint32_t val_h = read32((const volatile uint32_t *)ptr + 1);
    return (uint64_t)val_h << 32 | (uint64_t)val_l;
}

static void write64(volatile uint64_t *ptr, uint64_t val)
{
    write32((volatile uint32_t *)ptr + 0, (uint32_t)(val >>  0));
    write32((volatile uint32_t *)ptr + 1, (uint32_t)(val >> 32));
}

#ifdef QEMU_WORKAROUND
static void memcpy32(void *dst, const void *src, size_t size)
{
    uint32_t *dst_word = (uint32_t *)dst;
    uint32_t *src_word = (uint32_t *)src;
    size_t num_words = size / sizeof(uint32_t);
    for (size_t i = 0; i < num_words; i++) {
        write32(&dst_word[i], read32(&src_word[i]));
    }
}
#endif

static int default_max_packet_size(int device_speed)
{
    switch (device_speed) {
      case XHCI_LOW_SPEED:
        return 8;
      case XHCI_FULL_SPEED:
        return 64;
      case XHCI_HIGH_SPEED:
        return 64;
      default:
        return 512;
    }
}

static int xhci_ep_interval(int config_interval, int device_speed)
{
    if (device_speed < XHCI_HIGH_SPEED) {
        int log2_interval = 7;
        while ((1 << log2_interval) > config_interval) {
            log2_interval--;
        }
        return 3 + log2_interval;
    } else {
        if (config_interval >= 1 && config_interval <= 16) {
            return config_interval - 1;
        } else {
            return 3;
        }
    }
}

static bool reset_host_controller(xhci_op_regs_t *op_regs)
{
    write32(&op_regs->usb_command, read32(&op_regs->usb_command) | XHCI_USBCMD_HCRST);

    usleep(1*MILLISEC);  // some controllers need time to recover from reset

    return wait_until_clr(&op_regs->usb_command, XHCI_USBCMD_HCRST, 1000*MILLISEC)
        && wait_until_clr(&op_regs->usb_status,  XHCI_USBSTS_CNR,   1000*MILLISEC);
}

static bool start_host_controller(xhci_op_regs_t *op_regs)
{
    write32(&op_regs->usb_command, read32(&op_regs->usb_command) | XHCI_USBCMD_R_S);
    return wait_until_clr(&op_regs->usb_status, XHCI_USBSTS_HCH, 20*MILLISEC);
}

static bool halt_host_controller(xhci_op_regs_t *op_regs)
{
    write32(&op_regs->usb_command, read32(&op_regs->usb_command) & ~XHCI_USBCMD_R_S);
    return wait_until_set(&op_regs->usb_status, XHCI_USBSTS_HCH, 20*MILLISEC);
}

static int get_xhci_device_speed(xhci_op_regs_t *op_regs, int port_idx)
{
    return (read32(&op_regs->port_regs[port_idx].sc) & XHCI_PORT_SC_PS) >> XHCI_PORT_SC_PS_OFFSET;
}

static void reset_xhci_port(xhci_op_regs_t *op_regs, int port_idx)
{
    write32(&op_regs->port_regs[port_idx].sc, XHCI_PORT_SC_PP | XHCI_PORT_SC_PR);
}

static void disable_xhci_port(xhci_op_regs_t *op_regs, int port_idx)
{
    write32(&op_regs->port_regs[port_idx].sc, XHCI_PORT_SC_PP | XHCI_PORT_SC_PED);
    wait_until_clr(&op_regs->port_regs[port_idx].sc, XHCI_PORT_SC_PED, 20*MILLISEC);
}

static void ring_host_controller_doorbell(xhci_db_reg_t *db_regs)
{
    write32(&db_regs[0], 0);
}

static void ring_device_doorbell(xhci_db_reg_t *db_regs, int slot_id, int db_target)
{
    write32(&db_regs[slot_id], db_target);
}

static uint32_t event_type(const xhci_trb_t *event)
{
    return event->control & XHCI_TRB_TYPE;
}

static int event_cc(const xhci_trb_t *event)
{
    return event->params2 >> 24;
}

static int event_slot_id(const xhci_trb_t *event)
{
    return event->control >> 24;
}

static int event_ep_id(const xhci_trb_t *event)
{
    return (event->control >> 16) & 0x1f;
}

static uint32_t enqueue_trb(xhci_trb_t *trb_ring, uint32_t ring_size, uint32_t enqueue_state,
                            uint32_t control, uint64_t params1, uint32_t params2)
{
    // The ring enqueue state records the current cycle and the next free slot.
    uint32_t cycle = enqueue_state / ring_size;
    uint32_t index = enqueue_state % ring_size;

    // If at the last slot, insert a Link TRB and start a new cycle.
    if (index == (ring_size - 1)) {
        write64(&trb_ring[index].params1, (uintptr_t)trb_ring);
        write32(&trb_ring[index].params2, 0);
        write32(&trb_ring[index].control, XHCI_TRB_LINK | XHCI_TRB_TC | cycle);
        cycle ^= 1;
        index = 0;
    }

    // Insert the TRB.
    write64(&trb_ring[index].params1, params1);
    write32(&trb_ring[index].params2, params2);
    write32(&trb_ring[index].control, control | cycle);
    index++;

    // Return the new ring enqueue state.
    return cycle * ring_size + index;
}

static void enqueue_xhci_command(workspace_t *ws, uint32_t control, uint64_t params1, uint32_t params2)
{
    ws->cr_enqueue_state = enqueue_trb(ws->cr, WS_CR_SIZE, ws->cr_enqueue_state, control, params1, params2);
}

static bool get_xhci_event(workspace_t *ws, xhci_trb_t *event)
{
    // Get the event ring dequeue state, which records the current cycle and next slot to be read.
    uint32_t dequeue_state = ws->er_dequeue_state;
    uint32_t cycle = dequeue_state / WS_ER_SIZE;
    uint32_t index = dequeue_state % WS_ER_SIZE;

    // Copy the next slot.
    event->params1 = ws->er[index].params1;
    event->params2 = ws->er[index].params2;
    event->control = ws->er[index].control;

    // If the cycle count doesn't match, that slot hasn't been filled yet.
    if ((event->control & 0x1) != cycle) return false;

    // Advance the dequeue pointer.
    write64(&ws->rt_regs->ir[0].erdp, (uintptr_t)(&ws->er[index]));

    // Update the event ring dequeue state.
    if (index == (WS_ER_SIZE - 1)) {
        cycle ^= 1;
        index = 0;
    } else {
        index++;
    }
    ws->er_dequeue_state = cycle * WS_ER_SIZE + index;

    return true;
}

static uint32_t wait_for_xhci_event(workspace_t *ws, uint32_t wanted_type, int max_time, xhci_trb_t *event)
{
    int timer = max_time >> 3;
    while (!get_xhci_event(ws, event) || event_type(event) != wanted_type) {
        if (timer == 0) return XHCI_EVENT_CC_TIMEOUT;
        usleep(8);
        timer--;
    }
    return event_cc(event);
}

static void issue_setup_stage_trb(ep_tr_t *ep_tr, const usb_setup_pkt_t *setup_pkt)
{
    uint64_t params1 = *(const uint64_t *)setup_pkt;
    uint32_t params2 = sizeof(usb_setup_pkt_t);
    uint32_t control = XHCI_TRB_SETUP_STAGE | XHCI_TRB_TRT_IN | XHCI_TRB_IDT;
    ep_tr->enqueue_state = enqueue_trb(ep_tr->tr, EP_TR_SIZE, ep_tr->enqueue_state, control, params1, params2);
}

static void issue_data_stage_trb(ep_tr_t *ep_tr, const volatile void *buffer, uint32_t dir, size_t transfer_length)
{
    uint64_t params1 = (uintptr_t)buffer;
    uint32_t params2 = transfer_length;
    uint32_t control = XHCI_TRB_DATA_STAGE | dir;
    ep_tr->enqueue_state = enqueue_trb(ep_tr->tr, EP_TR_SIZE, ep_tr->enqueue_state, control, params1, params2);
}

static void issue_status_stage_trb(ep_tr_t *ep_tr, uint32_t dir)
{
    uint32_t control = XHCI_TRB_STATUS_STAGE | dir | XHCI_TRB_IOC;
    ep_tr->enqueue_state = enqueue_trb(ep_tr->tr, EP_TR_SIZE, ep_tr->enqueue_state, control, 0, 0);
}

static void issue_normal_trb(ep_tr_t *ep_tr, const volatile void *buffer, uint32_t dir, size_t transfer_length)
{
    uint64_t params1 = (uintptr_t)buffer;
    uint32_t params2 = transfer_length;
    uint32_t control = XHCI_TRB_NORMAL | dir | XHCI_TRB_IOC;
    ep_tr->enqueue_state = enqueue_trb(ep_tr->tr, EP_TR_SIZE, ep_tr->enqueue_state, control, params1, params2);
}

static bool send_setup_request(workspace_t *ws, int slot_id, const usb_setup_pkt_t *setup_pkt)
{
    xhci_trb_t event;

    ep_tr_t *ep_tr = (ep_tr_t *)ws->control_ep_tr_addr;

    issue_setup_stage_trb(ep_tr, setup_pkt);
    issue_status_stage_trb(ep_tr, XHCI_TRB_DIR_IN);
    ring_device_doorbell(ws->db_regs, slot_id, 1);
    return wait_for_xhci_event(ws, XHCI_TRB_TRANSFER_EVENT, 100*MILLISEC, &event) == XHCI_EVENT_CC_SUCCESS;
}

static bool send_get_data_request(workspace_t *ws, int slot_id, const usb_setup_pkt_t *setup_pkt,
                                  const volatile void *buffer, size_t length)
{
    xhci_trb_t event;

    ep_tr_t *ep_tr = (ep_tr_t *)ws->control_ep_tr_addr;

    issue_setup_stage_trb(ep_tr, setup_pkt);
    issue_data_stage_trb(ep_tr, buffer, XHCI_TRB_DIR_IN, length);
    issue_status_stage_trb(ep_tr, XHCI_TRB_DIR_OUT);
    ring_device_doorbell(ws->db_regs, slot_id, 1);
    return wait_for_xhci_event(ws, XHCI_TRB_TRANSFER_EVENT, 100*MILLISEC, &event) == XHCI_EVENT_CC_SUCCESS;
}

static bool disable_xhci_slot(workspace_t *ws, int slot_id)
{
    xhci_trb_t event;

    enqueue_xhci_command(ws, XHCI_TRB_DISABLE_SLOT | slot_id << 24, 0, 0);
    ring_host_controller_doorbell(ws->db_regs);
    if (wait_for_xhci_event(ws, XHCI_TRB_COMMAND_COMPLETE, 10*MILLISEC, &event) != XHCI_EVENT_CC_SUCCESS) {
        return false;
    }
    return true;
}

static int initialise_device(workspace_t *ws, int port_idx, int slot_type)
{
    usb_setup_pkt_t setup_pkt;

    xhci_trb_t event;

    // Get the port speed.

    int device_speed = get_xhci_device_speed(ws->op_regs, port_idx);

    // Allocate a device slot and set up its output context.

    enqueue_xhci_command(ws, XHCI_TRB_ENABLE_SLOT | slot_type << 16, 0, 0);
    ring_host_controller_doorbell(ws->db_regs);
    if (wait_for_xhci_event(ws, XHCI_TRB_COMMAND_COMPLETE, 10*MILLISEC, &event) != XHCI_EVENT_CC_SUCCESS) {
        return 0;
    }
    int slot_id = event_slot_id(&event);

    write64(&ws->device_context_index[slot_id], ws->output_context_addr);

    // Prepare the input context for the ADDRESS_DEVICE command.

    xhci_ctrl_context_t *ctrl_context = (xhci_ctrl_context_t *)ws->input_context_addr;
    ctrl_context->add_context_flags = XHCI_CONTEXT_A0 | XHCI_CONTEXT_A1;

    xhci_slot_context_t *slot_context = (xhci_slot_context_t *)(ws->input_context_addr + ws->context_size);
    slot_context->root_hub_port_num = 1 + port_idx;
    slot_context->params1           = 1 << 27 | device_speed << 20;

    xhci_ep_context_t *ep_context = (xhci_ep_context_t *)(ws->input_context_addr + 2 * ws->context_size);
    ep_context->params2             = XHCI_EP_CONTROL << 3 | 3 << 1; // EP Type | CErr
    ep_context->max_burst_size      = 0;
    ep_context->max_packet_size     = default_max_packet_size(device_speed);
    ep_context->tr_dequeue_ptr      = ws->control_ep_tr_addr | 1;

    // Initialise the control endpoint transfer ring.

    ep_tr_t *ep_tr = (ep_tr_t *)ws->control_ep_tr_addr;
    ep_tr->enqueue_state = EP_TR_SIZE;  // cycle = 1, index = 0

    // Set the device address. For full speed devices we need to read the first 8 bytes of the device descriptor
    // to determine the maximum packet size the device supports and update the device context accordingly. For
    // compatibility with some older USB devices we need to read the first 8 bytes of the device descriptor before
    // actually setting the address. We can conveniently combine both these requirements.

    size_t fetch_length = sizeof(usb_device_desc_t);
    uint32_t command_flags = 0;
    if (device_speed < XHCI_HIGH_SPEED) {
        fetch_length  = 8;
        command_flags = XHCI_TRB_BSR;
    }
  set_address:
    enqueue_xhci_command(ws, XHCI_TRB_ADDRESS_DEVICE | command_flags | slot_id << 24, ws->input_context_addr, 0);
    ring_host_controller_doorbell(ws->db_regs);
    if (wait_for_xhci_event(ws, XHCI_TRB_COMMAND_COMPLETE, 100*MILLISEC, &event) != XHCI_EVENT_CC_SUCCESS) {
        goto disable_slot;
    }
    if (command_flags == 0) {
        usleep(2*MILLISEC);  // USB set address recovery time.
    }

    build_setup_packet(&setup_pkt, 0x80, 0x06, USB_DESC_DEVICE << 8, 0, fetch_length);
    if (!send_get_data_request(ws, slot_id, &setup_pkt, ws->data, fetch_length)
    ||  !valid_usb_device_descriptor(ws->data)) {
        goto disable_slot;
    }

    if (fetch_length == 8) {
        usb_device_desc_t *device = (usb_device_desc_t *)ws->data;
        if (!valid_usb_max_packet_size(device->max_packet_size, device_speed == XHCI_LOW_SPEED)) {
            goto disable_slot;
        }
        if (usb_init_options & USB_EXTRA_RESET) {
            reset_xhci_port(ws->op_regs, port_idx);
        }
        ep_context->max_packet_size = device->max_packet_size;
        ep_context->tr_dequeue_ptr += 3 * sizeof(xhci_trb_t);

        fetch_length  = sizeof(usb_device_desc_t);
        command_flags = 0;
        goto set_address;
    }

    // Fetch the first configuration descriptor and the associated interface and endpoint descriptors. Start by
    // requesting just the configuration descriptor. Then read the descriptor to determine whether we need to
    // fetch more data.

    fetch_length = sizeof(usb_config_desc_t);
  fetch_config_descriptor:
    build_setup_packet(&setup_pkt, 0x80, 0x06, USB_DESC_CONFIG << 8, 0, fetch_length);
    if (!send_get_data_request(ws, slot_id, &setup_pkt, ws->data, fetch_length)
    || !valid_usb_config_descriptor(ws->data)) {
        goto disable_slot;
    }
    usb_config_desc_t *config = (usb_config_desc_t *)ws->data;
    size_t total_length = min(config->total_length, WS_DATA_SIZE);
    if (total_length > fetch_length) {
        fetch_length = total_length;
        goto fetch_config_descriptor;
    }
    ws->data_length = total_length;

    return slot_id;

  disable_slot:
    if (disable_xhci_slot(ws, slot_id)) {
        write64(&ws->device_context_index[slot_id], 0);
    }
    return 0;
}

static bool configure_keyboard(workspace_t *ws, int slot_id, int kbd_idx, usb_ep_info_t *kbd)
{
    usb_setup_pkt_t setup_pkt;

    xhci_trb_t event;

    // Calculate the endpoint ID. This is used both to select an endpoint context and as a doorbell target.
    int ep_id = 2 * kbd->endpoint_num + 1;  // EP <N> IN

    // Fill in the lookup tables in the workspace.
    ws->kbd_slot_id[kbd_idx] = slot_id;
    ws->kbd_ep_id  [kbd_idx] = ep_id;

    // The input context has already been initialised, so we just need to change the values used by the
    // CONFIGURE_ENDPOINT command before issuing the command.

    xhci_ctrl_context_t *ctrl_context = (xhci_ctrl_context_t *)ws->input_context_addr;
    ctrl_context->add_context_flags = XHCI_CONTEXT_A0 | 1 << ep_id;

    xhci_slot_context_t *slot_context = (xhci_slot_context_t *)(ws->input_context_addr + ws->context_size);
    slot_context->params1           = ep_id << 27 | kbd->device_speed << 20;

    xhci_ep_context_t *ep_context = (xhci_ep_context_t *)(ws->input_context_addr + (1 + ep_id) * ws->context_size);
    ep_context->params1             = 0;
    ep_context->params2             = XHCI_EP_INTERRUPT_IN << 3 | 3 << 1; // EP Type | CErr
    ep_context->interval            = kbd->interval;
    ep_context->max_burst_size      = 0;
    ep_context->max_packet_size     = kbd->max_packet_size;
    ep_context->tr_dequeue_ptr      = (uintptr_t)(&ws->kbd_tr[kbd_idx]) | 1;
    ep_context->average_trb_length  = sizeof(hid_kbd_rpt_t);
    ep_context->max_esit_payload_l  = kbd->max_packet_size;
    ep_context->max_esit_payload_h  = 0;

    enqueue_xhci_command(ws, XHCI_TRB_CONFIGURE_ENDPOINT | slot_id << 24, ws->input_context_addr, 0);
    ring_host_controller_doorbell(ws->db_regs);
    if (wait_for_xhci_event(ws, XHCI_TRB_COMMAND_COMPLETE, 100*MILLISEC, &event) != XHCI_EVENT_CC_SUCCESS) {
        return false;
    }

    // Now configure the device itself.

    // Set the device configuration.
    build_setup_packet(&setup_pkt, 0x00, 0x09, 1, 0, 0);
    if (!send_setup_request(ws, slot_id, &setup_pkt)) {
        return false;
    }

    // Set the idle duration to infinite.
    build_setup_packet(&setup_pkt, 0x21, 0x0a, 0, kbd->interface_num, 0);
    if (!send_setup_request(ws, slot_id, &setup_pkt)) {
        return false;
    }

    // Select the boot protocol.
    build_setup_packet(&setup_pkt, 0x21, 0x0b, 0, kbd->interface_num, 0);
    if (!send_setup_request(ws, slot_id, &setup_pkt)) {
        return false;
    }

    return true;
}

static int identify_keyboard(workspace_t *ws, int slot_id, int ep_id)
{
    for (int kbd_idx = 0; kbd_idx < MAX_KEYBOARDS; kbd_idx++) {
        if (slot_id == ws->kbd_slot_id[kbd_idx] && ep_id == ws->kbd_ep_id[kbd_idx]) {
            return kbd_idx;
        }
    }
    return -1;
}

static bool set_heap_segment(void)
{
    // Use the largest 32-bit addressable physical memory segment for the heap.
    uintptr_t max_segment_size = 0;
    for (int i = 0; i < pm_map_size && pm_map[i].end <= PAGE_C(4,GB); i++) {
        uintptr_t segment_size = pm_map[i].end - pm_map[i].start;
        if (segment_size >= max_segment_size) {
            max_segment_size = segment_size;
            heap_segment = i;
        }
    }
    return max_segment_size > 0;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void *xhci_init(uintptr_t base_addr)
{
    if (heap_segment < 0) {
        if (!set_heap_segment()) return NULL;
    }
    uintptr_t heap_segment_end = pm_map[heap_segment].end;

    uint8_t port_type[XHCI_MAX_PORTS];

    memset(port_type, 0, sizeof(port_type));

    xhci_cap_regs_t *cap_regs = (xhci_cap_regs_t *)base_addr;

#ifdef QEMU_WORKAROUND
    xhci_cap_regs_t cap_regs_copy;
    memcpy32(&cap_regs_copy, cap_regs, sizeof(cap_regs_copy));
    cap_regs = &cap_regs_copy;
#endif

    // Walk the extra capabilities list.
    uintptr_t ext_cap_base = base_addr;
    uintptr_t ext_cap_offs = cap_regs->hcc_params1 >> 16;
    while (ext_cap_offs != 0) {
        ext_cap_base += ext_cap_offs * sizeof(uint32_t);
        xhci_ext_cap_t *ext_cap = (xhci_ext_cap_t *)ext_cap_base;

#ifdef QEMU_WORKAROUND
        xhci_ext_cap_t ext_cap_copy;
        memcpy32(&ext_cap_copy, ext_cap, sizeof(ext_cap_copy));
        ext_cap = &ext_cap_copy;
#endif
        if (ext_cap->id == XHCI_EXT_CAP_LEGACY_SUPPORT) {
            xhci_legacy_support_t *legacy_support = (xhci_legacy_support_t *)ext_cap_base;
            // Take ownership from the SMM if necessary.
            int timer = 1000;
            while (legacy_support->bios_owns & 0x1) {
                legacy_support->host_owns = 0x1;
                if (timer == 0) return NULL;
                usleep(1*MILLISEC);
                timer--;
            }
        }
        if (ext_cap->id == XHCI_EXT_CAP_SUPPORTED_PROTOCOL) {
            xhci_supported_protocol_t *protocol = (xhci_supported_protocol_t *)ext_cap_base;

#ifdef QEMU_WORKAROUND
            xhci_supported_protocol_t protocol_copy;
            memcpy32(&protocol_copy, protocol, sizeof(protocol_copy));
            protocol = &protocol_copy;
#endif
            // Record the ports covered by this protocol.
            uint8_t protocol_type = protocol->params2 & 0x1f;  // the Protocol Slot Type
            switch (protocol->revision_major) {
              case 0x02:
                protocol_type |= PORT_TYPE_USB2;
                break;
              case 0x03:
                protocol_type |= PORT_TYPE_USB3;
                break;
            }
#if 0
            print_usb_info("protocol revision %i.%i type %i offset %i count %i",
                           protocol->revision_major, protocol->revision_minor / 16,
                           protocol_type, protocol->port_offset, protocol->port_count);
#endif
            for (int i = 0; i < protocol->port_count; i++) {
		int port_idx = protocol->port_offset + i - 1;
		if (port_idx >= 0 && port_idx < XHCI_MAX_PORTS) {
                    port_type[port_idx] = protocol_type;
                }
            }
        }
        ext_cap_offs = ext_cap->next_offset;
    }

    xhci_op_regs_t *op_regs = (xhci_op_regs_t *)(base_addr + cap_regs->cap_length);
    xhci_rt_regs_t *rt_regs = (xhci_rt_regs_t *)(base_addr + cap_regs->rts_offset);
    xhci_db_reg_t  *db_regs = (xhci_db_reg_t  *)(base_addr + cap_regs->db_offset);

    // Ensure the controller is halted and then reset it.
    if (!halt_host_controller(op_regs)) return NULL;

    if (!reset_host_controller(op_regs)) return NULL;

    // Record the controller page size.
    uintptr_t xhci_page_size = (read32(&op_regs->page_size) & 0xffff) << 12;
    uintptr_t xhci_page_mask = xhci_page_size - 1;

    // Find the maximum number of device slots the controller supports.
    int max_slots = cap_regs->hcs_params1 & 0xff;

    // Find the number of scratchpad buffers the controller wants.
    uintptr_t num_scratchpad_buffers = ((cap_regs->hcs_params2 >> 21) & 0x1f) << 5
                                     | ((cap_regs->hcs_params2 >> 27) & 0x1f);

    // Allocate and clear the scratchpad memory on the heap. This must be aligned to the controller page size.
    // TODO: check for heap overflow.
    uintptr_t scratchpad_size = num_scratchpad_buffers * xhci_page_size;
    pm_map[heap_segment].end -= num_pages(scratchpad_size);
    pm_map[heap_segment].end &= ~(xhci_page_mask >> PAGE_SHIFT);
    uintptr_t scratchpad_addr = pm_map[heap_segment].end << PAGE_SHIFT;

    memset((void *)scratchpad_addr, 0, scratchpad_size);

    // Allocate and initialise the device context base address and scratchpad buffer arrays on the heap.
    // Both need to be aligned on a 64 byte boundary.
    // TODO: check for heap overflow.
    uintptr_t device_context_index_size = (1 + max_slots) * sizeof(uint64_t);
    uintptr_t scratchpad_buffer_index_offs = round_up(device_context_index_size, 64);
    uintptr_t scratchpad_buffer_index_size = num_scratchpad_buffers * sizeof(uint64_t);
    pm_map[heap_segment].end -= num_pages(scratchpad_buffer_index_offs + scratchpad_buffer_index_size);
    uintptr_t device_context_index_addr = pm_map[heap_segment].end << PAGE_SHIFT;

    memset((void *)device_context_index_addr, 0, device_context_index_size);

    uint64_t *device_context_index = (uint64_t *)device_context_index_addr;
    if (num_scratchpad_buffers > 0) {
        uintptr_t scratchpad_buffer_index_addr = device_context_index_addr + scratchpad_buffer_index_offs;
        device_context_index[0] = scratchpad_buffer_index_addr;
        uint64_t *scratchpad_buffer_index = (uint64_t *)scratchpad_buffer_index_addr;
        for (uintptr_t i = 0; i < num_scratchpad_buffers; i++) {
            scratchpad_buffer_index[i] = scratchpad_addr + i * xhci_page_size;
        }
    }

    // Allocate and initialise a workspace for this controller. This needs to be permanently mapped into virtual
    // memory, so allocate it in the first segment.
    // TODO: check for segment overflow.
    pm_map[0].end -= num_pages(sizeof(workspace_t));
    uintptr_t workspace_addr = pm_map[0].end << PAGE_SHIFT;
    workspace_t *ws = (workspace_t *)workspace_addr;

    memset(ws, 0, sizeof(workspace_t));

    ws->op_regs = op_regs;
    ws->rt_regs = rt_regs;
    ws->db_regs = db_regs;

    ws->device_context_index = device_context_index;

    ws->context_size = cap_regs->hcc_params1 & 0x4 ? 64 : 32;

    ws->cr_enqueue_state = WS_CR_SIZE;  // cycle = 1, index = 0
    ws->er_dequeue_state = WS_ER_SIZE;  // cycle = 1, index = 0

    // Initialise the ERST for the primary interrupter. We only use the first segment.
    ws->erst[0].segment_addr = (uintptr_t)(&ws->er);
    ws->erst[0].segment_size = WS_ER_SIZE;

    write64(&rt_regs->ir[0].erdp,      (uintptr_t)(&ws->er));
    write32(&rt_regs->ir[0].erst_size, 1);
    write64(&rt_regs->ir[0].erst_addr, (uintptr_t)(&ws->erst));

    // Initialise and start the controller.
    write64(&op_regs->cr_control, (read64(&op_regs->cr_control) & 0x30) | (uintptr_t)(&ws->cr) | 0x1);
    write64(&op_regs->dcbaap,     device_context_index_addr);
    write32(&op_regs->config,     (read32(&op_regs->config) & 0xfffffc00) | max_slots);
    if (!start_host_controller(op_regs)) {
        pm_map[heap_segment].end = heap_segment_end;
        return NULL;
    }
    usleep(100*MILLISEC);  // USB maximum device attach time.

    // Scan the ports, looking for keyboards.
    usb_ep_info_t keyboard_info[MAX_KEYBOARDS];
    int num_keyboards = 0;
    int num_devices = 0;
    int num_ports = cap_regs->hcs_params1 & 0xff;
    for (int port_idx = 0; port_idx < num_ports; port_idx++) {
        if (num_keyboards >= MAX_KEYBOARDS) continue;

        // We only expect to find keyboards on USB2 ports.
        if (~port_type[port_idx] & PORT_TYPE_USB2) continue;

        // Check if anything is connected to this port.
        uint32_t port_status = read32(&op_regs->port_regs[port_idx].sc);
        if (~port_status & XHCI_PORT_SC_CCS) continue;
        num_devices++;

        // Reset the port (USB2 only).
        reset_xhci_port(op_regs, port_idx);

        // Wait for the device to be enabled.
        if (!wait_until_set(&op_regs->port_regs[port_idx].sc, XHCI_PORT_SC_PED, 500*MILLISEC)) continue;

        // Allocate and initialise a private workspace for this device.
        // TODO: check for heap overflow.
        const size_t device_ws_size = XHCI_MAX_OP_CONTEXT_SIZE + sizeof(ep_tr_t);
        pm_map[heap_segment].end -= num_pages(device_ws_size);
        uintptr_t device_workspace_addr = pm_map[heap_segment].end << PAGE_SHIFT;
        ws->output_context_addr = device_workspace_addr;
        ws->control_ep_tr_addr  = device_workspace_addr + XHCI_MAX_OP_CONTEXT_SIZE;

        memset((void *)device_workspace_addr, 0, device_ws_size);

        // Temporarily allocate and initialise the input context data structure on the heap.
        // As we only need this for the lifetime of this function, there's no need to adjust pm_map.
        ws->input_context_addr = device_workspace_addr - XHCI_MAX_IP_CONTEXT_SIZE;

        memset((void *)ws->input_context_addr, 0, XHCI_MAX_IP_CONTEXT_SIZE);

        // Retrieve the protocol slot type.
        int slot_type = port_type[port_idx] & PORT_TYPE_PST_MASK;

        // Initialise the device. If successful, this leaves a set of configuration descriptors in the workspace
        // data buffer.
        int slot_id = initialise_device(ws, port_idx, slot_type);
        if (slot_id == 0) {
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

            kbd->device_speed = get_xhci_device_speed(op_regs, port_idx);
            kbd->interval     = xhci_ep_interval(kbd->interval, kbd->device_speed);
            configure_keyboard(ws, slot_id, num_keyboards + kbd_idx, kbd);

            print_usb_info(" Keyboard found on port %i interface %i endpoint %i",
                           1 + port_idx, kbd->interface_num, kbd->endpoint_num);
        }

        if (new_keyboards > 0) {
            num_keyboards += new_keyboards;
            continue;
        }

        // If we didn't find any keyboard interfaces, we can free the allocated resources and disable the port.

        if (disable_xhci_slot(ws, slot_id)) {
            write64(&ws->device_context_index[slot_id], 0);
        }

      disable_port:
        disable_xhci_port(op_regs, port_idx);
        pm_map[heap_segment].end += num_pages(device_ws_size);
    }

    print_usb_info("  Found %i device%s, %i keyboard%s",
                   num_devices,   num_devices   != 1 ? "s" : "",
                   num_keyboards, num_keyboards != 1 ? "s" : "");

    if (num_keyboards == 0) {
        // Halt the host controller.
        (void)halt_host_controller(op_regs);

        // Free the pages we allocated in segment 0.
        pm_map[0].end += num_pages(sizeof(workspace_t));

        // Free the pages we allocated in the heap segment.
        pm_map[heap_segment].end = heap_segment_end;

        return NULL;
    }

    // Initialise the interrupt TRB ring for each keyboard interface.
    for (int kbd_idx = 0; kbd_idx < num_keyboards; kbd_idx++) {
        ep_tr_t *kbd_tr = &ws->kbd_tr[kbd_idx];
        kbd_tr->enqueue_state = EP_TR_SIZE;  // cycle = 1, index = 0

        hid_kbd_rpt_t *kbd_rpt = &ws->kbd_rpt[kbd_idx];
        issue_normal_trb(kbd_tr, kbd_rpt, XHCI_TRB_DIR_IN, sizeof(hid_kbd_rpt_t));
        ring_device_doorbell(ws->db_regs, ws->kbd_slot_id[kbd_idx], ws->kbd_ep_id[kbd_idx]);
    }

    return ws;
}

uint8_t xhci_get_keycode(void *workspace)
{
    workspace_t *ws = (workspace_t *)workspace;

    xhci_trb_t event;

    while (get_xhci_event(ws, &event)) {
        if (event_type(&event) != XHCI_TRB_TRANSFER_EVENT || event_cc(&event) != XHCI_EVENT_CC_SUCCESS) continue;

        int kbd_idx = identify_keyboard(ws, event_slot_id(&event), event_ep_id(&event));
        if (kbd_idx < 0) continue;

        hid_kbd_rpt_t *kbd_rpt = &ws->kbd_rpt[kbd_idx];
        uint8_t keycode = kbd_rpt->key_code[0];
        if (keycode != 0) {
            int kc_index_i = ws->kc_index_i;
            int kc_index_n = (kc_index_i + 1) % WS_KC_BUFFER_SIZE;
            if (kc_index_n != ws->kc_index_o) {
                ws->kc_buffer[kc_index_i] = keycode;
                ws->kc_index_i = kc_index_n;
            }
        }

        ep_tr_t *kbd_tr = &ws->kbd_tr[kbd_idx];
        issue_normal_trb(kbd_tr, kbd_rpt, XHCI_TRB_DIR_IN, sizeof(hid_kbd_rpt_t));
        ring_device_doorbell(ws->db_regs, ws->kbd_slot_id[kbd_idx], ws->kbd_ep_id[kbd_idx]);
    }

    int kc_index_o = ws->kc_index_o;
    if (kc_index_o != ws->kc_index_i) {
        ws->kc_index_o = (kc_index_o + 1) % WS_KC_BUFFER_SIZE;
        return ws->kc_buffer[kc_index_o];
    }

    return 0;
}
