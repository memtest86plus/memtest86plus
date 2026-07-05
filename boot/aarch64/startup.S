// SPDX-License-Identifier: GPL-2.0
//
// startup.S contains the 64-bit startup code for both the BSP and APs on
// ARM64 (AArch64). It initialises the MMU with our own identity-mapped page
// tables, drops from EL2 to EL1 if necessary, sets up the exception vectors
// and stacks, completes relocation, and finally calls the main application.
//
// The BSP is entered from the UEFI firmware at efi_boot with the MMU on
// (using the firmware page tables) at either EL2 or EL1. The APs are
// entered via PSCI CPU_ON at startup64 with the MMU off, again at either
// EL2 or EL1, with our CPU number in x0. After each program relocation,
// all CPUs re-enter at startup
//
// Copyright (C) 2026 Sam Demeulemeester
//

#define __ASSEMBLY__

#include "boot.h"

// Register initialisation values.

#define SCTLR_VALUE	0x30D01805	// RES1 bits, MMU on, D-cache on, I-cache on
#define TCR_VALUE	0x80903510	// T0SZ=16 IRGN0=WBWA ORGN0=WBWA SH0=inner TG0=4K
					// T1SZ=16 EPD1=1 TG1=4K (IPS filled in at run time)
#define MAIR_VALUE	0x4400FF	// idx0 = Normal WB WA, idx1 = Device-nGnRnE,
					// idx2 = Normal non-cacheable
#define HCR_VALUE	0x30080000000	// RW=1 (EL1 is AArch64), API=1 APK=1 (no PAuth traps)
#define CPTR_VALUE	0x33FF		// RES1 bits, no FP/SIMD traps
#define SPSR_EL1H_MASKED 0x3C5		// EL1h, all interrupt sources masked

// Exception context frame layout. This must match struct trap_regs in
// app/aarch64/interrupt.c

#define CTX_X(n)	((n) * 8)
#define CTX_SP		248
#define CTX_ELR		256
#define CTX_SPSR	264
#define CTX_ESR		272
#define CTX_FAR		280
#define CTX_VEC		288
#define CTX_SIZE	304

	.macro	mov_q, rd, val
	movz	\rd, #((\val) & 0xffff)
	movk	\rd, #(((\val) >> 16) & 0xffff), lsl #16
	movk	\rd, #(((\val) >> 32) & 0xffff), lsl #32
	movk	\rd, #(((\val) >> 48) & 0xffff), lsl #48
	.endm

	.text

	.globl	startup32
startup32:
	brk	#0		// unreachable (or at least should be)

// The EFI PE32+ boot entry point.

	.org	0x400
	.globl	efi_boot
efi_boot:
	// Preserve the EFI image handle and system table pointer while we
	// apply our dynamic relocations. This must be done before calling
	// any other C code, as we cannot rely on the linker eliminating
	// GOT indirection for data references
	mov	x19, x0
	mov	x20, x1
	bl	reloc
	mov	x0, x19
	mov	x1, x20
	mov	x2, xzr			// boot params not yet allocated
	bl	efi_setup

	// Save the boot params pointer.
	adrp	x1, boot_params_addr
	str	x0, [x1, :lo12:boot_params_addr]

	// Build our identity-mapped page tables. We are still running on the
	// firmwares page tables (at EL1 or EL2), which per the UEFI spec
	// identity-map all memory regions, so plain C code is fine here
	bl	paging_init

	// Record which copy of the program owns the current page tables
	adrp	x0, _start
	add	x0, x0, :lo12:_start
	adrp	x1, tables_for_base
	str	x0, [x1, :lo12:tables_for_base]

	mov	x0, xzr			// the BSP is CPU 0
	b	startup64

// The entry point for AP boot (via PSCI CPU_ON with the MMU off, CPU number
// in x0) and for the BSP after EFI setup (with the MMU on, using either the
// firmware page tables at EL1 or the firmware EL2 translation regime)

	.globl	startup64
startup64:
	msr	daifset, #0xF
	mov	x19, x0			// our CPU number

	// Enable FP/SIMD at EL1 and EL0 (the compiler may use SIMD registers).
	mov	x0, #(3 << 20)		// CPACR_EL1.FPEN
	msr	cpacr_el1, x0

	// Set up the EL1 translation regime to use our page tables. These
	// registers are writable from both EL1 and EL2.
	mov_q	x0, MAIR_VALUE
	msr	mair_el1, x0
	mov_q	x0, TCR_VALUE
	mrs	x1, id_aa64mmfr0_el1
	and	x1, x1, #0xF		// PARange
	cmp	x1, #5
	b.ls	1f
	mov	x1, #5			// cap the intermediate PA size at 48 bits
1:	bfi	x0, x1, #32, #3		// TCR_EL1.IPS
	msr	tcr_el1, x0
	adrp	x0, ttbr0_table
	add	x0, x0, :lo12:ttbr0_table
	msr	ttbr0_el1, x0
	adrp	x0, vectors
	add	x0, x0, :lo12:vectors
	msr	vbar_el1, x0

	mrs	x0, CurrentEL
	cmp	x0, #0x8
	b.eq	el2_entry

	// Entered at EL1. The MMU may be on (BSP, firmware page tables) or
	// off (AP). Switch to our page tables and make sure the MMU and
	// caches are enabled. Both mappings identity-map this code, so the
	// transition is safe
	isb
	tlbi	vmalle1
	dsb	nsh
	isb
	mov_q	x0, SCTLR_VALUE
	msr	sctlr_el1, x0
	isb
	b	el1_entry

el2_entry:
	// Initialise the EL2 registers that affect EL1, then drop to EL1
	// with the MMU and caches already enabled.

	// If the firmware runs EL2 with VHE (HCR_EL2.E2H set), the EL2
	// system register layouts differ and clearing E2H redefines the
	// active translation regime, so turn the EL2 MMU off first (this
	// code runs from an identity mapping, so that is safe)
	mrs	x0, hcr_el2
	tbz	x0, #34, 1f		// HCR_EL2.E2H
	mrs	x0, sctlr_el2
	bic	x0, x0, #1		// clear SCTLR_EL2.M
	msr	sctlr_el2, x0
	isb
	mov_q	x0, HCR_VALUE		// clears E2H
	msr	hcr_el2, x0
	isb
1:
	mrs	x0, midr_el1
	msr	vpidr_el2, x0
	mrs	x0, mpidr_el1
	msr	vmpidr_el2, x0
	mov	x0, #3			// EL1PCEN | EL1PCTEN
	msr	cnthctl_el2, x0
	msr	cntvoff_el2, xzr
	mov_q	x0, CPTR_VALUE
	msr	cptr_el2, x0
	msr	hstr_el2, xzr
	msr	mdcr_el2, xzr
	mov_q	x0, HCR_VALUE
	msr	hcr_el2, x0
	mov_q	x0, SCTLR_VALUE
	msr	sctlr_el1, x0
	tlbi	vmalle1
	dsb	nsh
	mov	x0, sp
	msr	sp_el1, x0		// continue on the same stack
	adr	x0, el1_entry
	msr	elr_el2, x0
	mov_q	x0, SPSR_EL1H_MASKED
	msr	spsr_el2, x0
	isb
	eret

el1_entry:
	msr	tpidr_el1, x19		// save our CPU number
	b	startup

// The main entry point and the entry point for restart after relocation.

	.globl	startup
startup:
	msr	daifset, #0xF

	// Some of the startup actions are not thread safe. Use a mutex
	// to protect this section of code
	adrp	x9, startup_mutex
	add	x9, x9, :lo12:startup_mutex
	mov	w10, #1
0:	ldaxr	w11, [x9]
	cbnz	w11, 0b
	stxr	w11, w10, [x9]
	cbnz	w11, 0b

	// Set up our stack.
	mrs	x10, tpidr_el1
	mov	x11, #AP_STACK_SIZE
	mul	x10, x10, x11
	mov	x11, #(BSP_STACK_SIZE - LOCALS_SIZE)
	add	x10, x10, x11
	adrp	x11, _stacks
	add	x11, x11, :lo12:_stacks
	add	x11, x11, x10
	mov	sp, x11

	bl	reloc

	// If this copy of the program doesnt own the current page tables,
	// rebuild them in this copy's memory (safe: we are still running on
	// the previous copy's tables, which identity-map everything)
	adrp	x0, _start
	add	x0, x0, :lo12:_start
	adrp	x9, tables_for_base
	ldr	x1, [x9, :lo12:tables_for_base]
	cmp	x0, x1
	b.eq	1f
	str	x0, [x9, :lo12:tables_for_base]
	bl	paging_init
1:
	// Every CPU (re)loads TTBR0 with *this copy's* page tables
	adrp	x0, ttbr0_table
	add	x0, x0, :lo12:ttbr0_table
	msr	ttbr0_el1, x0
	isb
	tlbi	vmalle1
	dsb	nsh
	isb

	// Point VBAR at *this copy's* exception vectors.
	adrp	x0, vectors
	add	x0, x0, :lo12:vectors
	msr	vbar_el1, x0
	isb

	// Release the startup mutex
	adrp	x9, startup_mutex
	add	x9, x9, :lo12:startup_mutex
	stlr	wzr, [x9]

	// Run Memtest86+.
	bl	main
2:	wfi
	b	2b

// The exception vector table.

	.macro	vec_entry, idx
	.align	7
	sub	sp, sp, #CTX_SIZE
	stp	x0, x1, [sp, #CTX_X(0)]
	mov	x0, #\idx
	b	exception_common
	.endm

	.align	11
	.globl	vectors
vectors:
	vec_entry 0		// current EL with SP0: synchronous
	vec_entry 1		// current EL with SP0: IRQ
	vec_entry 2		// current EL with SP0: FIQ
	vec_entry 3		// current EL with SP0: SError
	vec_entry 4		// current EL with SPx: synchronous
	vec_entry 5		// current EL with SPx: IRQ
	vec_entry 6		// current EL with SPx: FIQ
	vec_entry 7		// current EL with SPx: SError
	vec_entry 8		// lower EL (AArch64): synchronous
	vec_entry 9		// lower EL (AArch64): IRQ
	vec_entry 10		// lower EL (AArch64): FIQ
	vec_entry 11		// lower EL (AArch64): SError
	vec_entry 12		// lower EL (AArch32): synchronous
	vec_entry 13		// lower EL (AArch32): IRQ
	vec_entry 14		// lower EL (AArch32): FIQ
	vec_entry 15		// lower EL (AArch32): SError

	.align	2
exception_common:
	stp	x2,  x3,  [sp, #CTX_X(2)]
	stp	x4,  x5,  [sp, #CTX_X(4)]
	stp	x6,  x7,  [sp, #CTX_X(6)]
	stp	x8,  x9,  [sp, #CTX_X(8)]
	stp	x10, x11, [sp, #CTX_X(10)]
	stp	x12, x13, [sp, #CTX_X(12)]
	stp	x14, x15, [sp, #CTX_X(14)]
	stp	x16, x17, [sp, #CTX_X(16)]
	stp	x18, x19, [sp, #CTX_X(18)]
	stp	x20, x21, [sp, #CTX_X(20)]
	stp	x22, x23, [sp, #CTX_X(22)]
	stp	x24, x25, [sp, #CTX_X(24)]
	stp	x26, x27, [sp, #CTX_X(26)]
	stp	x28, x29, [sp, #CTX_X(28)]
	str	x30,      [sp, #CTX_X(30)]
	add	x1, sp, #CTX_SIZE
	str	x1, [sp, #CTX_SP]
	mrs	x1, elr_el1
	str	x1, [sp, #CTX_ELR]
	mrs	x1, spsr_el1
	str	x1, [sp, #CTX_SPSR]
	mrs	x1, esr_el1
	str	x1, [sp, #CTX_ESR]
	mrs	x1, far_el1
	str	x1, [sp, #CTX_FAR]
	str	x0, [sp, #CTX_VEC]

	mov	x0, sp
	bl	interrupt

	// If the handler returns, restore the interrupted context
	ldr	x1, [sp, #CTX_ELR]
	msr	elr_el1, x1
	ldr	x1, [sp, #CTX_SPSR]
	msr	spsr_el1, x1
	ldp	x2,  x3,  [sp, #CTX_X(2)]
	ldp	x4,  x5,  [sp, #CTX_X(4)]
	ldp	x6,  x7,  [sp, #CTX_X(6)]
	ldp	x8,  x9,  [sp, #CTX_X(8)]
	ldp	x10, x11, [sp, #CTX_X(10)]
	ldp	x12, x13, [sp, #CTX_X(12)]
	ldp	x14, x15, [sp, #CTX_X(14)]
	ldp	x16, x17, [sp, #CTX_X(16)]
	ldp	x18, x19, [sp, #CTX_X(18)]
	ldp	x20, x21, [sp, #CTX_X(20)]
	ldp	x22, x23, [sp, #CTX_X(22)]
	ldp	x24, x25, [sp, #CTX_X(24)]
	ldp	x26, x27, [sp, #CTX_X(26)]
	ldp	x28, x29, [sp, #CTX_X(28)]
	ldr	x30,      [sp, #CTX_X(30)]
	ldp	x0,  x1,  [sp, #CTX_X(0)]
	add	sp, sp, #CTX_SIZE
	eret

	.previous

// Variables.

	.data
	.align	4

	.globl	ap_startup_addr
ap_startup_addr:
	.quad	0			// filled in at run time

	.globl	boot_params_addr
boot_params_addr:
	.quad	0

tables_for_base:
	.quad	0

startup_mutex:
	.long	0

	.previous

// Main stack area.

	.section    ".stacks", "aw", %nobits
	.align  16

	. = . + STACKS_SIZE

	.previous
