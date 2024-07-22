/**
 * SPDX-License-Identifier: GPL-2.0
 *
 * \file
 *
 * Provides functions for reading SPD via I2C
 * Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.
 *
 */

#ifndef _I2C_H_
#define _I2C_H_

#define PRER_LO_REG 0x0
#define PRER_HI_REG 0x1
#define CTR_REG     0x2
#define TXR_REG     0x3
#define RXR_REG     0x3
#define CR_REG      0x4
#define SR_REG      0x4

#define CR_START  0x80
#define CR_STOP   0x40
#define CR_READ   0x20
#define CR_WRITE  0x10
#define CR_ACK    0x8
#define CR_IACK   0x1

#define SR_NOACK  0x80
#define SR_BUSY   0x40
#define SR_AL     0x20
#define SR_TIP    0x2
#define SR_IF     0x1

#define SWP0 0x62
#define SWP1 0x68
#define SWP2 0x6a
#define SWP3 0x60
#define CWP  0x66
#define RPS0 0x63
#define RPS1 0x69
#define RPS2 0x6b
#define RPS3 0x61
#define SPA0 0x6c
#define SPA1 0x6e
#define RPA  0x6d

#define LOONGSON_I2C0_ADDR 0x1fe00120
#define LOONGSON_I2C1_ADDR 0x1fe00130
#define LOONGSON_I2C2_ADDR 0x1fe00138

typedef union {
    struct {
         uint32_t slot0_addr : 8;
         uint32_t slot1_addr : 8;
    };
    uint32_t devid;
} dev_id;

typedef struct {
    uint8_t   *i2c_base;
    dev_id    devid;;
} i2c_slot_param;

typedef struct {
    i2c_slot_param  i2c_mc[4];
} i2c_param;

#endif // I2C_H
