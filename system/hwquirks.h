// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2022 Sam Demeulemeester
#ifndef _QUIRK_H_
#define _QUIRK_H_
/**
 *
 * Provides support for hardware quirks
 *
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define QUIRK_TYPE_NONE     0b00000000
#define QUIRK_TYPE_USB      0b00000001
#define QUIRK_TYPE_SMP      0b00000010
#define QUIRK_TYPE_SMBIOS   0b00000100
#define QUIRK_TYPE_SMBUS    0b00001000
#define QUIRK_TYPE_TIMER    0b00010000

typedef enum {
    QUIRK_NONE,
    QUIRK_TUSL2
} quirk_id_t;

typedef struct {
    quirk_id_t   id;
    uint8_t      type;
    uint16_t     root_vid;
    uint16_t     root_did;
    void (*process)(void);
} quirk_t;

extern quirk_t quirk;

void quirks_init(void);

#endif /* _QUIRK_H_ */
