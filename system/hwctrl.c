// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from an extract of memtest86+ lib.c:
//
// lib.c - MemTest-86  Version 3.4
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include "stddef.h"

#include "boot.h"
#include "bootparams.h"
#include "efi.h"

#include "io.h"

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
#ifdef __x86_64__
    if (boot_params->efi_info.loader_signature == EFI64_LOADER_SIGNATURE) {
        uintptr_t system_table_addr = (uintptr_t)boot_params->efi_info.sys_tab_hi << 32 | boot_params->efi_info.sys_tab;
        if (system_table_addr != 0) {
            efi64_system_table_t *sys_table = (efi64_system_table_t *)system_table_addr;
            efi_rs_table = (efi_runtime_services_t *)sys_table->runtime_services;
        }
    }
#else
    if (boot_params->efi_info.loader_signature == EFI32_LOADER_SIGNATURE) {
        uintptr_t system_table_addr = boot_params->efi_info.sys_tab;
        if (system_table_addr != 0) {
            efi32_system_table_t *sys_table = (efi32_system_table_t *)system_table_addr;
            efi_rs_table = (efi_runtime_services_t *)(uintptr_t)sys_table->runtime_services;
        }
    }
#endif
}

void reboot(void)
{
    // Use cf9 method as first try
    uint8_t cf9 = inb(0xcf9) & ~6;
    outb(cf9|2, 0xcf9); // Request hard reset
    usleep(50);
    outb(cf9|6, 0xcf9); // Actually do the reset
    usleep(50);

    // If we have UEFI, try EFI reset service
    if (efi_rs_table != NULL) {
        efi_rs_table->reset_system(EFI_RESET_COLD, 0, 0);
        usleep(1000000);
    }

     // Still here? try the PORT 0x64 method
    outb(0xfe, 0x64);
    usleep(150000);

#ifndef __x86_64__
    if (efi_rs_table == NULL) {
        // In last resort, (very) obsolete reboot method using BIOS
        *((uint16_t *)0x472) = 0;            // Request cold reboot.

        // Switch the CPU back to the real mode.
        __asm__ __volatile__("\t"
            "movl    $16, %ecx\n\t"
            "movl    %ecx, %ds\n\t"
            "movl    %ecx, %es\n\t"
            "movl    %ecx, %fs\n\t"
            "movl    %ecx, %gs\n\t"
            "movl    %ecx, %ss\n\t"

            "xorl    %ecx, %ecx\n\t"
            "movl    %cr0, %edx\n\t"
            "andl    $0x00000011, %edx\n\t"
            "orl     $0x60000000, %edx\n\t"
            "movl    %edx, %cr0\n\t"
            "movl    %ecx, %cr3\n\t"

            "movl    %cr0, %edx\n\t"
            "testl   $0x60000000, %edx\n\t"  // If no cache bits -> no wbinvd.
            "jz      2f\n\t"
            "wbinvd\n\t"

            "2:\n\t"
            "andb    $0x10, %dl\n\t"
            "movl    %edx, %cr0\n\t"

            // call BIOS reboot routine.
            "ljmpw   $0xf000, $0xfff0\n\t"
        );
    }
#endif

    /**
     * Make sure we won't return to the calling function in case all
     * of the above methods failed.
     */

    for (;;) {
         __asm__ __volatile__("hlt");
    }

}

void floppy_off()
{
    // Stop the floppy motor.
    outb(0x8, 0x3f2);
}

void cursor_off()
{
    // Set HW cursor off screen.
    outb(0x0f, 0x3d4);
    outb(0xff, 0x3d5);

    outb(0x0e, 0x3d4);
    outb(0xff, 0x3d5);
}
