// SPDX-License-Identifier: GPL-2.0
#ifndef _ACPI_H_
#define _ACPI_H_
/**
 * \file
 *
 * Provides support for multi-threaded operation.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 * Copyright (C) 2020-2022 Sam Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

#define FADT_PM_TMR_BLK_OFFSET      76
#define FADT_MINOR_REV_OFFSET       131
#define FADT_X_PM_TMR_BLK_OFFSET    208

/**
 * A struct containing various ACPI-related infos for later uses.
 */

typedef struct __attribute__ ((packed)) {
    uint8_t     ver_maj;
    uint8_t     ver_min;
    uintptr_t   rsdp_addr;
    uintptr_t   madt_addr;
    uintptr_t   fadt_addr;
    uintptr_t   hpet_addr;
    uintptr_t   pm_addr;
    bool        pm_is_io;
} acpi_t;

/**
 * The search step that located the ACPI RSDP (for debug).
 */
extern const char *rsdp_source;

/**
 * Global ACPI config struct
 */
extern acpi_t acpi_config;

/**
 * ACPI Table Checksum Function
 */
int acpi_checksum(const void *data, int length);

/**
 * Initialises the SMP state and detects the number of available CPU cores.
 */
void acpi_init(void);

#endif /* _ACPI_H_ */
