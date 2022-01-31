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

#define NUM_INT_VEC	20

	.text
	.code32

# The Linux 32-bit boot entry point.

	.globl	startup32
startup32:
	cld
	cli
	jmp	first_start

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

	# Fall though to the shared 32-bit entry point with the boot
	# params pointer in %esi...

	movl	%eax, %esi

	# ...and with the startup address in %edi.

first_start:
	movl	0x214(%esi), %ebx	# bootparams.code32_start
	leal	(startup - startup32)(%ebx), %edi

# The 32-bit entry point for AP boot and for restart after relocation.

	.globl	startup
startup:
	# Use the startup stack until we pick the correct one. We
	# need to take the mutex to protect our use of the stack.

	leal	(startup_stack_mutex - startup)(%edi), %eax
0: lock bts	$0, (%eax)
	jc	0b

	leal	(startup_stack_top - startup)(%edi), %esp

	# Load the GOT pointer.

	call	0f
0:	popl	%ebx
	addl	$_GLOBAL_OFFSET_TABLE_+[.-0b], %ebx

	# Save the boot params pointer (if first boot).

	cmpl	$1, first_boot@GOTOFF(%ebx)
	jnz	1f
	movl	%esi, boot_params_addr@GOTOFF(%ebx)
1:

	# Pick the correct stack. The stacks are allocated immediately
	# after the end of the loaded program, BSP first, then APs.

	call	smp_my_pcpu_num
	movl	$AP_STACK_SIZE, %edx
	mul	%edx
	addl	$BSP_STACK_SIZE, %eax
	leal	_end@GOTOFF(%ebx), %esp
	addl	%eax, %esp

	# Release the mutex that protects the startup stack.

	movl	$0, startup_stack_mutex@GOTOFF(%ebx)

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

	# Initialise the IDT.

	leal	idt@GOTOFF(%ebx), %edi
	leal	vec0@GOTOFF(%ebx), %esi
	movw	$NUM_INT_VEC, %cx
0:	movl	%esi, %edx
	movl	$(KERNEL_CS << 16), %eax
	movw	%dx, %ax		# selector = 0x0010 = cs
	movw	$0x8E00, %dx		# interrupt gate - dpl=0, present
	movl	%eax, (%edi)
	movl	%edx, 4(%edi)
	addl	$(vec1-vec0), %esi
	addl	$8, %edi
	dec	%cx
	jnz	0b

	# Initialise the IDT descriptor.

	leal	idt@GOTOFF(%ebx), %eax
	movl	%eax, 2 + idt_descr@GOTOFF(%ebx)

	# Load the IDT.

	lidt	idt_descr@GOTOFF(%ebx)

	# Zero the BSS (if first boot).

	cmpl	$1, first_boot@GOTOFF(%ebx)
	jnz	1f
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

	movl	%ebx, %edi		# ebx is overwritten by cpuid

	movl	$0x00000001, %eax	# test the PAE flag
	cpuid
	andl	$0x00000040, %edx
	jz	1f			# bail if not supported

	movl	%cr4, %eax		# enable PAE
	orl	$0x00000020, %eax
	movl	%eax, %cr4

	leal	pdp@GOTOFF(%edi), %eax	# set the page directory base address
	movl	%eax, %cr3

	# Enable long mode if supported.

	movl	$0x80000000, %eax	# check if function 0x80000001 is available
	cpuid
	cmpl	$0x80000001, %eax
	jb	0f			# bail if not supported

	mov	$0x80000001, %eax	# test the LM flag
	cpuid
	andl	$0x20000000, %edx
	jz	0f			# bail if not supported

	movl	$0xc0000080, %ecx	# enable long mode
	rdmsr
	orl	$0x00000100, %eax
	wrmsr

	leal	pml4@GOTOFF(%edi), %eax # set the page directory base address
	movl	%eax, %cr3

	# Enable paging.

0:	movl	%cr0, %eax
	orl	$0x80000000, %eax
	movl	%eax, %cr0

1:	movl	%edi, %ebx

	# Run the application.

	call	main

	# In case we return, simulate an exception.

	pushfl
	pushl	%cs
	call	0f
0:	pushl	$0	# error code
	pushl	$257	# vector
	jmp	int_handler

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

# Individual interrupt vector handlers. These need to be spaced equally, to
# allow the IDT initialisation loop above to work, so we use noops to pad out
# where required.

vec0:
	pushl	$0	# error code
	pushl	$0	# vector
	jmp int_handler
vec1:
	pushl	$0	# error code
	pushl	$1	# vector
	jmp int_handler

vec2:
	pushl	$0	# error code
	pushl	$2	# vector
	jmp int_handler

vec3:
	pushl	$0	# error code
	pushl	$3	# vector
	jmp	int_handler

vec4:
	pushl	$0	# error code
	pushl	$4	# vector
	jmp	int_handler

vec5:
	pushl	$0	# error code
	pushl	$5	# vector
	jmp	int_handler

vec6:
	pushl	$0	# error code
	pushl	$6	# vector
	jmp	int_handler

vec7:
	pushl	$0	# error code
	pushl	$7	# vector
	jmp	int_handler

vec8:
	nop;nop 	# error code already provided
	pushl	$8	# vector
	jmp	int_handler

vec9:
	pushl	$0	# error code
	pushl	$9	# vector
	jmp int_handler

vec10:
	nop;nop 	# error code already provided
	pushl	$10	# vector
	jmp	int_handler

vec11:
	nop;nop 	# error code already provided
	pushl	$11	# vector
	jmp	int_handler

vec12:
	nop;nop 	# error code already provided
	pushl	$12	# vector
	jmp	int_handler

vec13:
	nop;nop 	# error code already provided
	pushl	$13	# vector
	jmp	int_handler

vec14:
	nop;nop 	# error code already provided
	pushl	$14	# vector
	jmp	int_handler

vec15:
	pushl	$0	# error code
	pushl	$15	# vector
	jmp	int_handler

vec16:
	pushl	$0	# error code
	pushl	$16	# vector
	jmp	int_handler

vec17:
	nop;nop 	# error code
	pushl	$17	# vector
	jmp	int_handler

vec18:
	pushl	$0	# error code
	pushl	$18	# vector
	jmp	int_handler

vec19:
	pushl	$0	# error code
	pushl	$19	# vector
	jmp	int_handler

# The common interrupt handler code. Pass the register state to the
# application interrupt handler.

int_handler:
	pushl	%eax
	pushl	%ebx
	pushl	%ecx
	pushl	%edx
	pushl	%edi
	pushl	%esi
	pushl	%ebp

	# original stack pointer
	leal	48(%esp), %eax
	pushl	%eax
	pushl	%ds
	pushl	%es
	pushl	%ss
	pushl	%esp # pointer to trap regs struct on the stack
	call	interrupt
	addl	$20, %esp

	popl	%ebp
	popl	%esi
	popl	%edi
	popl	%edx
	popl	%ecx
	popl	%ebx
	popl	%eax
	addl	$8, %esp
	iret

# The interrupt descriptor table.

	.align	4
	.word	0			# for alignment
idt_descr:
	.word	idt_end - idt - 1	# size
	.long	0			# addr: filled in at run time

idt:
	.fill	NUM_INT_VEC, 8, 0	# filled in at run time
idt_end:

# The global descriptor table.

	.word	0			# for alignment
gdt_descr:
	.word	gdt_end - gdt - 1	# size
	.long	0			# addr: filled in at run time

	.align	4
	.globl	gdt
gdt:
	.quad	0x0000000000000000	# NULL descriptor
	.quad	0x0000000000000000	# not used
	.quad	0x00cf9b000000ffff	# 0x10 main 4gb code at 0x000000
	.quad	0x00cf93000000ffff	# 0x18 main 4gb data at 0x000000

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
	lgdt	ap_gdt_descr - ap_trampoline

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

	# Jump to the main entry point.
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

startup_stack_mutex:
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
