OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)

ENTRY(head);

/* This ELF is only a staging container that gets objcopy'd to the flat PE
   image; the PE section table in boot/aarch64/header.S is what defines the
   run-time permissions. Explicit PHDRS keep the staging segments W^X so ld
   doesn't warn about an RWX LOAD segment. */
PHDRS {
	header  PT_LOAD FLAGS(4);  /* R__ */
	text    PT_LOAD FLAGS(5);  /* R_E */
	data    PT_LOAD FLAGS(6);  /* RW_ */
	rodata  PT_LOAD FLAGS(4);  /* R__ */
}

SECTIONS {
	. = 0;
	.header : {
		*(.header)
	} :header
	. = ALIGN(4096);
	.text : {
		_file_text_start = . ;
		*memtest_text*(.data)
		_real_text_end = . ;
		. = ALIGN(512);
		_file_text_end = . ;
	} :text
	.data : {
		_file_data_start = . ;
		*memtest_data*(.data)
		_real_data_end = . ;
		. = ALIGN(512);
		_file_data_end = . ;
	} :data
	.reloc : {
		_file_reloc_start = . ;
		*(.reloc)
		_real_reloc_end = . ;
		. = ALIGN(512);
		_file_reloc_end = . ;
	} :rodata
	.sbat : {
		_file_sbat_start = . ;
		*(.sbat)
		_real_sbat_end = . ;
		. = ALIGN(512);
		_file_sbat_end = . ;
	} :rodata
	.osrel : {
		_file_osrel_start = . ;
		*(.osrel)
		_real_osrel_end = . ;
		. = ALIGN(512);
		_file_osrel_end = . ;
	} :rodata
	.shstrtab : { *(.shstrtab) }
	/DISCARD/ : { *(*) }

	_real_text_size  = _real_text_end  - _file_text_start;
	_real_data_size  = _real_data_end  - _file_data_start;
	_real_reloc_size = _real_reloc_end - _file_reloc_start;
	_real_sbat_size  = _real_sbat_end  - _file_sbat_start;
	_real_osrel_size = _real_osrel_end - _file_osrel_start;

	_file_head_size  = _file_text_start;
	_file_text_size  = _file_text_end  - _file_text_start;
	_file_data_size  = _file_data_end  - _file_data_start;
	_file_reloc_size = _file_reloc_end - _file_reloc_start;
	_file_sbat_size  = _file_sbat_end  - _file_sbat_start;
	_file_osrel_size = _file_osrel_end - _file_osrel_start;

	_sys_size  = (_real_text_size + _real_data_size + 15) >> 4;
	_init_size = _real_text_size + _real_data_size + _bss_size;

	_virt_head_size  = ((_file_head_size  + 4095) >> 12) << 12;
	_virt_text_size  = ((_real_text_size   + 4095) >> 12) << 12;
	_virt_data_size  = ((_real_data_size + _bss_size + 4095) >> 12) << 12;
	_virt_reloc_size = ((_file_reloc_size + 4095) >> 12) << 12;
	_virt_sbat_size  = ((_file_sbat_size  + 4095) >> 12) << 12;
	_virt_osrel_size = ((_file_osrel_size + 4095) >> 12) << 12;

	_virt_text_start  = _virt_head_size;
	_virt_data_start  = _virt_text_start  + _virt_text_size;
	_virt_reloc_start = _virt_data_start  + _virt_data_size;
	_virt_sbat_start  = _virt_reloc_start + _virt_reloc_size;
	_virt_osrel_start = _virt_sbat_start  + _virt_sbat_size;

	_virt_img_size = _virt_osrel_start + _virt_osrel_size;

	_virt_init_size = _virt_data_size + _virt_reloc_size + _virt_sbat_size + _virt_osrel_size;
}
