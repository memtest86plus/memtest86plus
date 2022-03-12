// SPDX-License-Identifier: GPL-2.0
#ifndef SMBIOS_H
#define SMBIOS_H
/**
 * \file
 *
 * Provides functions for reading SMBIOS tables
 *
 * Copyright (C) 2004-2022 Samuel Demeulemeester.
 */

/**
 * Initialize SMBIOS/DMI (locate struct)
 */

int smbios_init(void);

/**
 * Print DMI 
 */

void print_smbios_startup_info(void);

#endif // SMBIOS_H