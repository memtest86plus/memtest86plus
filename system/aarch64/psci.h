// SPDX-License-Identifier: GPL-2.0
#ifndef PSCI_H
#define PSCI_H
/**
 * \file
 *
 * Provides an interface to the ARM Power State Coordination Interface
 * (PSCI) firmware functions.
 *
 *//*
  // Copyright (C) 2026 Sam Demeulemeester.
 */

#include <stdint.h>

// PSCI function IDs

#define PSCI_FN_VERSION         UINT32_C(0x84000000)
#define PSCI_FN_CPU_ON          UINT32_C(0xC4000003)
#define PSCI_FN_SYSTEM_OFF      UINT32_C(0x84000008)
#define PSCI_FN_SYSTEM_RESET    UINT32_C(0x84000009)

// PSCI return codes

#define PSCI_RET_SUCCESS        0
#define PSCI_RET_NOT_SUPPORTED  -1
#define PSCI_RET_ALREADY_ON     -4

/**
 * Makes a PSCI firmware call using the conduit indicated by the ACPI FADT
 */
int64_t psci_call(uint64_t fn, uint64_t arg0, uint64_t arg1, uint64_t arg2);

/**
 * Starts the CPU core identified by mpidr at the given entry point address.
 * The entry point is entered with the MMU off and context_id in x0
 */
int64_t psci_cpu_on(uint64_t mpidr, uintptr_t entry_point, uint64_t context_id);

/**
 * Resets the system. Only returns if PSCI is not availabel or the call fails
 */
void psci_system_reset(void);

#endif // PSCI_H
