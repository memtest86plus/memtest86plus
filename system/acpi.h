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

/**
 * The search step that located the ACPI RSDP (for debug).
 */
extern const char *rsdp_source;

/**
 * The address of the ACPI RSDP
 */
extern uintptr_t rsdp_addr;

/**
 * The address of the ACPI MADT
 */
extern uintptr_t madt_addr;

/**
 * The address of the ACPI FADT
 */
extern uintptr_t fadt_addr;

/**
 * The address of the ACPI HPET
 */
extern uintptr_t hpet_addr;

/**
 * ACPI Table Checksum Function
 */
int acpi_checksum(const void *data, int length);

/**
 * Initialises the SMP state and detects the number of available CPU cores.
 */
void acpi_init(void);

#endif /* _ACPI_H_ */
