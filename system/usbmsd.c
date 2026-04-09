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

// SCSI command opcodes.
#define SCSI_TEST_UNIT_READY    0x00
#define SCSI_REQUEST_SENSE      0x03
#define SCSI_INQUIRY            0x12
#define SCSI_READ_CAPACITY_10   0x25
#define SCSI_READ_10            0x28
#define SCSI_WRITE_10           0x2A

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

static bool msd_bot_command(usb_msd_t *msd, const uint8_t *cdb, int cdb_len,
                            void *data, uint32_t data_len, bool data_in)
{
    const usb_hcd_t *hcd = msd->hcd;

    // Build Command Block Wrapper.
    usb_cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.signature            = CBW_SIGNATURE;
    cbw.tag                  = msd->tag++;
    cbw.data_transfer_length = data_len;
    cbw.flags                = data_in ? CBW_FLAG_DATA_IN : CBW_FLAG_DATA_OUT;
    cbw.lun                  = 0;
    cbw.cb_length            = cdb_len;
    memcpy(cbw.cb, cdb, cdb_len);

    // Send CBW via bulk OUT.
    if (!hcd->methods->bulk_transfer(hcd, &msd->ep_out, &cbw, sizeof(cbw), true)) {
        return false;
    }

    // Data phase (if any).
    if (data_len > 0 && data != NULL) {
        if (data_in) {
            if (!hcd->methods->bulk_transfer(hcd, &msd->ep_in, data, data_len, false)) {
                return false;
            }
        } else {
            if (!hcd->methods->bulk_transfer(hcd, &msd->ep_out, data, data_len, true)) {
                return false;
            }
        }
    }

    // Receive CSW via bulk IN.
    usb_csw_t csw;
    memset(&csw, 0, sizeof(csw));
    if (!hcd->methods->bulk_transfer(hcd, &msd->ep_in, &csw, sizeof(csw), false)) {
        return false;
    }

    // Validate CSW.
    if (csw.signature != CSW_SIGNATURE) return false;
    if (csw.tag != cbw.tag) return false;
    if (csw.status != 0) return false;

    return true;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

bool msd_init(usb_msd_t *msd)
{
    uint8_t cdb[10];

    // TEST UNIT READY — retry a few times since the device may need time to spin up.
    memset(cdb, 0, 6);
    cdb[0] = SCSI_TEST_UNIT_READY;
    for (int retry = 0; retry < 5; retry++) {
        if (msd_bot_command(msd, cdb, 6, NULL, 0, false)) {
            break;
        }
        usleep(500 * MILLISEC);
        if (retry == 4) return false;
    }

    // READ CAPACITY (10) — returns 8 bytes: last LBA (4 bytes BE) + block size (4 bytes BE).
    memset(cdb, 0, 10);
    cdb[0] = SCSI_READ_CAPACITY_10;

    uint8_t cap_data[8];
    if (!msd_bot_command(msd, cdb, 10, cap_data, 8, true)) {
        return false;
    }

    msd->block_count = ((uint32_t)cap_data[0] << 24) | ((uint32_t)cap_data[1] << 16)
                      | ((uint32_t)cap_data[2] << 8)  | (uint32_t)cap_data[3];
    msd->block_count += 1; // READ CAPACITY returns last LBA, not count.

    msd->block_size = ((uint32_t)cap_data[4] << 24) | ((uint32_t)cap_data[5] << 16)
                     | ((uint32_t)cap_data[6] << 8)  | (uint32_t)cap_data[7];

    if (msd->block_size == 0) return false;

    return true;
}

bool msd_read_sectors(usb_msd_t *msd, uint32_t lba, uint32_t count, void *buffer)
{
    uint8_t cdb[10];
    memset(cdb, 0, 10);
    cdb[0] = SCSI_READ_10;
    cdb[2] = (lba >> 24) & 0xFF;
    cdb[3] = (lba >> 16) & 0xFF;
    cdb[4] = (lba >> 8)  & 0xFF;
    cdb[5] =  lba        & 0xFF;
    cdb[7] = (count >> 8) & 0xFF;
    cdb[8] =  count       & 0xFF;

    return msd_bot_command(msd, cdb, 10, buffer, count * msd->block_size, true);
}

bool msd_write_sectors(usb_msd_t *msd, uint32_t lba, uint32_t count, const void *buffer)
{
    uint8_t cdb[10];
    memset(cdb, 0, 10);
    cdb[0] = SCSI_WRITE_10;
    cdb[2] = (lba >> 24) & 0xFF;
    cdb[3] = (lba >> 16) & 0xFF;
    cdb[4] = (lba >> 8)  & 0xFF;
    cdb[5] =  lba        & 0xFF;
    cdb[7] = (count >> 8) & 0xFF;
    cdb[8] =  count       & 0xFF;

    return msd_bot_command(msd, cdb, 10, (void *)buffer, count * msd->block_size, false);
}
