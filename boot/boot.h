// SPDX-License-Identifier: GPL-2.0
#ifndef BOOT_H
#define BOOT_H
/**
 * \file
 *
 * Provides definitions used in the boot code. Also defines exported symbols
 * needed in the main code.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

/*
 * NOTE: Increasing the value of MAX_APS would require:
 *  - relocating the stacks when the program is loaded in low memory
 *  - adjusting the display if more than 3 digits are needed for CPU IDs
 */
#define	MAX_APS		511		/* Maximum number of active APs */

#define BSP_STACK_SIZE	16384		/* Stack size for the BSP */

/*
 * Stack size for each AP. Each CPU's thread-local storage (the barrier
 * waiting flags) occupies the top LOCALS_SIZE bytes of its own stack slot,
 * directly below the bottom of the next CPU's stack: an AP overrunning its
 * stack silently corrupts its neighbour's flags and freezes the whole test.
 * 1024 bytes proved too small for the error reporting path; the canary in
 * system/cpulocal.c turns any future overrun into a visible report.
 */
#define AP_STACK_SIZE	2048

#define	STACKS_SIZE	(BSP_STACK_SIZE + MAX_APS * AP_STACK_SIZE)

#define	LOCALS_SIZE	16		/* Stack region reserved for thread-local storage */

#define LOW_LOAD_ADDR	0x00010000	/* The low  load address for the main program */
#define HIGH_LOAD_ADDR	0x00100000	/* The high load address for the main program */

#define SETUP_SECS	7		/* Size of the 16-bit setup code in sectors */

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

/* All of the symbols declared below are defined by the startup assembly code
 * or by the linker script, i.e. outside of the C world. The program image is
 * a single, self-contained object at run time, so they should never be
 * preemptible. Declaring them with hidden visibility keeps the compiler from
 * emitting GOT-based accesses, which saves size; it is also required for LTO
 * building, because with LTO the linker trusts the compiler's symbol
 * resolution information, and a PC-relative access cannot be given a dynamic
 * relocation while a GOT-based access for a preemptible symbol would become
 * a GLOB_DAT relocation, which the runtime relocation processor does not
 * handle. Symbols that are not referenced from the C code at all are
 * claimed by the LTO build through boot/x86/lto_anchor.c (and the per
 * architecture equivalents) instead. */
#define MT_HIDDEN __attribute__((visibility("hidden")))

extern uint8_t	_start[] MT_HIDDEN;

extern uint8_t	startup32[] MT_HIDDEN;

extern uint8_t	startup64[] MT_HIDDEN;

extern uint8_t	startup[] MT_HIDDEN;

extern uint64_t	pml4[] MT_HIDDEN;

extern uint64_t	pdp[] MT_HIDDEN;

extern uint64_t	pd0[] MT_HIDDEN;
extern uint64_t	pd1[] MT_HIDDEN;
extern uint64_t	pd2[] MT_HIDDEN;
extern uint64_t	pd3[] MT_HIDDEN;

extern uintptr_t boot_params_addr MT_HIDDEN;

extern uint8_t	ap_trampoline[] MT_HIDDEN;

#if defined(__loongarch_lp64) || defined(__aarch64__)
extern uintptr_t ap_startup_addr MT_HIDDEN;
#else
extern uint32_t	ap_startup_addr MT_HIDDEN;
#endif

extern uint8_t	ap_trampoline_end[] MT_HIDDEN;

extern uint8_t	_stacks[] MT_HIDDEN;

extern uint8_t	_end[] MT_HIDDEN;

#undef MT_HIDDEN

#endif /* ! __ASSEMBLY__ */

#endif /* BOOT_H */
