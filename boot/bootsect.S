// SPDX-License-Identifier: GPL-2.0
//
// bootsect.S supports booting directly from the BIOS or via an intermediate
// bootloader that supports the Linux boot protocol. If booted directly from
// the BIOS, it is loaded at address 0x7c00. It then loads setup.S immediately
// after itself (address 0x7e00) and the main program code at segment MAIN_SEG,
// using BIOS interrupts to read the data from disk. When using an intermediate
// bootloader, it provides the first few bytes of the Linux boot header (at the
// end of the boot sector), with the remainder of the header being provided by
// setup.S.
//
// Copyright (C) 2020 Martin Whitaker.
//
// Derived from memtest86+ bootsect.S:
//
//	 bootsect.s		Copyright (C) 1991, 1992 Linus Torvalds
//
// 1-Jan-96 Modified by Chris Brady for use as a boot loader for MemTest-86.

#define __ASSEMBLY__

#include "boot.h"

	.section ".bootsect", "ax", @progbits
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

	# Many BIOS's default disk parameter tables will not recognize
	# multi-sector reads beyond the maximum sector number specified
	# in the default diskette parameter tables - this may mean 7
	# sectors in some cases.
	#
	# Since single sector reads are slow and out of the question,
	# we must take care of this by creating new parameter tables
	# (for the first disk) in RAM. We will set the maximum sector
	# count to 18 - the most we will encounter on an HD 1.44.
	#
	# High doesn't hurt. Low does.
	#
	# Segments are as follows:
	#	ds=es=ss=cs = BOOT_SEG,
	#	fs = 0, gs = parameter table segment

	pushw	$0
	popw	%fs
	movw	$0x78, %bx		# fs:bx is parameter table address
	lgs	%fs:(%bx),%si		# gs:si is source

	movw	%dx, %di		# es:di is destination
	movw	$6, %cx 		# copy 12 bytes
	cld

	rep	movsw %gs:(%si), (%di)

	movw	%dx, %di
	movb	$18, 4(%di)		# patch sector count

	movw	%di, %fs:(%bx)
	movw	%es, %fs:2(%bx)

	movw	%cs, %ax
	movw	%ax, %fs
	movw	%ax, %gs

	xorb	%ah, %ah		# reset FDC
	xorb	%dl, %dl
	int	$0x13

	# Load the setup sectors directly after the boot block.
	# Note that 'es' is already set up.

load_setup:
	xorw	%dx, %dx			# drive 0, head 0
	movw	$0x0002, %cx			# sector 2, track 0
	movw	$0x0200, %bx			# address = 512, in BOOT_SEG
	movw	$(0x0200 + SETUP_SECS), %ax	# service 2, nr of sectors
						# (assume all on head 0, track 0)
	int	$0x13				# read it
	jnc	load_setup_done 		# ok - continue

	pushw	%ax			# dump error code
	call	print_nl
	movw	%sp, %bp
	call	print_hex
	popw	%ax

	xorb	%dl, %dl		# reset FDC
	xorb	%ah, %ah
	int	$0x13
	jmp	load_setup

load_setup_done:

	# Get disk drive parameters, specifically number of sectors/track.
	# It seems that there is no BIOS call to get the number of sectors.
	# Guess 18 sectors if sector 18 can be read, 15 if sector 15 can be
	# read. Otherwise guess 9.

	xorw	%dx, %dx		# drive 0, head 0
	movw	$0x0012, %cx		# sector 18, track 0
	movw	$BOOT_STACK, %bx	# use the bottom of the stack (es = cs)
	movw	$0x0201, %ax		# service 2, 1 sector
	int	$0x13
	jnc	got_sectors
	movb	$0x0f, %cl		# sector 15
	movw	$0x0201, %ax		# service 2, 1 sector
	int	$0x13
	jnc	got_sectors
	movb	$0x09, %cl

got_sectors:
	movw	%cx, %cs:sectors
	movw	$BOOT_SEG, %ax
	movw	%ax, %es

	# Print a message.

	movb	$0x03, %ah		# read cursor pos
	xorb	%bh, %bh
	int	$0x10

	leaw	boot_msg, %bp
	movw	$(boot_msg_end - boot_msg), %cx
	movw	$0x0007, %bx		# page 0, attribute 7 (normal)
	movw	$0x1301, %ax		# write string, move cursor
	int	$0x10

	# Load the main test program.

	movw	$MAIN_SEG, %ax
	movw	%ax, %es
	call	read_it
	call	kill_motor
	call	turn_off_cursor
	call	print_nl

	# Fix up the Linux boot header to indicate we've loaded into low memory.

	movl	$LOW_LOAD_ADDR, code32_start

	# After that (everything loaded), we jump to the setup code loaded
	# directly after the boot block.

	ljmp	$SETUP_SEG, $0

# This subroutine loads the system at address 0x10000, making sure no 64KB
# boundaries are crossed. We try to load it as fast as possible, loading
# whole tracks whenever we can.
#
# in:	es - starting address segment (normally 0x1000)
#
sread:	.word 1 + SETUP_SECS		# sectors read of current track
head:	.word 0 			# current head
track:	.word 0 			# current track

read_it:
	movw	%es, %ax
	testw	$0x0fff, %ax
die:
	jne	die			# es must be at 64kB boundary
	xorw	%bx,%bx 		# bx is starting address within segment
rp_read:
	movw	%es, %ax
	subw	$MAIN_SEG, %ax		# have we loaded all yet?
	cmpw	sys_size, %ax
	jbe	ok1_read
	ret
ok1_read:
	movw	%cs:sectors, %ax
	subw	sread, %ax
	movw	%ax, %cx
	shlw	$9, %cx
	addw	%bx, %cx
	jnc	ok2_read
	je	ok2_read
	xorw	%ax, %ax
	subw	%bx, %ax
	shrw	$9, %ax
ok2_read:
	call	read_track
	movw	%ax, %cx
	add	sread, %ax
	cmpw	%cs:sectors, %ax
	jne	ok3_read
	movw	$1, %ax
	subw	head, %ax
	jne	ok4_read
	incw	track
ok4_read:
	movw	%ax, head
	xorw	%ax, %ax
ok3_read:
	movw	%ax, sread
	shlw	$9, %cx
	addw	%cx, %bx
	jnc	rp_read
	movw	%es, %ax
	addb	$0x10, %ah
	movw	%ax, %es
	xorw	%bx, %bx
	jmp	rp_read

read_track:
	pusha
	pusha
	movw	$0xe2e, %ax		# loading... message 2e = .
	movw	$7, %bx
	int	$0x10
	popa

	movw	track, %dx
	movw	sread, %cx
	incw	%cx
	movb	%dl, %ch
	movw	head, %dx
	movb	%dl, %dh
	andw	$0x0100, %dx
	movb	$2, %ah

	pushw	%dx			# save for error dump
	pushw	%cx
	pushw	%bx
	pushw	%ax

	int	$0x13
	jc	bad_rt
	addw	$8, %sp
	popa
	ret

bad_rt:
	pushw	%ax			# save error code
	call	print_all		# ah = error, al = read

	xorb	%ah, %ah
	xorb	%dl, %dl
	int	$0x13

	addw	$10, %sp
	popa
	jmp read_track

# This subroutine is for debugging purposes. It will print out all of the
# registers.  The assumption is that this is called from a routine, with a
# stack frame like:
#	dx
#	cx
#	bx
#	ax
#	err
#	ret <- sp

print_all:
	movw	$5, %cx 		# error code + 4 registers
	movw	%sp, %bp

print_loop:
	pushw	%cx			# save count left
	call	print_nl		# nl for readability

	cmpb	5, %cl			# see if register name is needed
	jae	no_reg

	movw	$(0xe05 + 'A' - 1), %ax
	subb	%cl, %al
	int	$0x10
	movb	$'X', %al
	int	$0x10
	movb	$':', %al
	int	$0x10

no_reg:
	addw	$2, %bp 		# next register
	call	print_hex		# print it
	popw	%cx
	loop	print_loop
	ret

print_nl:
	movw	$0xe0d, %ax		# CR
	int	$0x10
	movb	$0x0a, %al		# LF
	int	$0x10
	ret

# This subroutine is for debugging purposes, and prints the word pointed to
# by ss:bp in hexadecimal.

print_hex:
	movw	$4, %cx 		# 4 hex digits
	movw	(%bp), %dx		# load word into dx

print_digit:
	rolw	$4, %dx 		# rotate so that lowest 4 bits are used
	movb	$0xe, %ah
	movb	%dl, %al		# mask off so we have only next nibble
	andb	$0xf, %al
	addb	$'0', %al		# convert to 0-based digit
	cmpb	$'9', %al		# check for overflow
	jbe	good_digit
	addb	$('A' - '0' - 10), %al

good_digit:
	int	$0x10
	loop	print_digit
	ret

# This subroutine turns off the floppy drive motor, so that we enter the
# kernel in a known state, and don't have to worry about it later.

kill_motor:
	pushw	%dx
	movw	$0x3f2, %dx
	xorb	%al, %al
	outb	%al, %dx
	popw	%dx
	ret

# This subroutine turns off the text display cursor.

turn_off_cursor:
	movb	$0x01, %ah
	movb	$0x00, %bh
	movw	$0x2000, %cx
	int	$0x10
	ret

# Local variables.

sectors:
	.word	0

boot_msg:
	.ascii	"Loading Memtest86+"
boot_msg_end:

# Emulate the Linux boot header, to allow loading by intermediate boot loaders.

	.org	497
setup_sects:
	.byte	SETUP_SECS
root_flags:
	.word	0
sys_size:
	.long	_sys_size
ram_size:
	.word	0
vid_mode:
	.word	0
root_dev:
	.word	0
boot_flag:
	.word	0xAA55
