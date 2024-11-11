// SPDX-License-Identifier: GPL-2.0
//
// header64.S supports booting directly from a LoongArcht64 UEFI BIOS or via
// an intermediate bootloader that supports the Linux boot protocol. When booted
// directly from the BIOS, it provides the MS-DOS & PE/COFF headers.
//
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.
//

#define __ASSEMBLY__

#include "boot.h"
#include "peimage.h"

# The EFI loader loads the header at ImageBase, so we have to locate the main program
# after that. This means we can't load the main program at HIGH_LOAD_ADDR. Pick a load
# address well away from HIGH_LOAD_ADDR, to avoid overlap when relocating the code.

#define	IMAGE_BASE	0x200000

	.section ".header", "ax", @progbits

	.globl	head
head:
	# "MZ", the MS-DOS header signature.
	.byte 0x4d
	.byte 0x5a

    # The PE header pointer.
	.org	0x3c
	.long	pe_header

pe_header:
	.ascii	"PE"
	.short	0

coff_header:
	.short	IMAGE_FILE_MACHINE_LOONGARCH64		# Machine (LoongArch64)
	.short	3				# NumberOfSections
	.long	0 				# TimeDateStamp
	.long	0				# PointerToSymbolTable
	.long	0				# NumberOfSymbols
	.short	section_table - optional_header	# SizeOfOptionalHeader
	.short	IMAGE_FILE_DEBUG_STRIPPED	\
	      | IMAGE_FILE_LOCAL_SYMS_STRIPPED	\
	      | IMAGE_FILE_LINE_NUMS_STRIPPED	\
	      | IMAGE_FILE_EXECUTABLE_IMAGE	# Characteristics

optional_header:
	.short	IMAGE_NT_OPTIONAL_HDR64_MAGIC	# PE32+ format
	.byte	0x02				# MajorLinkerVersion
	.byte	0x14				# MinorLinkerVersion

	.long	_virt_text_size			# SizeOfCode
	.long	_virt_sbat_size			# SizeOfInitializedData
	.long	0				# SizeOfUninitializedData

	.long	_virt_text_start + 0x400	# AddressOfEntryPoint

	.long	_virt_text_start		# BaseOfCode

extra_header_fields:
	.quad	IMAGE_BASE		# ImageBase
	.long	4096			# SectionAlignment
	.long	512				# FileAlignment
	.short	0				# MajorOperatingSystemVersion
	.short	0				# MinorOperatingSystemVersion
	.short	0				# MajorImageVersion
	.short	0				# MinorImageVersion
	.short	0				# MajorSubsystemVersion
	.short	0				# MinorSubsystemVersion
	.long	0				# Win32VersionValue

	.long	_virt_img_size			# SizeOfImage
	.long	_file_head_size			# SizeOfHeaders
	.long	0				# CheckSum
	.short	10				# Subsystem (EFI application)
	.short	0				# DllCharacteristics
	.quad	0				# SizeOfStackReserve
	.quad	0				# SizeOfStackCommit
	.quad	0				# SizeOfHeapReserve
	.quad	0				# SizeOfHeapCommit
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
	.short	0				# NumberOfRelocations
	.short	0				# NumberOfLineNumbers
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
	.short	0				# NumberOfRelocations
	.short	0				# NumberOfLineNumbers
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
	.short	0				# NumberOfRelocations
	.short	0				# NumberOfLineNumbers
	.long	IMAGE_SCN_MEM_READ		\
	      | IMAGE_SCN_CNT_INITIALIZED_DATA	# Characteristics (section flags)

	.org	512

.section ".reloc"
	.long	0					// Page RVA
	.long	10					// Block Size (2*4+2)
	.short	(IMAGE_REL_BASED_ABSOLUTE << 12) + 0	// reloc 0 -> 0

.section ".sbat", "a", @progbits
.incbin "../boot/sbat.csv"
