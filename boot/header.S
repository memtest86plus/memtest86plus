// SPDX-License-Identifier: GPL-2.0
//
// header.S supports booting directly from the BIOS/UEFI or via an intermediate
// bootloader that supports the Linux boot protocol. All-in-one.
//
// To support loading from the UEFI, it provides the MS-DOS & PE/COFF headers.
//
// When using an intermediate bootloader, it provides the first few bytes of
// the Linux boot header (at the end of the boot sector), with the remainder
// of the header being provided by setup.S.
//
// Finally, if booted directly from the BIOS, it gets loaded at address 0x7C00
// with DL register set to the boot drive number: 0x0 - FDD, 0x80 - first HDD.
// Once started, it creates a new Floppy Disk Drive parameter table and prints
// boot_msg - "Loading". Next it tries to load setup.S immediately after itself
// (address 0x7E00). Once succesfull, it prints " MT86+" and starts loading
// the main program code at segment MAIN_SEG (address 0x10000), using BIOS
// int 13h services to read the data from the disk. After each successfull read
// a dot (.) is printed. When read fails, "X" gets printed and read operation is
// restarted.
//
// Copyright (C) 2020 Martin Whitaker.
//
// Derived from Linux 5.6 arch/x86/boot/header.S:
//
//	Copyright (C) 1991, 1992 Linus Torvalds
//
//	Based on bootsect.S and setup.S
//	modified by more people than can be counted
//
//	Rewritten as a common file by H. Peter Anvin (Apr 2007)

#define __ASSEMBLY__

#include "boot.h"

# The EFI loader loads the header at ImageBase, so we have to locate the main program
# after that. This means we can't load the main program at HIGH_LOAD_ADDR. Pick a load
# address well away from HIGH_LOAD_ADDR, to avoid overlap when relocating the code.

#define	IMAGE_BASE	0x200000
#define	BASE_OF_CODE	0x1000

	.section ".header", "ax", @progbits
	.code16

# The BIOS boot entry point. This will be located at 0x7c00.

	.globl	boot
boot:
	# "MZ", the MS-DOS header signature. Fortunately it translates
	# to harmless x86 instructions.
	.byte 0x4d
	.byte 0x5a

	# Initialize the segment registers and the stack.

	ljmp	$BOOT_SEG, $init
init:
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss

	movw	$(BOOT_STACK_TOP - 12), %bp
	movw	%bp, %sp

	# Old BIOS's default disk parameter tables will not recognize
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
	#
	# The table will be stored at the stack, taking 12 bytes.

	xorw	%bx, %bx
	movw	%bx, %fs
	movb	$0x78, %bl
	lgs	%fs:(%bx),%si		# Load source addr from fs:bx to gs:si

	movw	%sp, %di		# Set es:di from ss:sp as the destination
	movw	$6, %cx 		# copy 12 bytes

	cld
	rep	movsw %gs:(%si), %es:(%di)

	movb	$18, %ss:4(%bp)		# patch sector count

	movw	%bp, %fs:(%bx)
	movw	%es, %fs:2(%bx)

	# Called with bh=0
	movb	$0x03, %ah		# Read cursor pos.
	int	$0x10

	jmp	boot_done		# That's all that fits here, continue...

sread: .word 1 + SETUP_SECS		# sectors read of the current track

	# The PE header pointer.
	.org	0x3c
	.long	pe_header


pe_header:
	.ascii	"PE"
	.word 	0

coff_header:
#ifdef __x86_64__
	.word	0x8664				# Machine (x86-64)
#else
	.word	0x14c				# Machine (i386)
#endif
	.word	1				# NumberOfSections
	.long	0 				# TimeDateStamp
	.long	0				# PointerToSymbolTable
	.long	0				# NumberOfSymbols
	.word	section_table - optional_header	# SizeOfOptionalHeader
#ifdef __x86_64__
	.word	0x20f				# Characteristics
						# IMAGE_FILE_DEBUG_STRIPPED |
						# IMAGE_FILE_LOCAL_SYMS_STRIPPED |
						# IMAGE_FILE_LINE_NUMS_STRIPPED |
						# IMAGE_FILE_EXECUTABLE_IMAGE |
						# IMAGE_FILE_RELOCS_STRIPPED
#else
	.word	0x30f				# Characteristics.
						# IMAGE_FILE_32BIT_MACHINE |
						# IMAGE_FILE_DEBUG_STRIPPED |
						# IMAGE_FILE_LOCAL_SYMS_STRIPPED |
						# IMAGE_FILE_LINE_NUMS_STRIPPED |
						# IMAGE_FILE_EXECUTABLE_IMAGE |
						# IMAGE_FILE_RELOCS_STRIPPED
#endif

optional_header:
#ifdef __x86_64__
	.word	0x20b 				# PE32+ format
#else
	.word	0x10b				# PE32 format
#endif
	.byte	0x02				# MajorLinkerVersion
	.byte	0x14				# MinorLinkerVersion

	.long	_text_size			# SizeOfCode
	.long	0				# SizeOfInitializedData
	.long	0				# SizeOfUninitializedData

	.long	BASE_OF_CODE + 0x1e0		# AddressOfEntryPoint

	.long	BASE_OF_CODE			# BaseOfCode
#ifndef __x86_64__
	.long	0				# data
#endif

extra_header_fields:
#ifdef __x86_64__
	.quad	IMAGE_BASE			# ImageBase
#else
	.long	IMAGE_BASE			# ImageBase
#endif
	.long	4096				# SectionAlignment
	.long	512				# FileAlignment
	.word	0				# MajorOperatingSystemVersion
	.word	0				# MinorOperatingSystemVersion
head:					# Also variable: current head
	.word	0				# MajorImageVersion
track:					# Also variable: current track
	.word	0				# MinorImageVersion
sectors:				# Also variable: sector count=18/15/9
	.word	0				# MajorSubsystemVersion
	.word	0				# MinorSubsystemVersion
	.long	0				# Win32VersionValue

	.long	BASE_OF_CODE + _init_size	# SizeOfImage
	.long	512				# SizeOfHeaders
	.long	0				# CheckSum
	.word	10				# Subsystem (EFI application)
	.word	0				# DllCharacteristics
#ifdef __x86_64__
	.quad	0				# SizeOfStackReserve
	.quad	0				# SizeOfStackCommit
	.quad	0				# SizeOfHeapReserve
	.quad	0				# SizeOfHeapCommit
#else
	.long	0				# SizeOfStackReserve
	.long	0				# SizeOfStackCommit
	.long	0				# SizeOfHeapReserve
	.long	0				# SizeOfHeapCommit
#endif
	.long	0				# LoaderFlags
	.long	0				# NumberOfRvaAndSizes

	# Section table
section_table:
	.ascii	".text"
	.byte	0
	.byte	0
	.byte	0
	.long	_text_size			# VirtualSize
	.long	BASE_OF_CODE			# VirtualAddress
	.long	_text_size			# SizeOfRawData
	.long	_text_start			# PointerToRawData
	.long	0				# PointerToRelocations
	.long	0				# PointerToLineNumbers
	.word	0				# NumberOfRelocations
	.word	0				# NumberOfLineNumbers
	.long	0x60500020			# Characteristics (section flags)

boot_done:
	# Print the message. Called with bh=0, and dh, dl already set.

	movw	$boot_msg, %bp
	movw	$(boot_msg_end - boot_msg), %cx
	movb	$0x07, %bl			# Attribute 7 (normal).
	movw	$0x1301, %ax			# Write string, move cursor.
	int	$0x10

load_setup:
	xorw	%dx, %dx			# dl=drive 0, dh=head 0 for below.
	xorb	%ah, %ah
	int	$0x13				# reset FDC

	# Load the setup sectors directly after the boot block.
	# Note that 'es' is already set up.

	movw	$0x0002, %cx			# sector 2, track 0
	movw	$0x0200, %bx			# address = 512, in BOOT_SEG
	movw	$(0x0200 + SETUP_SECS), %ax	# service 2, nr of sectors
						# (assume all on head 0, track 0)
	int	$0x13				# read it.
	jc	load_setup			# error - repeat

	xor	%bx, %bx
	movb	$0x03, %ah			# Read cursor pos.
	int	$0x10

	movw	$header_msg, %bp
	movw	$(header_msg_end - header_msg), %cx
	movb	$0x07, %bl			# Attribute 7 (normal).
	movw	$0x1301, %ax			# Write string, move cursor.
	int	$0x10

load_setup_done:
	# Get disk drive parameters, specifically number of sectors/track.
	# It seems that there is no BIOS call to get the number of sectors.
	# Guess 18 sectors if sector 18 can be read, 15 if sector 15 can be
	# read. Otherwise guess 9.

	movw	$MAIN_SEG, %ax
	movw	%ax, %es
	xorw	%bx, %bx 		# bx is starting address within segment

	xorw	%dx, %dx		# drive 0, head 0
	movw	$0x0012, %cx		# sector 18, track 0
	movw	$0x0201, %ax		# service 2, 1 sector
	int	$0x13
	jnc	got_sectors
	movb	$0x0f, %cl		# sector 15
	movw	$0x0201, %ax		# service 2, 1 sector
	int	$0x13
	jnc	got_sectors
	movb	$0x09, %cl

got_sectors:
	movw	%cx, %ds:sectors

	# Load the main test program.
	call	read_it

	# FDD motor will be switches off via floppy_off() called from
	# global_init().

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

read_it:
	# es:bx is already set to $MAIN_SEG:0 in load_setup_done

rp_read:
	movw	%es, %ax
	subw	$MAIN_SEG, %ax		# have we loaded all yet?
	cmpw	%ds:sys_size, %ax
	jbe	ok1_read
	ret
ok1_read:
	movw	%ds:sectors, %ax
	subw	%ds:sread, %ax
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
	add	%ds:sread, %ax
	cmpw	%ds:sectors, %ax
	jne	ok3_read
	movw	$1, %ax
	subw	head, %ax
	jne	ok4_read
	incw	track
ok4_read:
	movw	%ax, %ds:head
	xorw	%ax, %ax
ok3_read:
	movw	%ax, %ds:sread
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
	movw	$0x0e2e, %ax		# 0x2e = "."
read_track_recover:
	movw	$00007, %bx
	int	$0x10
	popa
	pusha

	movw	%ds:track, %dx
	movw	%ds:sread, %cx
	incw	%cx
	movb	%dl, %ch
	movw	%ds:head, %dx
	movb	%dl, %dh
	andw	$0x0100, %dx
	movb	$2, %ah

	int	$0x13
	jc	bad_rt
	popa
	ret
bad_rt:
	xorb	%ah, %ah
	int	$0x13			# Reset FDC, dl already points to the drive id

	movw	$0x0e58, %ax		# Leave 0x58 = "X" to be printed
	jmp	read_track_recover

boot_msg:
	.ascii "Loading"
boot_msg_end:

header_msg:
	.ascii " MT86+"
header_msg_end:

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
