OUTPUT_FORMAT("elf64-loongarch")
OUTPUT_ARCH(loongarch);

ENTRY(startup64);

/* Explicit PHDRS so ld doesn't auto-merge the read-execute and read-write
   sections into a single RWX LOAD segment (LoongArch ld will warn otherwise).
   The output ELF is later objcopy'd to a flat binary, so these segments are
   purely cosmetic, but the warning is silenced and the layout is explicit. */
PHDRS {
	text    PT_LOAD    FLAGS(5);  /* R_E */
	data    PT_LOAD    FLAGS(6);  /* RW_ */
	dynamic PT_DYNAMIC FLAGS(6);
}

SECTIONS {
	. = 0;
	.text : {
		_start = .;
		*(.text)
		*(.text.*)
		*(.plt)
		_etext = . ;
	} :text = 0x00004003
	.rodata : {
		*(.rodata)
		*(.rodata.*)
	} :text
	.dynsym     : { *(.dynsym)   } :text
	.dynstr     : { *(.dynstr)   } :text
	.hash       : { *(.hash)     } :text
	.gnu.hash   : { *(.gnu.hash) } :text
	.shstrtab   : { *(.shstrtab) }

	.rela.text    : { *(.rela.text   .rela.text.*)   } :text
	.rela.rodata  : { *(.rela.rodata .rela.rodata.*) } :text
	.rela.data    : { *(.rela.data   .rela.data.*)   } :text
	.rela.got     : { *(.rela.got    .rela.got.*)    } :text
	.rela.plt     : { *(.rela.plt    .rela.plt.*)    } :text

	/* Page-align the boundary between read-only and writable sections.
	   PE SectionAlignment is 4096, so this ensures the split point
	   matches the PE virtual address layout exactly. */
	. = ALIGN(4096);
	_etext_ro = .;

	.data : {
		 _data = .;
		*(.data)
		*(.data.*)
	} :data
	.got : {
		*(.got.plt)
		*(.got)
	} :data
	.dynamic    : { *(.dynamic) } :data :dynamic
	_edata = . ;
	. = ALIGN(4);
	.bss : {
		_bss = .;
		*(.dynbss)
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(16);
		_stacks = .;
		*(.stacks)
		/* _end must be at least 256 byte aligned */
		. = ALIGN(256);
		_end = .;
	} :data
	/DISCARD/ : { *(*) }
}
