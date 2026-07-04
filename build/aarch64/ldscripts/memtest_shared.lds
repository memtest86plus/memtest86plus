OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64);

ENTRY(startup64);

/* Explicit PHDRS so ld doesn't auto-merge the read-execute and read-write
   sections into a single RWX LOAD segment. The output ELF is later
   objcopy'd to a flat binary, so these segments are purely cosmetic, but
   any warning is silenced and the layout is explicit. */
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
	} :text = 0x00000000
	.rodata : {
		*(.rodata)
		*(.rodata.*)
	} :text
	.dynsym     : { *(.dynsym)   } :text
	.dynstr     : { *(.dynstr)   } :text
	.hash       : { *(.hash)     } :text
	.gnu.hash   : { *(.gnu.hash) } :text
	.shstrtab   : { *(.shstrtab) }

	.rela.dyn     : { *(.rela.dyn)                   } :text
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
	/* Define _DYNAMIC here so the sanity-check static link (which has no
	   dynamic section) still resolves the reference in reloc64.c. */
	.dynamic    : { _DYNAMIC = .; *(.dynamic) } :data :dynamic
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
