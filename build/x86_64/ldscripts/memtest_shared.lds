OUTPUT_FORMAT("elf64-x86-64");
OUTPUT_ARCH(i386:x86-64);

ENTRY(startup64);
SECTIONS {
	. = 0;
	.text : {
		_start = .;
		*(.text)
		*(.text.*)
		*(.plt)
		_etext = . ;
	} = 0x9090
	.rodata : {
		*(.rodata)
		*(.rodata.*)
	}
	.dynsym     : { *(.dynsym) }
	.dynstr     : { *(.dynstr) }
	.hash       : { *(.hash) }
	.gnu.hash   : { *(.gnu.hash) }
	.shstrtab   : { *(.shstrtab) }

	.rela.text    : { *(.rela.text   .rela.text.*) }
	.rela.rodata  : { *(.rela.rodata .rela.rodata.*) }
	.rela.data    : { *(.rela.data   .rela.data.*) }
	.rela.got     : { *(.rela.got    .rela.got.*) }
	.rela.plt     : { *(.rela.plt    .rela.plt.*) }
	.rela.dyn     : { *(.rela.dyn    .rela.dyn.*) }

	/* Page-align the boundary between read-only and writable sections.
	   PE SectionAlignment is 4096, so this ensures the split point
	   matches the PE virtual address layout exactly. */
	. = ALIGN(4096);
	_etext_ro = .;

	.data : {
		 _data = .;
		*(.data)
		*(.data.*)
	}
	.got : {
		*(.got.plt)
		*(.got)
	}
	.dynamic    : { *(.dynamic) }
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
	}
	/DISCARD/ : { *(*) }
}
