// SPDX-License-Identifier: GPL-2.0
//
// mbr.S supports booting directly from the BIOS when a memtest binary image is
// stored on a hard disk or USB stick. When booted by the BIOS, it is loaded
// at address 0x7c00. It then loads the setup code immediately after itself
// (address 0x7e00) and the main program code at segment MAIN_SEG, using BIOS
// interrupts to read the data from disk. It locates the start of the memtest
// image containing the setup and main program code from the LBA stored at
// offset 0x1b0. The LBA value is assumed to be offset by 4, for compatibility
// with the xorrisofs --grub2-mbr option.
//
// The first 512B of the memtest binary image is not used, so either of the
// memtest.bin or memtest.efi images may be used.
//
// Copyright (C) 2020 Martin Whitaker.

#define __ASSEMBLY__

#include "boot.h"

	.section ".mbr", "ax", @progbits
	.code16

# The BIOS boot entry point. This will be located at 0x7c00.

	.globl	boot
boot:
	# Initialise the segment registers and the stack.

	ljmp	$BOOT_SEG, $init
init:
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movw	$BOOT_STACK_TOP, %ax
	movw	%ax, %sp

	# Check we have a valid drive number. If not, assume we are
	# booting from the first hard drive.

	testb	$0x80, %dl
	jz	0f
	testb	$0x70, %dl
	jz	1f
0:	movb	$0x80, %dl
1:

	# Load the setup code.

	movw	$SETUP_SECS, dap_length
	movw	$SETUP_SEG, dap_segment
	movl	image_base, %eax
	subl	$3, %eax
	movl	%eax, dap_lba_start
	movw	$dap, %si
	movb	$0x42, %ah
	int	$0x13
	jc	load_error

	# Print a message.

	pushw	%dx

	movb	$0x03, %ah		# read cursor pos
	xorb	%bh, %bh
	int	$0x10

	leaw	boot_msg, %bp
	movw	$(boot_msg_end - boot_msg), %cx
	movw	$0x0007, %bx		# page 0, attribute 7 (normal)
	movw	$0x1301, %ax		# write string, move cursor
	int	$0x10

	popw	%dx

	# Load the main test program.

	movw	$MAIN_SEG, dap_segment
	addl	$SETUP_SECS, dap_lba_start
	movw	$_sys_size, %cx		# length in 16B chunks
	addw	$31, %cx		# convert to sectors (rounding up)
	shrw	$5, %cx
0:	movw	$64, %bx		# load either 64 sectors or remaining
	cmpw	%bx, %cx		# length if less than 64 sectors
	ja	1f
	movw	%cx, %bx
1:	movw	%bx, dap_length
	movw	$dap, %si
	movb	$0x42, %ah
	int	$0x13
	jc	load_error
	addw	$0x800, dap_segment
	addl	$0x40, dap_lba_start
	subw	%bx, %cx
	jg	0b

	# Turn off the floppy drive motor.

	movw	$0x3f2, %dx
	xorb	%al, %al
	outb	%al, %dx

	# Turn off the text display cursor.

	movb	$0x01, %ah
	movb	$0x00, %bh
	movw	$0x2000, %cx
	int	$0x10

	# Fix up the Linux boot header to indicate we've loaded into low memory.

	movl	$LOW_LOAD_ADDR, 0x214

	# After that (everything loaded), we jump to the setup code loaded
	# directly after the boot block.

	ljmp	$SETUP_SEG, $0

load_error:
	movb	%ah, %al
	call	hex_to_ascii
	movb	%al, error_msg+1
	movb	%ah, %al
	shrb	$4, %al
	call	hex_to_ascii
	movb	%al, error_msg+0

	movb	$0x03, %ah		# read cursor pos
	xorb	%bh, %bh
	int	$0x10

	leaw	error_msg, %bp
	movw	$(error_msg_end - error_msg), %cx
	movw	$0x0007, %bx		# page 0, attribute 7 (normal)
	movw	$0x1301, %ax		# write string, move cursor
	int	$0x10

0:	hlt
	jmp	0b


hex_to_ascii:
	andb	$0xf, %al
	addb	$'0', %al
	cmpb	$'9', %al
	jle	1f
	addb	$('A' - '0' - 10), %al
1:	ret

# Local variables.

dap:
	.byte	0x10
	.byte	0
dap_length:
	.word	0
dap_offset:
	.word	0
dap_segment:
	.word	0
dap_lba_start:
	.quad	0

boot_msg:
	.ascii	"Loading Memtest86+\r\n"
boot_msg_end:

error_msg:
	.ascii	"   Disk read failed\r\n"
error_msg_end:

	.org	0x1b0
image_base:
	.quad	5		# default to sector 1, offset by 4

	.org	0x1fe
boot_flag:
	.word	0xAA55
