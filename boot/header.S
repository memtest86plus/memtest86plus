// SPDX-License-Identifier: GPL-2.0
//
// header.S supports booting directly from a UEFI BIOS or via an intermediate
// bootloader that supports the Linux boot protocol. When booted directly from
// the BIOS, it provides the MS-DOS & PE/COFF headers. When using an intermediate
// bootloader, it provides the first few bytes of the Linux boot header (at the
// end of the boot sector), with the remainder of the header being provided by
// setup.S.
//
// Copyright (C) 2020-2023 Martin Whitaker.
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
#include "peimage.h"

# The EFI loader loads the header at ImageBase, so we have to locate the main program
# after that. This means we can't load the main program at HIGH_LOAD_ADDR. Pick a load
# address well away from HIGH_LOAD_ADDR, to avoid overlap when relocating the code.

#define	IMAGE_BASE	0x200000

	.section ".header", "ax", @progbits
	.code16

	.globl	boot
boot:
	# "MZ", the MS-DOS header signature.
	.byte 0x4d
	.byte 0x5a

        # In case we are booted by a legacy BIOS, print an error message.
        # Fortunately the MS-DOS header translates to harmless instructions.

	ljmp	$BOOT_SEG, $(error - boot)
error:
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	xorw	%sp, %sp
	sti
	cld

	movw	$error_msg, %si
0:	lodsb
	andb	%al, %al
	jz	wait
	movb	$0xe, %ah
	movw	$7, %bx
	int	$0x10
	jmp	0b

wait:
	# Allow the user to press a key, then reboot.
	xorw	%ax, %ax
	int	$0x16
	int	$0x19

	# int 0x19 should never return. In case it does, invoke the BIOS.
        # reset code.
	ljmp	$0xf000,$0xfff0

        # The PE header pointer.
	.org	0x3c
	.long	pe_header

error_msg:
	.ascii	"This is a UEFI bootable image\r\n"
	.ascii	"\n"
        .asciz  "Press any key to reboot\r\n"

pe_header:
	.ascii	"PE"
	.word 	0

coff_header:
#ifdef __x86_64__
	.word	IMAGE_FILE_MACHINE_X64		# Machine (x86-64)
#else
	.word	IMAGE_FILE_MACHINE_I386		# Machine (i386)
#endif
	.word	3				# NumberOfSections
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
	.long	_virt_sbat_size			# SizeOfInitializedData
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

	.org	512

.section ".reloc"
	.long	0					// Page RVA
	.long	10					// Block Size (2*4+2)
	.word	(IMAGE_REL_BASED_ABSOLUTE << 12) + 0	// reloc 0 -> 0

.section ".sbat", "a", @progbits
.incbin "../boot/sbat.csv"
