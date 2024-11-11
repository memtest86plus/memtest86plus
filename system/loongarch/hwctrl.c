// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.

#include "stddef.h"

#include "boot.h"
#include "bootparams.h"
#include "efi.h"

#include "unistd.h"

#include "hwctrl.h"

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static efi_runtime_services_t   *efi_rs_table = NULL;

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void hwctrl_init(void)
{
    boot_params_t *boot_params = (boot_params_t *)boot_params_addr;
    if (boot_params->efi_info.loader_signature == EFI64_LOADER_SIGNATURE) {
        uintptr_t system_table_addr = (uintptr_t)boot_params->efi_info.sys_tab_hi << 32 | boot_params->efi_info.sys_tab;
        if (system_table_addr != 0) {
            efi64_system_table_t *sys_table = (efi64_system_table_t *)system_table_addr;
            efi_rs_table = (efi_runtime_services_t *)sys_table->runtime_services;
        }
    }
}

void reboot(void)
{
    if (efi_rs_table != NULL) {
        efi_rs_table->reset_system(EFI_RESET_COLD, 0, 0);
        usleep(1000000);
    } else {
        while (1);
    }
}

void floppy_off()
{
    //
    // Do nothing
    //
}

void cursor_off()
{
    //
    // Do nothing
    //
}
