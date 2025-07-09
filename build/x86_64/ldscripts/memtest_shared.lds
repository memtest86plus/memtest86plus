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
	.dynamic    : { *(.dynamic) }
	.shstrtab   : { *(.shstrtab) }

	.rela.text    : { *(.rela.text   .rela.text.*) }
	.rela.rodata  : { *(.rela.rodata .rela.rodata.*) }
	.rela.data    : { *(.rela.data   .rela.data.*) }
	.rela.plt     : { *(.rela.plt    .rela.plt.*) }
	.rela.got     : { *(.rela.got    .rela.got.*) }
	.got : {
		*(.got.plt)
		*(.got)
		_edata = . ;
	}
	.rela.dyn     : { *(.rela.dyn    .rela.dyn.*) }

	. = ALIGN(4);
	.data : {
		 _data = .;
		*(.data)
		*(.data.*)
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
