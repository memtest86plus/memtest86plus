// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.

#include "display.h"

#include "io.h"
#include "string.h"

#include "cpuid.h"
#include "memrw.h"
#include "vmem.h"
#include "smbus.h"
#include "i2c.h"
#include "spd.h"

i2c_param i2c_info;

uint8_t max_mc_nu = 0;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static bool determined_i2c_address(void)
{
    uint8_t i;

    if (strstr(cpuid_info.vendor_id.str, "Loongson")) {
        if (strstr(cpuid_info.brand_id.str, "3A5") ||
              strstr(cpuid_info.brand_id.str, "3A6")) {
            max_mc_nu = 2;
            for(i = 0; i < max_mc_nu; i++) {
                i2c_info.i2c_mc[i].i2c_base = (uint8_t *)map_region(LOONGSON_I2C0_ADDR, 0x8, true);
                i2c_info.i2c_mc[i].devid.slot0_addr = i * 2 + 0x0;
                i2c_info.i2c_mc[i].devid.slot1_addr = i * 2 + 0x1;
           }
        } else if (strstr((const char *)cpuid_info.brand_id.str, "3C5")) {
            max_mc_nu = 4;
            for(i = 0; i < max_mc_nu; i++) {
                i2c_info.i2c_mc[i].i2c_base = (uint8_t *)map_region(LOONGSON_I2C0_ADDR, 0x8, true);
            }
            i2c_info.i2c_mc[0].devid.slot0_addr = 0x4;
            i2c_info.i2c_mc[0].devid.slot1_addr = 0x5;

            i2c_info.i2c_mc[1].devid.slot0_addr = 0x0;
            i2c_info.i2c_mc[1].devid.slot1_addr = 0x1;

            i2c_info.i2c_mc[2].devid.slot0_addr = 0x2;
            i2c_info.i2c_mc[2].devid.slot1_addr = 0x3;

            i2c_info.i2c_mc[3].devid.slot0_addr = 0x6;
            i2c_info.i2c_mc[3].devid.slot1_addr = 0x7;
        } else if (strstr(cpuid_info.brand_id.str, "3D5")) {
            max_mc_nu = 4;
            i2c_info.i2c_mc[0].i2c_base = (uint8_t *)map_region(LOONGSON_I2C1_ADDR, 0x8, true);
            i2c_info.i2c_mc[1].i2c_base = (uint8_t *)map_region(LOONGSON_I2C1_ADDR, 0x8, true);

            i2c_info.i2c_mc[2].i2c_base = (uint8_t *)map_region(LOONGSON_I2C2_ADDR, 0x8, true);
            i2c_info.i2c_mc[3].i2c_base = (uint8_t *)map_region(LOONGSON_I2C2_ADDR, 0x8, true);
            for(i = 0; i < max_mc_nu; i++) {
                i2c_info.i2c_mc[i].devid.slot0_addr = (i % 2 * 2 + 0x0);
                i2c_info.i2c_mc[i].devid.slot1_addr = (i % 2 * 2 + 0x1);
            }
        }
        return true;
    } else {
        return false;
    }
    return false;
}

static uint8_t i2c_read_byte(uint8_t *base, uint8_t dev_addr, uint16_t offset)
{
    uint8_t buf;
    volatile uint8_t spd_adr = (uint8_t)(offset & 0xff);
    /* if addr less than 0x100 set to page0 as default status */
    if (offset & 0xff) {
        /*set page to 0*/
        write8(base + TXR_REG, SPA0);
        /*send device select code*/
        write8(base + CR_REG, CR_START | CR_WRITE);
        /* wait send finished */
        while (read8(base + SR_REG) & SR_TIP);
        /* i2c_stop */
        write8(base + CR_REG, CR_STOP);
        while (read8(base + SR_REG) & SR_BUSY);
        /*set page to 0 end*/
    }

    /* if addr large than 0xff set to page1 */
    if (offset & 0xff00) {
        /*set page to 1*/
        write8(base + TXR_REG, SPA1);
        /*send device select code*/
        write8(base + CR_REG, CR_START | CR_WRITE);
        /* wait send finished */
        while(read8(base + SR_REG) & SR_TIP);
        /* i2c_stop */
        write8(base + CR_REG, CR_STOP);
        while(read8(base + SR_REG) & SR_BUSY);
        /*set page to 1 end*/
    }

    /* load device address */
    write8(base + TXR_REG, dev_addr & 0xfe);
    /* send start frame */
    write8(base + CR_REG, CR_START | CR_WRITE);
    /* wait send finished */
    while (read8(base + SR_REG) & SR_TIP);

    /* load data to be send */
    write8(base + TXR_REG, spd_adr);
    /* send data frame */
    write8(base + CR_REG, CR_WRITE);
    /* wait send finished */
    while (read8(base + SR_REG) & SR_TIP);

    /* load device address */
    write8(base + TXR_REG, dev_addr | 0x1);
    /* send start frame */
    write8(base + CR_REG, CR_START | CR_WRITE);
    /* wait send finished */
    while (read8(base + SR_REG) & SR_TIP);

    /* receive data to fifo */
    write8(base + CR_REG, CR_READ | CR_ACK);
    while (read8(base + SR_REG) & SR_TIP);
    /* read data from fifo */
    buf = read8(base + RXR_REG);

    /* free i2c bus */
    write8(base + CR_REG, CR_STOP);
    while (read8(base + SR_REG) & SR_BUSY);

    /* if addr large than 0xff set to page0 as default status */
    if (offset & 0xff00) {
        /*set page to 0*/
        write8(base + TXR_REG, SPA0);
        /*send device select code*/
        write8(base + CR_REG, CR_START | CR_WRITE);
        /* wait send finished */
        while (read8(base + SR_REG) & SR_TIP);
        /* i2c_stop */
        write8(base + CR_REG, CR_STOP);
        while (read8(base + SR_REG) & SR_BUSY);
        /*set page to 1 end*/
    }
    return buf;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

uint8_t get_spd(uint8_t slot_idx, uint16_t spd_adr)
{
    uint8_t device_id, mc;

    mc = slot_idx / 2;

    if (strstr(cpuid_info.vendor_id.str, "Loongson")) {
        device_id = 0xa1;
    }

    device_id |= (((slot_idx % 2) ?
                    i2c_info.i2c_mc[mc].devid.slot1_addr :
                    i2c_info.i2c_mc[mc].devid.slot0_addr) << 1);
    return i2c_read_byte(i2c_info.i2c_mc[mc].i2c_base, device_id, spd_adr);
}

void print_spd_startup_info(void)
{
    uint8_t spdidx = 0, spd_line_idx = 0;

    spd_info curspd;
    ram.freq = 0;
    curspd.isValid = false;

    if (!determined_i2c_address()) {
        return;
    }

    for (spdidx = 0; spdidx < max_mc_nu * 2; spdidx++) {
        parse_spd(&curspd, spdidx);

        if (!curspd.isValid)
            continue;

        if (spd_line_idx == 0) {
            prints(ROW_SPD-2, 0, "Memory SPD Information");
            prints(ROW_SPD-1, 0, "----------------------");
        }

        print_spdi(curspd, ROW_SPD+spd_line_idx);
        spd_line_idx++;
    }
}
