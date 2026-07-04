// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester.

#include <stdint.h>

#include "acpi.h"

#include "psci.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// FADT ARM_BOOT_ARCH flags.

#define FADT_ARM_PSCI_COMPLIANT (1 << 0)
#define FADT_ARM_PSCI_USE_HVC   (1 << 1)

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

int64_t psci_call(uint64_t fn, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    register uint64_t x0 __asm__ ("x0") = fn;
    register uint64_t x1 __asm__ ("x1") = arg0;
    register uint64_t x2 __asm__ ("x2") = arg1;
    register uint64_t x3 __asm__ ("x3") = arg2;

    if (acpi_config.arm_boot_arch & FADT_ARM_PSCI_USE_HVC) {
        __asm__ __volatile__ ("hvc #0"
            : "+r" (x0), "+r" (x1), "+r" (x2), "+r" (x3)
            : /* no other inputs */
            : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
              "x12", "x13", "x14", "x15", "x16", "x17", "memory"
        );
    } else {
        __asm__ __volatile__ ("smc #0"
            : "+r" (x0), "+r" (x1), "+r" (x2), "+r" (x3)
            : /* no other inputs */
            : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
              "x12", "x13", "x14", "x15", "x16", "x17", "memory"
        );
    }

    return (int64_t)x0;
}

int64_t psci_cpu_on(uint64_t mpidr, uintptr_t entry_point, uint64_t context_id)
{
    return psci_call(PSCI_FN_CPU_ON, mpidr, entry_point, context_id);
}

void psci_system_reset(void)
{
    if (acpi_config.arm_boot_arch & FADT_ARM_PSCI_COMPLIANT) {
        (void)psci_call(PSCI_FN_SYSTEM_RESET, 0, 0, 0);
    }
}
