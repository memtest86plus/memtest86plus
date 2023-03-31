// SPDX-License-Identifier: GPL-2.0
//
// header.S supports booting directly from the 1) BIOS (FDD), 2) UEFI or 3) via
// an intermediate bootloader that supports the Linux boot protocol. All-in-one.
//
// 1) If booted directly from the BIOS, ".bootsect" gets loaded at address 0x7C00
// with DL register set to the boot drive number: 0x00 - FDD, 0x80 - first HDD.
// Unfortunately, when executing "MZ" signature as x86 instructions, the original
// value of DX becomes instantly and irrevocably lost, but this is fine as we only
// support booting from FDD anyway. Once started, the code immediately prints
// boot_msg - "Loading ". Next, it sets DX to 0x00 (as assumed by the code) and
// creates a new Floppy Disk Drive parameter table. Then, it tries to load
// ".setup" section immediately after itself (address 0x7E00) that contains
// setup code, Once successful, it prints memtest version and starts loading
// the main program code at address 0x10000. After each successful read,
// a dot - "." is printed. If read fails, error message gets printed and
// the read operation is restarted. At the end, 07E0h:0000h is called which
// immediately jumps into do_setup that handles switches CPU into protected mode
// and calls kernel entry point at 0x10000.
//
// 2) To support loading from the UEFI, it provides the MS-DOS & PE/COFF headers.
// The PE/COFF headers point to offset 0x10000 + 0x1E0 where efi_boot from
// startup32.S / startup64.S that handles EFI handover and the rest of booting.
//
// 3) When using an intermediate bootloader, it provides the first few bytes of
// the Linux boot header (at the end of the ".bootsect" section), with the
// remainder of the header and the setup code being provided in the ".setup".
// section.
//
// Copyright (C) 2020-2024 Martin Whitaker.
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
#include "build_version.h"
#include "peimage.h"
#include "build_version.h"

# The EFI loader loads the header at ImageBase, so we have to locate the main program
# after that. This means we can't load the main program at HIGH_LOAD_ADDR. Pick a load
# address well away from HIGH_LOAD_ADDR, to avoid overlap when relocating the code.

#define	IMAGE_BASE		0x200000
#define	BOOT_PARAMS_START	(SETUP_SECS * 512)
#define	BOOT_PARAMS_END		(BOOT_PARAMS_START + 4096)

	.section ".bootsect", "ax", @progbits
	.code16

	.globl	boot
boot:
	# "MZ", the MS-DOS header signature.
	# Fortunately it translates to mostly harmless x86 instructions.
	.byte 0x4d	# dec	%bp
	.byte 0x5a	# pop	%dx

	ljmp	$BOOT_SEG, $init

init:
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movw	$BOOT_STACK_TOP, %bp
	movw	%bp, %sp
	sti
	cld

	leaw	boot_msg, %bp
	movw	$(boot_msg_end - boot_msg), %cx
	call	print_string

	jmp	init_fdd

print_string:
	# Print string from %es:%bp of length %cx via BIOS routine int 10h: 1301h
	push	%cx
	movb	$0x03, %ah		# read cursor pos
	xorb	%bh, %bh		# page 0
	int	$0x10

	pop	%cx
	movb	$0x07, %bl		# (page 0 set above) attribute 7 (normal)
	movw	$0x1301, %ax		# write string, move cursor
	int	$0x10
	ret

	boot_msg:
	.ascii	"Loading "
	boot_msg_end:

	.org	0x3c
# The PE header pointer.
	.long	pe_header

init_fdd:
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

	xorw	%dx, %dx

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

	leaw	mt86plus_version, %bp
	movw	$(mt86plus_version_end - mt86plus_version), %cx
	call	print_string

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

	.org	497
# Emulate the Linux boot header, to allow loading by intermediate boot loaders.
# Indicate that the main program code should be loaded in high memory.
# The code from ".bootsect" will fix up the values if we are booted directly
# from the BIOS.

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

	.section ".setup", "ax", @progbits
	.globl setup
setup:
	jmp	do_setup

	.globl	header
header:
	.ascii	"HdrS"
version:
	.word	0x020c
realmode_swtch:
	.long	0
start_sys_seg:
	.word	0x1000
kernel_version:
	.word	mt86plus_version-512
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

// The code below collects the memory map information from the BIOS, disables
// APM, enables A20, and performs the switch from real mode to protected mode
// before jumping to the main program entry point.
//
// The memory map information is stored in the 4KB block of memory immediately
// following the setup code. The layout of the information matches the Linux
// boot_params struct. A pointer to this block is passed to the main program,
// for compatiblity with the Linux 32-bit boot protocol.

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

mt86plus_version:
	.ascii "Memtest86+ v" , MT_VERSION
mt86plus_version_end:
	.byte   0

pe_header:
	.ascii	"PE"
	.word 	0

coff_header:
#ifdef __x86_64__
	.word	IMAGE_FILE_MACHINE_X64		# Machine (x86-64)
#else
	.word	IMAGE_FILE_MACHINE_I386		# Machine (i386)
#endif
	.word	4				# NumberOfSections
	.long	0 				# TimeDateStamp
	.long	0				# PointerToSymbolTable
	.long	0				# NumberOfSymbols
	.word	section_table - optional_header	# SizeOfOptionalHeader
#ifdef __x86_64__
	.word	IMAGE_FILE_DEBUG_STRIPPED	\
	      | IMAGE_FILE_LOCAL_SYMS_STRIPPED	\
	      | IMAGE_FILE_LINE_NUMS_STRIPPED	\
	      | IMAGE_FILE_EXECUTABLE_IMAGE	# Characteristics
#else
	.word	IMAGE_FILE_32BIT_MACHINE	\
	      | IMAGE_FILE_DEBUG_STRIPPED	\
	      | IMAGE_FILE_LOCAL_SYMS_STRIPPED	\
	      | IMAGE_FILE_LINE_NUMS_STRIPPED	\
	      | IMAGE_FILE_EXECUTABLE_IMAGE	# Characteristics.
#endif

optional_header:
#ifdef __x86_64__
	.word	IMAGE_NT_OPTIONAL_HDR64_MAGIC	# PE32+ format
#else
	.word	IMAGE_NT_OPTIONAL_HDR32_MAGIC	# PE32 format
#endif
	.byte	0x02				# MajorLinkerVersion
	.byte	0x14				# MinorLinkerVersion

	.long	_virt_text_size			# SizeOfCode
	.long	_virt_osrel_size		# SizeOfInitializedData
	.long	0				# SizeOfUninitializedData

	.long	_virt_text_start + 0x1e0	# AddressOfEntryPoint

	.long	_virt_text_start		# BaseOfCode
#ifndef __x86_64__
	.long	_virt_sbat_start		# BaseOfData
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
	.word	0				# MajorImageVersion
	.word	0				# MinorImageVersion
	.word	0				# MajorSubsystemVersion
	.word	0				# MinorSubsystemVersion
	.long	0				# Win32VersionValue

	.long	_virt_img_size			# SizeOfImage
	.long	_file_head_size			# SizeOfHeaders
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
	.long	IMAGE_DIRECTORY_ENTRY_DEBUG	# NumberOfRvaAndSizes

	.long	0				# DataDirectory.Export.VirtualAddress
	.long	0				# DataDirectory.Export.Size
	.long	0				# DataDirectory.Import.VirtualAddress
	.long	0				# DataDirectory.Import.Size
	.long	0				# DataDirectory.Resource.VirtualAddress
	.long	0				# DataDirectory.Resource.Size
	.long	0				# DataDirectory.Exception.VirtualAddress
	.long	0				# DataDirectory.Exception.Size
	.long	0				# DataDirectory.Certs.VirtualAddress
	.long	0				# DataDirectory.Certs.Size
	.long	_virt_reloc_start		# DataDirectory.BaseReloc.VirtualAddress
	.long	_real_reloc_size		# DataDirectory.BaseReloc.Size

	# Section table
section_table:
	.ascii	".text"
	.byte	0
	.byte	0
	.byte	0
	.long	_virt_text_size			# VirtualSize
	.long	_virt_text_start		# VirtualAddress
	.long	_file_text_size			# SizeOfRawData
	.long	_file_text_start		# PointerToRawData
	.long	0				# PointerToRelocations
	.long	0				# PointerToLineNumbers
	.word	0				# NumberOfRelocations
	.word	0				# NumberOfLineNumbers
	.long	IMAGE_SCN_MEM_READ		\
	      | IMAGE_SCN_MEM_WRITE		\
	      | IMAGE_SCN_MEM_EXECUTE		\
	      | IMAGE_SCN_CNT_CODE		# Characteristics (section flags)

	.ascii	".reloc"
	.byte	0
	.byte	0
	.long	_virt_reloc_size		# VirtualSize
	.long	_virt_reloc_start		# VirtualAddress
	.long	_file_reloc_size		# SizeOfRawData
	.long	_file_reloc_start		# PointerToRawData
	.long	0				# PointerToRelocations
	.long	0				# PointerToLineNumbers
	.word	0				# NumberOfRelocations
	.word	0				# NumberOfLineNumbers
	.long	IMAGE_SCN_MEM_READ		\
	      | IMAGE_SCN_CNT_INITIALIZED_DATA	# Characteristics (section flags)

	.ascii	".sbat"
	.byte	0
	.byte	0
	.byte	0
	.long	_virt_sbat_size			# VirtualSize
	.long	_virt_sbat_start		# VirtualAddress
	.long	_file_sbat_size			# SizeOfRawData
	.long	_file_sbat_start		# PointerToRawData
	.long	0				# PointerToRelocations
	.long	0				# PointerToLineNumbers
	.word	0				# NumberOfRelocations
	.word	0				# NumberOfLineNumbers
	.long	IMAGE_SCN_MEM_READ		\
	      | IMAGE_SCN_CNT_INITIALIZED_DATA	# Characteristics (section flags)

	.ascii	".osrel"
	.byte	0
	.byte	0
	.long	_virt_osrel_size		# VirtualSize
	.long	_virt_osrel_start		# VirtualAddress
	.long	_file_osrel_size		# SizeOfRawData
	.long	_file_osrel_start		# PointerToRawData
	.long	0				# PointerToRelocations
	.long	0				# PointerToLineNumbers
	.word	0				# NumberOfRelocations
	.word	0				# NumberOfLineNumbers
	.long	IMAGE_SCN_MEM_READ		\
	      | IMAGE_SCN_CNT_INITIALIZED_DATA	# Characteristics (section flags)

# Pad to the declared size.
	.org	(SETUP_SECS*512)

.section ".reloc"
	.long	0					// Page RVA
	.long	10					// Block Size (2*4+2)
	.word	(IMAGE_REL_BASED_ABSOLUTE << 12) + 0	// reloc 0 -> 0

.section ".sbat", "a", @progbits
.incbin "../boot/sbat.csv"

.section ".osrel", "a", @progbits
	.ascii "ID=memtest86plus\nNAME=\"memtest86+\"\nVERSION=\""
	.ascii MT_VERSION
	.ascii "."
	.asciz GIT_HASH
	.ascii "\""
