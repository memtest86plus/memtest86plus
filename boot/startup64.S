// SPDX-License-Identifier: GPL-2.0
//
// startup64.S contains the 64-bit startup code for both the BSP and APs.
// It initialises stacks, memory management, and exception handling, clears
// the BSS, completes relocation, and finally calls the main application.
// It supports both the 32-bit and 64-bit Linux boot protocols and EFI boot
// for the first boot of the BSP.
//
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from memtest86+ head.S:
//
// linux/boot/head.S
// Copyright (C) 1991, 1992  Linus Torvalds
// 1-Jan-96 Modified by Chris Brady for use as a boot/loader for MemTest-86.
// Set up the memory management for flat non-paged linear addressing.
// 17 May 2004 : Added X86_PWRCAP for AMD64 (Memtest86+ - Samuel D.)

#define __ASSEMBLY__

#include "boot.h"

#define NUM_INT_VEC	20

	.text
	.code32

# The Linux 32-bit boot entry point.

	.globl	startup32
startup32:
	cld
	cli

	# Get the load address.

	movl	0x214(%esi), %ebx	# bootparams.code32_start

	# Save the boot params pointer.

	movl	%esi, (boot_params_addr - startup32)(%ebx)

	# Use the startup stack until we pick the correct one.

	leal	(startup_stack_top - startup32)(%ebx), %esp

	# Initialise the pml4 and pdp tables.

	leal	(pml4 - startup32)(%ebx), %ecx
	leal	(pdp - startup32)(%ebx), %edx
	movl	%edx, %eax
	addl	$0x3, %eax
	movl	%eax, 0(%ecx)
	leal	(pd0 - startup32)(%ebx), %eax
	addl	$0x3, %eax
	movl	%eax, 0(%edx)
	leal	(pd1 - startup32)(%ebx), %eax
	addl	$0x3, %eax
	movl	%eax, 8(%edx)
	leal	(pd2 - startup32)(%ebx), %eax
	addl	$0x3, %eax
	movl	%eax, 16(%edx)
	leal	(pd3 - startup32)(%ebx), %eax
	addl	$0x3, %eax
	movl	%eax, 24(%edx)

	# Set the page directory base address.

	movl	%ecx, %cr3

	# Enable PAE.

	movl	%cr4, %eax
	orl	$0x20, %eax
	movl	%eax, %cr4

	# Enable long mode.

	movl	$0xc0000080, %ecx
	rdmsr
	orl	$0x00000100, %eax
	wrmsr

	# Enable paging and protection.

	movl	%cr0, %eax
	orl	$0x80000001, %eax
	movl	%eax, %cr0

	# Initialise the 64-bit GDT descriptor.

	leal	(gdt - startup32)(%ebx), %eax
	movl	%eax, 2 + (gdt_descr - startup32)(%ebx)

	# Load the GDT and enter long mode.

	lgdt	(gdt_descr - startup32)(%ebx)
	leal	(startup - startup32)(%ebx), %eax
	movw	$KERNEL_CS, -2(%esp)
	movl	%eax, -6(%esp)
	ljmp	*-6(%esp)

	.code64

# The EFI PE32+ boot entry point.

	.org	0x1e0
	.globl	efi_boot
efi_boot:
	movq	%rcx, %rdi		# the EFI image handle
	movq	%rdx, %rsi		# the EFI system table pointer
	movq	$0, %rdx		# the boot params pointer (0 = not yet allocated)
	jmp	efi_handover

# The Linux 64-bit boot entry point.

	.org	0x200
	.globl	startup64
startup64:
	cld
	cli

	# Save the boot params pointer.

	movq	%rsi, boot_params_addr(%rip)

	jmp	startup

# The Linux 64-bit EFI handover point.

	.org	0x210
	.globl	efi_handover
efi_handover:
	andq	$~0xf, %rsp
	call	efi_setup

	# Save the boot params pointer.

	movq	%rax, boot_params_addr(%rip)

# The 64-bit entry point for AP boot and for restart after relocation.

	.globl	startup
startup:
	# Some of the startup actions are not thread safe. Use a mutex
	# to protect this section of code.

0: lock btsl	$0, startup_mutex(%rip)
	jc	0b

	# Use the startup stack until we pick the correct one.

	leaq	startup_stack_top(%rip), %rsp

	# Pick the correct stack.

	xorq	%rax, %rax
	call	smp_my_cpu_num
	movl	$AP_STACK_SIZE, %edx
	mul	%edx
	addq	$(BSP_STACK_SIZE - LOCALS_SIZE), %rax
	leaq	_stacks(%rip), %rsp
	addq	%rax, %rsp

	# Initialise the pml4 and pdp tables.

	leaq	pml4(%rip), %rcx
	leaq	pdp(%rip), %rdx
	movq	%rdx, %rax
	addq	$0x3, %rax
	movq	%rax, 0(%rcx)
	leaq	pd0(%rip), %rax
	addq	$0x3, %rax
	movq	%rax, 0(%rdx)
	leaq	pd1(%rip), %rax
	addq	$0x3, %rax
	movq	%rax, 8(%rdx)
	leaq	pd2(%rip), %rax
	addq	$0x3, %rax
	movq	%rax, 16(%rdx)
	leaq	pd3(%rip), %rax
	addq	$0x3, %rax
	movq	%rax, 24(%rdx)

	# Set the page directory base address.

	movq	%rcx, %cr3

	# Initialise the GDT descriptor.

	leaq	gdt(%rip), %rax
	movq	%rax, 2 + gdt_descr(%rip)

	# Load the GDT and the segment registers.

	lgdt	gdt_descr(%rip)
	leaq	flush(%rip), %rax
	movw	$KERNEL_CS, -2(%rsp)
	movl	%eax, -6(%rsp)
	ljmp	*-6(%rsp)
flush:	movw	$KERNEL_DS, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss

	# Initialise the IDT.

	leaq	idt(%rip), %rdi
	leaq	vec0(%rip), %rsi
	movw	$NUM_INT_VEC, %cx
0:	movq	%rsi, %rdx
	movl	$(KERNEL_CS << 16), %eax
	movw	%dx, %ax		# selector = 0x0010 = cs
	movw	$0x8E00, %dx		# interrupt gate - dpl=0, present
	movl	%eax, (%rdi)
	movl	%edx, 4(%rdi)
	shrq	$32, %rdx
	movl	%edx, 8(%rdi)
	movl	$0,  12(%rdi)
	addq	$(vec1-vec0), %rsi
	addq	$16, %rdi
	dec	%cx
	jnz	0b

	# Initialise the IDT descriptor.

	leaq	idt(%rip), %rax
	movq	%rax, 2 + idt_descr(%rip)

	# Load the IDT.

	lidt	idt_descr(%rip)

	# Zero the BSS (if first boot).

	cmpl	$1, first_boot(%rip)
	jne	1f
	xorq	%rax, %rax
	leaq	_bss(%rip), %rdi
	leaq	_end(%rip), %rcx
	subq	%rdi, %rcx
0:	movq	%rax, (%rdi)
	addq	$8, %rdi
	subq	$8, %rcx
	jnz	0b
	movl	$0, first_boot(%rip)
1:
	# Initialise the FPU.

	finit

#if 0
	# Enable SSE.

	movq	%cr0, %rax
	andw	$0xfffb, %ax		# clear coprocessor emulation bit
	orw	$0x0002, %ax		# set coprocessor monitoring bit
	mov	%rax, %cr0
	movq	%cr4, %rax
	orw	$0x0600, %ax		# set OSFXSR and OSXMMEXCPT
	movq	%rax, %cr4
#endif

	# Call the dynamic linker to fix up the addresses in the GOT.

	call	reloc

	# Release the startup mutex.

	movl	$0, startup_mutex(%rip)

	# Run the application.

	call	main

	# In case we return, simulate an exception.

	pushfq
	xorq	%rax, %rax
	movw	%cs, %ax
	pushq	%rax
	call	0f
0:	pushq	$0	# error code
	pushq	$257	# vector
	jmp	int_handler

# Individual interrupt vector handlers. These need to be spaced equally, to
# allow the IDT initialisation loop above to work, so we use noops to pad out
# where required.

vec0:
	pushq	$0	# error code
	pushq	$0	# vector
	jmp	int_handler

vec1:
	pushq	$0	# error code
	pushq	$1	# vector
	jmp	int_handler

vec2:
	pushq	$0	# error code
	pushq	$2	# vector
	jmp	int_handler

vec3:
	pushq	$0	# error code
	pushq	$3	# vector
	jmp	int_handler

vec4:
	pushq	$0	# error code
	pushq	$4	# vector
	jmp	int_handler

vec5:
	pushq	$0	# error code
	pushq	$5	# vector
	jmp	int_handler

vec6:
	pushq	$0	# error code
	pushq	$6	# vector
	jmp	int_handler

vec7:
	pushq	$0	# error code
	pushq	$7	# vector
	jmp	int_handler

vec8:
	nop;nop 	# error code already provided
	pushq	$8	# vector
	jmp	int_handler

vec9:
	pushq	$0	# error code
	pushq	$9	# vector
	jmp	int_handler

vec10:
	nop;nop 	# error code already provided
	pushq	$10	# vector
	jmp	int_handler

vec11:
	nop;nop 	# error code already provided
	pushq	$11	# vector
	jmp	int_handler

vec12:
	nop;nop 	# error code already provided
	pushq	$12	# vector
	jmp	int_handler

vec13:
	nop;nop 	# error code already provided
	pushq	$13	# vector
	jmp	int_handler

vec14:
	nop;nop 	# error code already provided
	pushq	$14	# vector
	jmp	int_handler

vec15:
	pushq	$0	# error code
	pushq	$15	# vector
	jmp	int_handler

vec16:
	pushq	$0	# error code
	pushq	$16	# vector
	jmp	int_handler

vec17:
	nop;nop 	# error code
	pushq	$17	# vector
	jmp	int_handler

vec18:
	pushq	$0	# error code
	pushq	$18	# vector
	jmp	int_handler

vec19:
	pushq	$0	# error code
	pushq	$19	# vector
	jmp	int_handler

# The common interrupt handler code. Pass the register state to the application
# interrupt handler. On entry this expects the stack to contain:
#
#	rsp+30	ss
#	rsp+28	rsp
#	rsp+20	rflags
#	rsp+18	cs
#	rsp+10	rip
#	rsp+08	error code
#	rsp+00	vector number
#
# It adds the additional state expected by the application to the bottom of the
# stack frame.

int_handler:
	pushq	%rbp
	pushq	%rsi
	pushq	%rdi
	pushq	%rdx
	pushq	%rcx
	pushq	%rbx
	pushq	%rax
	xorq	%rax, %rax
	movw	%ss, %ax
	pushq	%rax
	movw	%es, %ax
	pushq	%rax
	movw	%ds, %ax
	pushq	%rax
	movq	%rsp, %rdi		# pointer to trap regs struct on the stack
	cld
	call	interrupt
	addq	$24, %rsp
	popq	%rax
	popq	%rbx
	popq	%rcx
	popq	%rdx
	popq	%rdi
	popq	%rsi
	popq	%rbp
	addq	$16, %rsp		# discard the vector number and error code
	iretq

# The interrupt descriptor table.

	.align	4
	.word	0			# for alignment
idt_descr:
	.word	idt_end - idt - 1	# size
	.quad	0			# addr: filled in at run time

idt:
	.fill	NUM_INT_VEC*2, 8, 0	# filled in at run time
idt_end:

# The global descriptor table.

	.word	0			# for alignment
gdt_descr:
	.word	gdt_end - gdt - 1	# size
	.quad	0			# addr: filled in at run time

	.align	4
	.globl	gdt
gdt:
	.quad	0x0000000000000000	# NULL descriptor
	.quad	0x0000000000000000	# not used
	.quad	0x00209a0000000000	# 0x10 64-bit code at 0x000000
	.quad	0x0000920000000000	# 0x18 64-bit data at 0x000000

	.globl	gdt_end
gdt_end:

	.data

	.macro	ptes64 start, count=64
	.quad	\start + 0x0000000 + 0x83
	.quad	\start + 0x0200000 + 0x83
	.quad	\start + 0x0400000 + 0x83
	.quad	\start + 0x0600000 + 0x83
	.quad	\start + 0x0800000 + 0x83
	.quad	\start + 0x0A00000 + 0x83
	.quad	\start + 0x0C00000 + 0x83
	.quad	\start + 0x0E00000 + 0x83
	.if \count-1
	ptes64	"(\start+0x01000000)",\count-1
	.endif
	.endm

	.macro	maxdepth depth=1
	.if \depth-1
	maxdepth \depth-1
	.endif
	.endm

	maxdepth

# The level 4 page map table.

	.align	4096
	.globl	pml4
pml4:
	.quad	0			# filled in at run time

# Page Directory Pointer Table:
# 4 Entries, pointing to the Page Directory Tables.

	.align	4096
	.globl	pdp
pdp:
	.quad	0			# filled in at run time
	.quad	0			# filled in at run time
	.quad	0			# filled in at run time
	.quad	0			# filled in at run time

# Page Directory Tables:
# There are 4 tables. The first two map the first 2 GB of memory. The third
# is used with PAE to map the rest of memory in 1 GB segments. The fourth is
# reserved for mapping the video frame buffer. We use 2 MB pages so only the
# Page Directory Table is used (no page tables).

	.align	4096
	.globl	pd0
pd0:
	ptes64	0x0000000000000000

	.align	4096
	.globl	pd1
pd1:
	ptes64	0x0000000040000000

	.align	4096
	.globl	pd2
pd2:
	ptes64	0x0000000080000000

	.align	4096
	.globl	pd3
pd3:
	ptes64	0x00000000C0000000

	.previous

# ap_trampoline is the entry point for CPUs other than the bootstrap
# CPU (BSP). It gets copied to a page in low memory, to enable the APs
# to boot when the main program has been loaded in high memory.

	.code16
	.align	4

	.globl	ap_trampoline
ap_trampoline:
	movw	%cs, %ax
	movw	%ax, %ds

	# Patch the jump address.

	movl	(ap_startup_addr - ap_trampoline), %ebx
	movl	%ebx, (ap_jump - ap_trampoline + 2)

	# Patch and load the GDT descriptor. It should point to the main
	# GDT descriptor, which has already been initialised by the BSP.

	movl	%ebx, %eax
	addl	$(gdt - startup), %eax
	movl	%eax, (ap_gdt_descr - ap_trampoline + 2)
data32	lgdt	ap_gdt_descr - ap_trampoline

	# Set the page directory base address.

	movl	%ebx, %eax
	addl	$(pml4 - startup), %eax
	movl	%eax, %cr3

	# Enable PAE.

	movl	%cr4, %eax
	orl	$0x20, %eax
	movl	%eax, %cr4

	# Enable long mode.

	movl	$0xc0000080, %ecx
	rdmsr
	orl	$0x00000100, %eax
	wrmsr

	# Enable paging and protection.

	movl	%cr0, %eax
	orl	$0x80000001, %eax
	movl	%eax, %cr0

	# Jump to the 64-bit entry point.
ap_jump:
data32	ljmp	$KERNEL_CS, $0

	.align	4
	.word	0			# for alignment
ap_gdt_descr:
	.word	gdt_end - gdt - 1	# gdt limit
	.long	0			# gdt base - filled in at run time

	.globl	ap_startup_addr
ap_startup_addr:
	.long	0			# filled in at run time

	.globl	ap_trampoline_end
ap_trampoline_end:

	.previous

# Variables.

	.data
	.align	4

	.globl	boot_params_addr
boot_params_addr:
	.quad	0

startup_mutex:
	.long	0

first_boot:
	.long	1

	.previous

# Startup stack.

	.bss
	.align	16

startup_stack_base:
	. = . + 64
startup_stack_top:

	.previous

# Main stack area.

	.section    ".stacks", "aw", @nobits
	.align  16

	. = . + STACKS_SIZE

	.previous
