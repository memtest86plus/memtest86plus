// SPDX-License-Identifier: GPL-2.0
//
// startup32.S contains the 32-bit startup code for both the BSP and APs.
// It initialises stacks, memory management, and exception handling, clears
// the BSS, completes relocation, and finally calls the main application.
// It supports the 32-bit Linux boot protocol and EFI boot for the first
// boot of the BSP.
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

#define	INT64_CS	0x08

#define NUM_INT_VEC	20

	.text
	.code32

# The Linux 32-bit boot entry point.

	.globl	startup32
startup32:
	cld
	cli

	# Jump to the shared 32-bit entry point with the boot params pointer
	# in %esi and the startup address in %edi.

	movl	0x214(%esi), %ebx	# bootparams.code32_start
	leal	(startup - startup32)(%ebx), %edi
	jmp	startup

# The Linux 32-bit EFI handover point.

	.org	0x10
	.globl	efi_handover
efi_handover:
	popl	%eax			# the return address (discard)
	popl	%ecx			# the EFI image handle
	popl	%edx			# the EFI system table pointer
	popl	%esi			# the boot params pointer

	# Load the GOT pointer.

	call	0f
0:	popl	%ebx
	addl	$_GLOBAL_OFFSET_TABLE_+[.-0b], %ebx

	# Fill out the boot params structure.

	subl	$12, %esp		# align the stack
	andl	$~0xf, %esp
	addl	$12, %esp

	pushl	%esi			# the boot params pointer
	pushl	%edx			# the EFI system table pointer
	pushl	%ecx			# the EFI image handle

	call	efi_setup

	# Fall through to the shared 32-bit entry point with the boot params
	# pointer in %esi and the startup address in %edi.

	movl	%eax, %esi
	movl	0x214(%esi), %ebx	# bootparams.code32_start
	leal	(startup - startup32)(%ebx), %edi

# The 32-bit entry point for AP boot and for restart after relocation.

	.globl	startup
startup:
	# Some of the startup actions are not thread safe. Use a mutex
	# to protect this section of code.

	leal	(startup_mutex - startup)(%edi), %eax
0: lock btsl	$0, (%eax)
	jc	0b

	# Use the startup stack until we pick the correct one.

	leal	(startup_stack_top - startup)(%edi), %esp

	# Load the GOT pointer.

	call	0f
0:	popl	%ebx
	addl	$_GLOBAL_OFFSET_TABLE_+[.-0b], %ebx

	# If first boot, save the boot params pointer...

	cmpl	$1, first_boot@GOTOFF(%ebx)
	jne	1f
	movl	%esi, boot_params_addr@GOTOFF(%ebx)

	# ...and check if the processor supports long mode.

	movl	$0x80000000, %eax	# check if function 0x80000001 is available
	pushl	%ebx			# ebx is overwritten by cpuid
	cpuid
	popl	%ebx			# restore ebx
	cmpl	$0x80000001, %eax
	jb	1f
	movl	$0x80000001, %eax	# test the LM flag
	pushl	%ebx			# ebx is overwritten by cpuid
	cpuid
	popl	%ebx			# restore ebx
	andl	$0x20000000, %edx
	jz	1f
	movl	$1, use_long_mode@GOTOFF(%ebx)
1:

	# Pick the correct stack.

	call	smp_my_cpu_num
	movl	$AP_STACK_SIZE, %edx
	mul	%edx
	addl	$(BSP_STACK_SIZE - LOCALS_SIZE), %eax
	leal	_stacks@GOTOFF(%ebx), %esp
	addl	%eax, %esp

	# Initialise the GDT descriptor.

	leal	gdt@GOTOFF(%ebx), %eax
	movl	%eax, 2 + gdt_descr@GOTOFF(%ebx)

	# Load the GDT and the segment registers.

	lgdt	gdt_descr@GOTOFF(%ebx)
	leal	flush@GOTOFF(%ebx), %eax
	movw	$KERNEL_CS, -2(%esp)
	movl	%eax, -6(%esp)
	ljmp	*-6(%esp)
flush:	movw	$KERNEL_DS, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss

	# Initialise the IDT. If we are going to operate in long mode, we need
	# a 64-bit IDT, otherwise we need a 32-bit IDT.

	leal	idt@GOTOFF(%ebx), %edi
	cmpl	$1, use_long_mode@GOTOFF(%ebx)
	je	init_idt64
	jmp	init_idt32

	# Initialise the IDT descriptor.

init_idt_descr:
	movw	%ax, idt_descr@GOTOFF(%ebx)
	leal	idt@GOTOFF(%ebx), %eax
	movl	%eax, 2 + idt_descr@GOTOFF(%ebx)

	# Load the IDT.

	lidt	idt_descr@GOTOFF(%ebx)

	# Zero the BSS (if first boot).

	cmpl	$1, first_boot@GOTOFF(%ebx)
	jne	1f
	xorl	%eax, %eax
	leal	_bss@GOTOFF(%ebx), %edi
	leal	_end@GOTOFF(%ebx), %ecx
	subl	%edi, %ecx
0:	movl	%eax, (%edi)
	addl	$4, %edi
	subl	$4, %ecx
	jnz	0b
	movl	$0, first_boot@GOTOFF(%ebx)
1:
	# Initialise the FPU.

	finit

	# Call the dynamic linker to fix up the addresses in the GOT.

	call	reloc

	# Disable paging (needed during restart). Also disable write protect
	# (in case set by EFI boot).

	movl	%cr0, %eax
	andl	$0x7ffeffff, %eax
	movl	%eax, %cr0

	# Enable PAE if supported.

	pushl	%ebx			# ebx is overwritten by cpuid
	movl	$0x00000001, %eax	# test the PAE flag
	cpuid
	andl	$0x00000040, %edx
	popl	%ebx			# restore ebx
	jz	1f			# bail if not supported

	movl	%cr4, %eax		# enable PAE
	orl	$0x00000020, %eax
	movl	%eax, %cr4

	leal	pdp@GOTOFF(%ebx), %eax	# set the page directory base address
	movl	%eax, %cr3

	# Enable long mode if supported.

	cmpl	$1, use_long_mode@GOTOFF(%ebx)
	jne	0f

	movl	$0xc0000080, %ecx	# enable long mode
	rdmsr
	orl	$0x00000100, %eax
	wrmsr

	leal	pml4@GOTOFF(%ebx), %eax # set the page directory base address
	movl	%eax, %cr3

	# Enable paging and protection.

0:	movl	%cr0, %eax
	orl	$0x80000001, %eax
	movl	%eax, %cr0
1:
	# Release the startup mutex.

	movl	$0, startup_mutex@GOTOFF(%ebx)

	# Run the application.

	call	main

	# In case we return, simulate an exception.

	pushfl
	pushl	%cs
	call	0f
0:	pushl	$0	# error code
	pushl	$257	# vector
	jmp	int_handler32

# The EFI PE32 boot entry point.

	.org	0x1e0
	.globl	efi_boot
efi_boot:
	popl	%eax			# the return address (discard)
	popl	%ecx			# the EFI image handle
	popl	%edx			# the EFI system table pointer

	pushl	$0			# the boot params pointer (0 = not yet allocated)
	pushl	%edx			# the EFI system table pointer
	pushl	%ecx			# the EFI image handle

	call	efi_handover		# never returns

# Initialise the 64-bit IDT.

init_idt64:
	leal	vec64_0@GOTOFF(%ebx), %esi
	movw	$NUM_INT_VEC, %cx
0:	movl	%esi, %edx
	movl	$(INT64_CS << 16), %eax
	movw	%dx, %ax		# selector = 0x0008 = long mode cs
	movw	$0x8E00, %dx		# interrupt gate - dpl=0, present
	movl	%eax, (%edi)
	movl	%edx, 4(%edi)
	addl	$(vec64_1-vec64_0), %esi
	addl	$16, %edi
	dec	%cx
	jnz	0b

	movw	$(NUM_INT_VEC*16 - 1), %ax
	jmp	init_idt_descr

# Initialise the 32-bit IDT.

init_idt32:
	leal	vec32_0@GOTOFF(%ebx), %esi
	movw	$NUM_INT_VEC, %cx
0:	movl	%esi, %edx
	movl	$(KERNEL_CS << 16), %eax
	movw	%dx, %ax		# selector = 0x0010 = cs
	movw	$0x8E00, %dx		# interrupt gate - dpl=0, present
	movl	%eax, (%edi)
	movl	%edx, 4(%edi)
	addl	$(vec32_1-vec32_0), %esi
	addl	$8, %edi
	dec	%cx
	jnz	0b

	movw	$(NUM_INT_VEC*8 - 1), %ax
	jmp	init_idt_descr

# Individual interrupt vector handlers for long mode. These need to be
# spaced equally, to allow the IDT initialisation loop above to work,
# so we use noops to pad out where required.

	.code64

vec64_0:
	pushq	$0	# error code
	pushq	$0	# vector
	jmp	int_handler64

vec64_1:
	pushq	$0	# error code
	pushq	$1	# vector
	jmp	int_handler64

vec64_2:
	pushq	$0	# error code
	pushq	$2	# vector
	jmp	int_handler64

vec64_3:
	pushq	$0	# error code
	pushq	$3	# vector
	jmp	int_handler64

vec64_4:
	pushq	$0	# error code
	pushq	$4	# vector
	jmp	int_handler64

vec64_5:
	pushq	$0	# error code
	pushq	$5	# vector
	jmp	int_handler64

vec64_6:
	pushq	$0	# error code
	pushq	$6	# vector
	jmp	int_handler64

vec64_7:
	pushq	$0	# error code
	pushq	$7	# vector
	jmp	int_handler64

vec64_8:
	nop;nop 	# error code already provided
	pushq	$8	# vector
	jmp	int_handler64

vec64_9:
	pushq	$0	# error code
	pushq	$9	# vector
	jmp	int_handler64

vec64_10:
	nop;nop 	# error code already provided
	pushq	$10	# vector
	jmp	int_handler64

vec64_11:
	nop;nop 	# error code already provided
	pushq	$11	# vector
	jmp	int_handler64

vec64_12:
	nop;nop 	# error code already provided
	pushq	$12	# vector
	jmp	int_handler64

vec64_13:
	nop;nop 	# error code already provided
	pushq	$13	# vector
	jmp	int_handler64

vec64_14:
	nop;nop 	# error code already provided
	pushq	$14	# vector
	jmp	int_handler64

vec64_15:
	pushq	$0	# error code
	pushq	$15	# vector
	jmp	int_handler64

vec64_16:
	pushq	$0	# error code
	pushq	$16	# vector
	jmp	int_handler64

vec64_17:
	nop;nop 	# error code
	pushq	$17	# vector
	jmp	int_handler64

vec64_18:
	pushq	$0	# error code
	pushq	$18	# vector
	jmp	int_handler64

vec64_19:
	pushq	$0	# error code
	pushq	$19	# vector
	jmp	int_handler64

# The interrupt handler code for long mode. Pass the register state to the
# common interrupt handler. On entry this expects the stack to contain:
#
#	rsp+30	ss
#	rsp+28	rsp
#	rsp+20	rflags
#	rsp+18	cs
#	rsp+10	rip
#	rsp+08	error code
#	rsp+00	vector number
#
# We create a new stack frame in the format expected by int_handler. We can
# reuse the space currently occupied by the vector number and the error code,
# as they are not needed on return.

int_handler64:
	subq	$16, %rsp
	movl	%ebp, 0x04(%rsp)	# save the state of ebp
	leal	0x48(%rsp), %ebp	# save the state of esp before the interrupt
	movl	%ebp, 0x00(%rsp)
	movl	0x10(%rsp), %ebp	# save the vector number
	movl	%ebp, 0x08(%rsp)
	movl	0x18(%rsp), %ebp	# save the error code
	movl	%ebp, 0x0c(%rsp)
	movl	0x20(%rsp), %ebp	# save the state of eip
	movl	%ebp, 0x10(%rsp)
	movl	0x28(%rsp), %ebp	# save the state of cs
	movl	%ebp, 0x14(%rsp)
	movl	0x30(%rsp), %ebp	# save the state of eflags
	movl	%ebp, 0x18(%rsp)
	leal	int_handler(%rip), %ebp
	movw	$KERNEL_CS, -2(%rsp)
	movl	%ebp, -6(%rsp)
	lcall	*-6(%rsp)
	movl	0x04(%rsp), %ebp	# restore the saved state of ebp
	addq	$32, %rsp		# discard the stack frame we created
	iretq

# Individual interrupt vector handlers for protected mode. These need to be
# spaced equally, to allow the IDT initialisation loop above to work, so we
# use noops to pad out where required.

	.code32

vec32_0:
	pushl	$0	# error code
	pushl	$0	# vector
	jmp	int_handler32

vec32_1:
	pushl	$0	# error code
	pushl	$1	# vector
	jmp	int_handler32

vec32_2:
	pushl	$0	# error code
	pushl	$2	# vector
	jmp	int_handler32

vec32_3:
	pushl	$0	# error code
	pushl	$3	# vector
	jmp	int_handler32

vec32_4:
	pushl	$0	# error code
	pushl	$4	# vector
	jmp	int_handler32

vec32_5:
	pushl	$0	# error code
	pushl	$5	# vector
	jmp	int_handler32

vec32_6:
	pushl	$0	# error code
	pushl	$6	# vector
	jmp	int_handler32

vec32_7:
	pushl	$0	# error code
	pushl	$7	# vector
	jmp	int_handler32

vec32_8:
	nop;nop 	# error code already provided
	pushl	$8	# vector
	jmp	int_handler32

vec32_9:
	pushl	$0	# error code
	pushl	$9	# vector
	jmp	int_handler32

vec32_10:
	nop;nop 	# error code already provided
	pushl	$10	# vector
	jmp	int_handler32

vec32_11:
	nop;nop 	# error code already provided
	pushl	$11	# vector
	jmp	int_handler32

vec32_12:
	nop;nop 	# error code already provided
	pushl	$12	# vector
	jmp	int_handler32

vec32_13:
	nop;nop 	# error code already provided
	pushl	$13	# vector
	jmp	int_handler32

vec32_14:
	nop;nop 	# error code already provided
	pushl	$14	# vector
	jmp	int_handler32

vec32_15:
	pushl	$0	# error code
	pushl	$15	# vector
	jmp	int_handler32

vec32_16:
	pushl	$0	# error code
	pushl	$16	# vector
	jmp	int_handler32

vec32_17:
	nop;nop 	# error code
	pushl	$17	# vector
	jmp	int_handler32

vec32_18:
	pushl	$0	# error code
	pushl	$18	# vector
	jmp	int_handler32

vec32_19:
	pushl	$0	# error code
	pushl	$19	# vector
	jmp	int_handler32

# The interrupt handler code for protected mode. Pass the register state to
# the common interrupt handler. On entry this expects the stack to contain:
#
#	esp+10	eflags
#	esp+0c	cs
#	esp+08	eip
#	esp+04	error code
#	esp+00	vector number
#
# It adds the additional state expected by int_handler to the bottom of the
# stack frame.

int_handler32:
	pushl	%ebp		# save the state of ebp
	leal	24(%esp), %ebp	# save the state of esp before the interrupt
	pushl	%ebp
	leal	int_handler@GOTOFF(%ebx), %ebp
	movw	$KERNEL_CS, -2(%esp)
	movl	%ebp, -6(%esp)
	lcall	*-6(%esp)
	popl	%ebp		# discard the saved state of esp
	popl	%ebp		# restore the saved state of ebp
	addl	$8, %esp	# discard the vector number and error code
	iret

# The common interrupt handler code. Pass the register state to the application
# interrupt handler. On entry this expects the stack to contain:
#
#	esp+18	eflags
#	esp+14	cs
#	esp+10	eip
#	esp+0c	error code
#	esp+08	vector number
#	esp+04	ebp
#	esp+00	esp
#
# It adds the additional state expected by the application to the bottom of the
# stack frame.

int_handler:
	pushl	%esi
	pushl	%edi
	pushl	%edx
	pushl	%ecx
	pushl	%ebx
	pushl	%eax
	pushl	%ss
	pushl	%es
	pushl	%ds
	pushl	%esp		# pointer to trap regs struct on the stack
	cld
	call	interrupt
	addl	$16, %esp
	popl	%eax
	popl	%ebx
	popl	%ecx
	popl	%edx
	popl	%edi
	popl	%esi
	lret

# The interrupt descriptor table, used for both long mode and protected mode.

	.align	4
	.word	0			# for alignment
	.globl	idt_descr
idt_descr:
	.word	0			# size: filled in at run time
	.quad	0			# addr: filled in at run time

	.align	8
	.globl	idt
idt:
	.fill	2*NUM_INT_VEC, 8, 0	# filled in at run time
idt_end:

# The global descriptor table.

	.align	4
	.word	0			# for alignment
gdt_descr:
	.word	gdt_end - gdt - 1	# size
	.long	0			# addr: filled in at run time

	.align	4
	.globl	gdt
gdt:
	.quad	0x0000000000000000	# NULL descriptor
	.quad	0x00209a0000000000	# 0x08 64-bit code at 0x000000
	.quad	0x00cf9a000000ffff	# 0x10 main 4gb code at 0x000000
	.quad	0x00cf92000000ffff	# 0x18 main 4gb data at 0x000000

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

# The long mode level 4 page map table.

	.align	4096
	.globl	pml4
pml4:
	.long	pdp + 0x3		# relocated at run time
	.long	0

# Page Directory Pointer Table:
# 4 Entries, pointing to the Page Directory Tables.

	.align	4096
	.globl	pdp
pdp:
	.long	pd0 + 0x1		# relocated at run time
	.long	0
	.long	pd1 + 0x1		# relocated at run time
	.long	0
	.long	pd2 + 0x1		# relocated at run time
	.long	0
	.long	pd3 + 0x1		# relocated at run time
	.long	0

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

	# Load the startup address and use it to patch the jump address.

	movl	(ap_startup_addr - ap_trampoline), %edi
	movl	%edi, (ap_jump - ap_trampoline + 2)

	# Patch and load the GDT descriptor. It should point to the main
	# GDT descriptor, which has already been initialised by the BSP.

	movl	%edi, %eax
	addl	$(gdt - startup), %eax
	movl	%eax, (ap_gdt_descr - ap_trampoline + 2)
data32	lgdt	ap_gdt_descr - ap_trampoline

	# Switch to protected mode and reload the segment registers.

	movl	%cr0, %eax
	orl	$1, %eax
	movl	%eax, %cr0
	jmp	ap_flush
ap_flush:
	movw	$KERNEL_DS, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss

	# Jump to the main entry point with the startup address in %edi.
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
	.long	0

startup_mutex:
	.long	0

first_boot:
	.long	1

use_long_mode:
	.long	0

	.previous

# Startup stack.

	.bss
	.align	16

startup_stack_base:
	. = . + 64
startup_stack_top:

	.previous

# Main stack area.

	.section    "stacks", "aw", @progbits
	.align  16

	. = . + STACKS_SIZE

	.previous
