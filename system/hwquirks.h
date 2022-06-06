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

#define QUIRK_TYPE_NONE     (1 << 0)
#define QUIRK_TYPE_USB      (1 << 1)
#define QUIRK_TYPE_SMP      (1 << 2)
#define QUIRK_TYPE_SMBIOS   (1 << 3)
#define QUIRK_TYPE_SMBUS    (1 << 4)
#define QUIRK_TYPE_TIMER    (1 << 5)
#define QUIRK_TYPE_MEM_SIZE (1 << 6)

typedef enum {
    QUIRK_NONE,
    QUIRK_TUSL2,
    QUIRK_ALI_ALADDIN_V
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
