// SPDX-License-Identifier: GPL-2.0
//
// setup.S collects the memory map information from the BIOS, disables APM,
// enables A20, and performs the switch from real mode to protected mode
// before jumping to the main program entry point.
//
// The memory map information is stored in the 4KB block of memory immediately
// following the setup code. The layout of the information matches the Linux
// boot_params struct. A pointer to this block is passed to the main program,
// for compatiblity with the Linux 32-bit boot protocol.
//
// Copyright (C) 2020-2021 Martin Whitaker.
//
// Derived from memtest86+ setup.S and head.S:
//
// 1-Jan-96 Modified by Chris Brady for use as a boot/loader for memtest-86.

#define __ASSEMBLY__

#include "boot.h"

#define	BOOT_PARAMS_START	(SETUP_SECS * 512)
#define	BOOT_PARAMS_END		(BOOT_PARAMS_START + 4096)

	.section ".setup", "ax", @progbits
	.code16

# Emulate the Linux boot header, to allow loading by other boot loaders.
# Indicate that the main program code should be loaded in high memory.
# bootsect.S will fix up the values if we are booted directly from the BIOS.

	.globl setup
setup:
	jmp	do_setup
header:
	.ascii	"HdrS"
version:
	.word	0x020c
realmode_swtch:
	.long	0
start_sys_seg:
	.word	0x1000
kernel_version:
	.word	0
type_of_loader:
	.byte	0
loadflags:
	.byte	0x1		# LOADED_HIGH
setup_move_size:
	.word	0
	.globl	code32_start
code32_start:
	.long	HIGH_LOAD_ADDR
ramdisk_image:
	.long	0
ramdisk_size:
	.long	0
bootsect_kludge:
	.long	0
heap_end_ptr:
	.word	0
ext_loader_ver:
	.byte	0
ext_loader_type:
	.byte	0
cmd_line_ptr:
	.long	0
initrd_addr_max:
	.long	0xffffffff
kernel_alignment:
	.long	4096
relocatable_kernel:
	.byte	0
min_alignment:
	.byte	12
xload_flags:
#ifdef __x86_64__
	.word	0x9		# XLF_KERNEL_64,XLF_EFI_HANDOVER_64
#else
	.word	0x4		# XLF_EFI_HANDOVER_32
#endif
cmd_line_size:
	.long	255
hardware_subarch:
	.long	0
hardware_subarch_data:
	.quad	0
payload_offset:
	.long	0
payload_length:
	.long	0
setup_data:
	.quad	0
pref_address:
	.quad	HIGH_LOAD_ADDR
init_size:
	.long	_init_size
handover_offset:
	.long	0x10

do_setup:
	# Reload the segment registers, except for the stack.

	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es

	# Get the memory map and disable APM.

	call	get_mem_info
	call	disable_apm

	# Disable interrupts.

	cli
	movb	$0x80, %al		# disable NMI
	outb	%al, $0x70

	# Enable A20.

	# Try to switch using the fast A20 gate.
	movw	$0x92, %dx
	inb	%dx, %al
	# Skip if it's unimplemented (read returns 0xff).
	cmpb	$0xff, %al
	jz	0f
	orb	$0x02, %al		# set the ALT_A20_GATE bit
	andb	$0xfe, %al		# clear the INIT_NOW bit
	outb	%al, %dx
0:
	# Use the keyboard controller method anyway.
	call	empty_8042
	movb	$0xd1, %al		# send write command
	outb	%al, $0x64
	call	empty_8042      
	movb	$0xdf, %al		# A20 on
	outb	%al, $0x60
	call	empty_8042

	# Set up a minimal GDT and IDT.

	xorl	%eax, %eax
	movw	%cs, %ax
	shll	$4, %eax
	addl	%eax, gdt_descr - setup + 2
	lgdt	gdt_descr - setup
	lidt	idt_descr - setup

	# Load a pointer to the boot_params block into ESI.

	xorl	%esi, %esi
	movw	%cs, %si
	shll	$4, %esi
	addl	$BOOT_PARAMS_START, %esi

	# Fix up the jump address.

	movl	(code32_start - setup), %eax
	movl	%eax, (jump - setup + 2)

	# Copy code32_start to the boot_params struct.

	movl	%eax, (BOOT_PARAMS_START + 0x214)

	# Copy cmd_line_ptr and cmd_line_size to the boot_params struct.

	movl	(cmd_line_ptr  - setup), %eax
	movl	%eax, (BOOT_PARAMS_START + 0x228)
	movl	(cmd_line_size - setup), %eax
	movl	%eax, (BOOT_PARAMS_START + 0x238)

	# Switch to protected mode.

	movl	%cr0, %eax
	orl	$1, %eax
	movl	%eax, %cr0
	jmp	flush
flush:
	# Reload the segment registers and jump to the main test program.

	movw	$KERNEL_DS, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movw	%ax, %fs
	movw	%ax, %gs
jump:
data32	ljmp	$KERNEL_CS, $0


# This subroutine queries the BIOS to determine the system memory map
# and stores the results in the boot_params structure that we pass to
# the startup code.

#define SMAP	0x534d4150

get_mem_info:
	push	%ds
	push	%es

	# Set DS and ES to point to the start of the boot_params structure.

	movw	%ds, %ax
	addw	$(BOOT_PARAMS_START >> 4), %ax
	movw	%ax, %ds
	movw	%ax, %es

	# Zero the entire boot_params structure.

	movw	$0x0000, %di
	movw	$0x0400, %cx
	xorl	%eax, %eax
	cld
	rep	stosl

	# First try method E820. E820 returns memory classified into a whole
	# bunch of different types, and allows memory holes and everything.

mem_e820:
	movw	$E820_MAP, %di		# destination pointer
	xorl	%ebx, %ebx		# continuation counter

loop_e820:
	movl	$0x0000e820, %eax	# e820, upper word zeroed
	movl	$SMAP, %edx		# ASCII 'SMAP'
	movl	$20, %ecx		# size of the e820 record
	int	$0x15			# make the call
	jc	done_e820		# bail out if it fails

	cmpl	$SMAP, %eax		# check the return is 'SMAP'
	jne	done_e820		# bail out if it fails

	incb	(E820_ENTRIES)
	addw	$E820_ENTRY_SIZE, %di

	movb	(E820_ENTRIES), %al	# check for table full
	cmpb	$E820_MAP_SIZE, %al
	je	done_e820

	cmpl	$0, %ebx		# any more entries?
	jne	loop_e820

done_e820:
	cmpb	$0, (E820_ENTRIES)
	jnz	get_mem_done

	# Next try method E801.

mem_e801:
	stc				# Fix to work around buggy BIOSs
	xorw	%cx,%cx 		# which don't clear/set carry on
	xorw	%dx,%dx 		# pass/error of e801h memory size
					# call or merely pass cx,dx through
					# without changing them.
	movw	$0xe801, %ax
	int	$0x15
	jc	mem_88

	cmpw	$0x0, %cx		# Kludge to handle BIOSes which
	jne	0f			# report their extended memory in
	cmpw	$0x0, %dx		# AX/BX rather than CX/DX. The spec
	jne	0f			# I have read seems to indicate that
	movw	%ax, %cx		# AX/BX are more reasonable anyway.
	movw	%bx, %dx
0:
	jmp	fake_e820

	# Finally try method 88.

mem_88:
	movb	$0x88, %ah
	int	$0x15
	movw	%ax, %cx
	movw	$0, %dx

fake_e820:
	# Write entry for memory below 1MB.
	movl	$0x0,      E820_ADDR(%di)
	movl	$0xa0000,  E820_SIZE(%di)
	movl	$1,        E820_TYPE(%di)
	incb	(E820_ENTRIES)
	addw	$E820_ENTRY_SIZE, %di

	# Write entry for memory between 1MB and 16MB.

	andl	$0xffff, %ecx		# convert to 32-bits
	jz	0f
	shll	$10, %ecx		# convert to bytes
	movl	$0x100000, E820_ADDR(%di)
	movl	%ecx,      E820_SIZE(%di)
	movl	$1,        E820_TYPE(%di)
	incb	(E820_ENTRIES)
	addw	$E820_ENTRY_SIZE, %di
0:
	# Write entry for memory above 16MB.

	andl	$0xffff, %edx		# convert to 32-bits
	jz	1f
	shll	$16, %edx		# convert to bytes
	movl	$0x1000000, E820_ADDR(%di)
	movl	%edx,       E820_SIZE(%di)
	movl	$1,         E820_TYPE(%di)
	incb	(E820_ENTRIES)
	addw	$E820_ENTRY_SIZE, %di
1:

get_mem_done:
	pop	%es
	pop	%ds
	ret

# This subroutine disables APM if it is present.

disable_apm:
	movw	$0x5300, %ax		# APM BIOS installation check
	xorw	%bx, %bx
	int	$0x15
	jc	disable_apm_done	# error -> no APM BIOS

	cmpw	$0x504d, %bx		# check for "PM" signature
	jne	disable_apm_done	# no signature -> no APM BIOS

	movw	$0x5304, %ax		# Disconnect first just in case
	xorw	%bx, %bx
	int	$0x15			# ignore return code

	movw	$0x5301, %ax		# Real Mode connect
	xorw	%bx, %bx
	int	$0x15
	jc	disable_apm_done	# error

	movw	$0x5308, %ax		# Disable APM
	mov	$0xffff, %bx
	xorw	%cx, %cx
	int	$0x15

disable_apm_done:
	ret

# This subroutine checks that the keyboard command queue is empty (after
# emptying the output buffers). No timeout is used - if this hangs there
# is something wrong with the machine, and we probably couldn't proceed
# anyway.

empty_8042:
	call	delay
	inb	$0x64, %al		# 8042 status port
	cmpb	$0xff, %al		# skip if not implemented
	jz	empty_8042_ret
	testb	$1, %al 		# anything in the output buffer?
	jz	no_output
	call	delay
	inb	$0x60, %al		# read it
	jmp	empty_8042
        
no_output:
	testb	$2, %al 		# is input buffer full?
	jnz	empty_8042		# yes - loop
empty_8042_ret:
	ret

# This subroutine provides a short delay.

delay:
	.word	0x00eb			# jmp $+2
	ret

# A minimal GDT and IDT.

	.align	4
gdt:
	.quad	0x0000000000000000	# NULL descriptor
	.quad	0x0000000000000000	# not used
	.quad	0x00c09a0000007fff	# 128MB 32-bit code at 0x000000
	.quad	0x00c0920000007fff	# 128MB 32-bit code at 0x000000
gdt_end:

	.word	0			# for alignment
gdt_descr:
	.word	gdt_end - gdt - 1	# gdt limit
	.long	gdt - setup		# gdt base - relocated at run time

	.word	0			# for alignment
idt_descr:
	.word	0			# idt limit=0
	.long	0			# idt base=0

# Pad to the declared size.

	.org	(SETUP_SECS*512)
