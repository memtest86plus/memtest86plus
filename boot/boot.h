// SPDX-License-Identifier: GPL-2.0
#ifndef BOOT_H
#define BOOT_H
/**
 * \file
 *
 * Provides definitions used in the boot code. Also defines exported symbols
 * needed in the main code.
 *
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

/*
 * NOTE: Increasing the value of MAX_APS would require:
 *  - relocating the stacks when the program is loaded in low memory
 *  - modifying smp.c to support the x2APIC architecture
 *  - adjusting the display if more than 3 digits are needed for CPU IDs
 */
#define	MAX_APS		255		/* Maximum number of active APs */

#define BSP_STACK_SIZE	16384		/* Stack size for the BSP */
#define AP_STACK_SIZE	1024		/* Stack size for each AP */

#define	STACKS_SIZE	(BSP_STACK_SIZE + MAX_APS * AP_STACK_SIZE)

#define LOW_LOAD_ADDR	0x00010000	/* The low  load address for the main program */
#define HIGH_LOAD_ADDR	0x00100000	/* The high load address for the main program */

#define SETUP_SECS	2		/* Size of the 16-bit setup code in sectors */

#define BOOT_SEG	0x07c0		/* Segment address for the 16-bit boot code */
#define SETUP_SEG	0x07e0		/* Segment address for the 16-bit setup code */
#define MAIN_SEG	0x1000		/* Segment address for the main program code
					   when loaded by the 16-bit bootloader */

#define KERNEL_CS	0x10		/* 32-bit segment address for code */
#define KERNEL_DS	0x18		/* 32-bit segment address for data */

/* The following addresses are offsets from BOOT_SEG. */

#define BOOT_STACK	((1 + SETUP_SECS) * 512)
#define BOOT_STACK_TOP	((MAIN_SEG - BOOT_SEG) << 4)

/* The following definitions must match the Linux boot_params struct. */

#define E820_ENTRIES	0x1e8		/* offsetof(boot_params.e820_entries) */
#define E820_MAP	0x2d0		/* offsetof(boot_params.e820_table) */

#define E820_MAP_SIZE	128		/* max. number of entries in E820_MAP */

/* The following definitions must match the Linux e820_entry struct. */

#define E820_ADDR	0		/* offsetof(e820_entry.addr) */
#define E820_SIZE	8		/* offsetof(e820_entry.size) */
#define E820_TYPE	16		/* offsetof(e820_entry.type) */
#define E820_ENTRY_SIZE 20		/* sizeof(e820_entry) */

#ifndef __ASSEMBLY__

#include <stdint.h>

extern uint8_t	_start[];

extern uint8_t	startup32[];

extern uint8_t	startup[];

extern uint64_t	pml4[];

extern uint64_t	pdp[];

extern uint64_t	pd0[];
extern uint64_t	pd1[];
extern uint64_t	pd2[];
extern uint64_t	pd3[];

extern uintptr_t boot_params_addr;

extern uint8_t	ap_trampoline[];

extern uint32_t	ap_startup_addr;

extern uint8_t	ap_trampoline_end[];

extern uint8_t	_stacks[];

extern uint8_t	_end[];

#endif /* ! __ASSEMBLY__ */

#endif /* BOOT_H */
