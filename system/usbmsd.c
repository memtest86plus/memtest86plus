// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester.

#include <stdbool.h>
#include <stdint.h>

#include "string.h"
#include "unistd.h"

#include "usbmsd.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define CBW_SIGNATURE       0x43425355
#define CSW_SIGNATURE       0x53425355

#define CBW_FLAG_DATA_IN    0x80
#define CBW_FLAG_DATA_OUT   0x00

// Bulk-Only Mass Storage Reset class request.
#define BOT_RESET           0xFF

// CSW status values.
#define CSW_STATUS_PASSED       0
#define CSW_STATUS_FAILED       1
#define CSW_STATUS_PHASE_ERR    2

// SCSI command opcodes.
#define SCSI_TEST_UNIT_READY    0x00
#define SCSI_REQUEST_SENSE      0x03
#define SCSI_INQUIRY            0x12
#define SCSI_READ_CAPACITY_10   0x25
#define SCSI_READ_10            0x28
#define SCSI_WRITE_10           0x2A
#define SCSI_READ_16            0x88
#define SCSI_WRITE_16           0x8A
#define SCSI_READ_CAPACITY_16   0x9E
#define SCSI_SAI_READ_CAPACITY_16   0x10    // Service action for SCSI_READ_CAPACITY_16

// LBA returned by READ CAPACITY (10) when the disk is too large to address with 32 bits.
#define READ_CAP_10_OVERFLOW    0xFFFFFFFFu

#define MILLISEC                1000    // in microseconds

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef struct __attribute__((packed)) {
    uint32_t    signature;
    uint32_t    tag;
    uint32_t    data_transfer_length;
    uint8_t     flags;
    uint8_t     lun;
    uint8_t     cb_length;
    uint8_t     cb[16];
} usb_cbw_t;

typedef struct __attribute__((packed)) {
    uint32_t    signature;
    uint32_t    tag;
    uint32_t    data_residue;
    uint8_t     status;
} usb_csw_t;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

// Clears a halted bulk endpoint on both the device and the controller side.
static bool msd_clear_stall(usb_msd_t *msd, const usb_ep_t *ep, bool is_in)
{
    const usb_hcd_t *hcd = msd->hcd;

    usb_setup_pkt_t setup_pkt;
    build_setup_packet(&setup_pkt, USB_REQ_TO_ENDPOINT, USB_CLR_FEATURE,
                       USB_ENDPOINT_HALT, ep->endpoint_num | (is_in ? 0x80 : 0), 0);
    if (!hcd->methods->setup_request(hcd, &msd->ep0, &setup_pkt)) {
        return false;
    }

    if (hcd->methods->reset_bulk_ep != NULL) {
        int ep_id = 2 * ep->endpoint_num + (is_in ? 1 : 0);
        return hcd->methods->reset_bulk_ep(hcd, ep, ep_id);
    }
    return true;
}

// BOT Reset Recovery (BOT spec 5.3.4): class reset, then clear both bulk endpoints.
static bool msd_reset_recovery(usb_msd_t *msd)
{
    const usb_hcd_t *hcd = msd->hcd;

    usb_setup_pkt_t setup_pkt;
    build_setup_packet(&setup_pkt, USB_REQ_TO_INTERFACE | USB_REQ_CLASS, BOT_RESET,
                       0, msd->ep_in.interface_num, 0);
    if (!hcd->methods->setup_request(hcd, &msd->ep0, &setup_pkt)) {
        return false;
    }
    usleep(10 * MILLISEC);

    bool ok = msd_clear_stall(msd, &msd->ep_in, true);
    return msd_clear_stall(msd, &msd->ep_out, false) && ok;
}

static bool msd_bot_command(usb_msd_t *msd, const uint8_t *cdb, int cdb_len,
                            void *data, uint32_t data_len, bool data_in)
{
    const usb_hcd_t *hcd = msd->hcd;

    // Build Command Block Wrapper. cb[] not in the initializer is zero-padded.
    usb_cbw_t cbw = {
        .signature            = CBW_SIGNATURE,
        .tag                  = msd->tag++,
        .data_transfer_length = data_len,
        .flags                = data_in ? CBW_FLAG_DATA_IN : CBW_FLAG_DATA_OUT,
        .lun                  = 0,
        .cb_length            = cdb_len,
    };
    memcpy(cbw.cb, cdb, cdb_len);

    // Send CBW via bulk OUT. A failure here means the transport is broken.
    if (!hcd->methods->bulk_transfer(hcd, &msd->ep_out, &cbw, sizeof(cbw), true)) {
        msd_reset_recovery(msd);
        return false;
    }

    // Data phase (if any). On failure (usually a STALL on a rejected command),
    // clear the endpoint so the CSW can still be read (BOT spec 6.7.2/6.7.3).
    bool data_ok = true;
    if (data_len > 0 && data != NULL) {
        const usb_ep_t *ep = data_in ? &msd->ep_in : &msd->ep_out;
        data_ok = hcd->methods->bulk_transfer(hcd, ep, data, data_len, !data_in);
        if (!data_ok) {
            msd_clear_stall(msd, ep, data_in);
        }
    }

    // Receive CSW via bulk IN; retry once after clearing a stalled IN endpoint.
    usb_csw_t csw;
    if (!hcd->methods->bulk_transfer(hcd, &msd->ep_in, &csw, sizeof(csw), false)) {
        if (!msd_clear_stall(msd, &msd->ep_in, true)
        ||  !hcd->methods->bulk_transfer(hcd, &msd->ep_in, &csw, sizeof(csw), false)) {
            msd_reset_recovery(msd);
            return false;
        }
    }

    // Validate the CSW; a bad CSW or a phase error requires a full reset recovery.
    if (csw.signature != CSW_SIGNATURE || csw.tag != cbw.tag || csw.status == CSW_STATUS_PHASE_ERR) {
        msd_reset_recovery(msd);
        return false;
    }

    return data_ok && csw.status == CSW_STATUS_PASSED;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

// The block size is device-reported and sizes host buffers; only accept sane values.
static bool valid_block_size(uint32_t size)
{
    return size == 512 || size == 1024 || size == 2048 || size == 4096;
}

static bool read_capacity_16(usb_msd_t *msd)
{
    uint8_t cdb[16] = {
        SCSI_READ_CAPACITY_16,
        SCSI_SAI_READ_CAPACITY_16,
        0, 0, 0, 0, 0, 0, 0, 0,         // 8-byte LBA (0 for service action 0x10)
        0, 0, 0, 32,                    // allocation length = 32
        0, 0
    };
    uint8_t cap_data[32];
    if (!msd_bot_command(msd, cdb, 16, cap_data, sizeof(cap_data), true)) {
        return false;
    }

    uint64_t last_lba = 0;
    for (int i = 0; i < 8; i++) {
        last_lba = (last_lba << 8) | cap_data[i];
    }
    msd->block_count = last_lba + 1;

    msd->block_size = ((uint32_t)cap_data[8]  << 24) | ((uint32_t)cap_data[9]  << 16)
                    | ((uint32_t)cap_data[10] << 8)  | (uint32_t)cap_data[11];

    return valid_block_size(msd->block_size);
}

bool msd_init(usb_msd_t *msd)
{
    msd->use_16 = false;

    // TEST UNIT READY — retry a few times since the device may need time to spin up.
    uint8_t cdb_tur[6] = { SCSI_TEST_UNIT_READY };
    for (int retry = 0; retry < 5; retry++) {
        if (msd_bot_command(msd, cdb_tur, 6, NULL, 0, false)) {
            break;
        }
        usleep(500 * MILLISEC);
        if (retry == 4) return false;
    }

    // READ CAPACITY (10) — returns 8 bytes: last LBA (4 bytes BE) + block size (4 bytes BE).
    uint8_t cdb_cap[10] = { SCSI_READ_CAPACITY_10 };

    uint8_t cap_data[8];
    if (!msd_bot_command(msd, cdb_cap, 10, cap_data, 8, true)) {
        // Some larger drives reject 10-byte commands; try the 16-byte variant.
        if (!read_capacity_16(msd)) return false;
        msd->use_16 = true;
        return true;
    }

    uint32_t last_lba_10 = ((uint32_t)cap_data[0] << 24) | ((uint32_t)cap_data[1] << 16)
                         | ((uint32_t)cap_data[2] << 8)  | (uint32_t)cap_data[3];

    msd->block_size = ((uint32_t)cap_data[4] << 24) | ((uint32_t)cap_data[5] << 16)
                     | ((uint32_t)cap_data[6] << 8)  | (uint32_t)cap_data[7];

    if (!valid_block_size(msd->block_size)) return false;

    // Drive >= 2 TiB: last LBA saturates to 0xFFFFFFFF; query READ CAPACITY (16) for the real value.
    if (last_lba_10 == READ_CAP_10_OVERFLOW) {
        if (!read_capacity_16(msd)) return false;
        msd->use_16 = true;
    } else {
        msd->block_count = (uint64_t)last_lba_10 + 1;
    }

    return true;
}

bool msd_read_sectors(usb_msd_t *msd, uint64_t lba, uint32_t count, void *buffer)
{
    if (msd->use_16 || (lba >> 32) != 0) {
        uint8_t cdb[16] = {
            SCSI_READ_16, 0,
            (uint8_t)(lba >> 56), (uint8_t)(lba >> 48), (uint8_t)(lba >> 40), (uint8_t)(lba >> 32),
            (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8),  (uint8_t)lba,
            (uint8_t)(count >> 24), (uint8_t)(count >> 16), (uint8_t)(count >> 8), (uint8_t)count,
            0, 0
        };
        return msd_bot_command(msd, cdb, 16, buffer, count * msd->block_size, true);
    }

    uint8_t cdb[10] = {
        SCSI_READ_10, 0,
        (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba,
        0,
        (uint8_t)(count >> 8), (uint8_t)count, 0
    };
    return msd_bot_command(msd, cdb, 10, buffer, count * msd->block_size, true);
}

bool msd_write_sectors(usb_msd_t *msd, uint64_t lba, uint32_t count, const void *buffer)
{
    if (msd->use_16 || (lba >> 32) != 0) {
        uint8_t cdb[16] = {
            SCSI_WRITE_16, 0,
            (uint8_t)(lba >> 56), (uint8_t)(lba >> 48), (uint8_t)(lba >> 40), (uint8_t)(lba >> 32),
            (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8),  (uint8_t)lba,
            (uint8_t)(count >> 24), (uint8_t)(count >> 16), (uint8_t)(count >> 8), (uint8_t)count,
            0, 0
        };
        return msd_bot_command(msd, cdb, 16, (void *)buffer, count * msd->block_size, false);
    }

    uint8_t cdb[10] = {
        SCSI_WRITE_10, 0,
        (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba,
        0,
        (uint8_t)(count >> 8), (uint8_t)count, 0
    };
    return msd_bot_command(msd, cdb, 10, (void *)buffer, count * msd->block_size, false);
}
