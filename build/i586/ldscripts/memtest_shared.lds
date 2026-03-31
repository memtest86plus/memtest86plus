OUTPUT_FORMAT("elf32-i386");
OUTPUT_ARCH(i386);

ENTRY(startup32);
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

	.rel.text    : { *(.rel.text   .rel.text.*) }
	.rel.rodata  : { *(.rel.rodata .rel.rodata.*) }
	.rel.data    : { *(.rel.data   .rel.data.*) }
	.rel.got     : { *(.rel.got    .rel.got.*) }
	.rel.plt     : { *(.rel.plt    .rel.plt.*) }

	/* Page-align the boundary between read-only and writable sections.
	   PE SectionAlignment is 4096, so this ensures the split point
	   matches the PE virtual address layout exactly. */
	. = ALIGN(4096);
	_etext_ro = .;

	.dynamic    : { *(.dynamic) }

	. = ALIGN(4);
	.data : {
		 _data = .;
		*(.data)
		*(.data.*)
	}
	.got : {
		*(.got.plt)
		*(.got)
		_edata = . ;
	}
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
