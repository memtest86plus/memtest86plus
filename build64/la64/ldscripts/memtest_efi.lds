OUTPUT_FORMAT("binary")
OUTPUT_ARCH(loongarch)

ENTRY(head);
SECTIONS {
	. = 0;
	.header : {
		*(.header)
	}
	. = ALIGN(4096);
	.text : {
		_file_text_start = . ;
		*(.data)
		_real_text_end = . ;
		. = ALIGN(512);
		_file_text_end = . ;
	}
	.reloc : {
		_file_reloc_start = . ;
		*(.reloc)
		_real_reloc_end = . ;
		. = ALIGN(512);
		_file_reloc_end = . ;
	}
	.sbat : {
		_file_sbat_start = . ;
		*(.sbat)
		_real_sbat_end = . ;
		. = ALIGN(512);
		_file_sbat_end = . ;
	}
	/DISCARD/ : { *(*) }

	_real_text_size  = _real_text_end  - _file_text_start;
	_real_reloc_size = _real_reloc_end - _file_reloc_start;
	_real_sbat_size  = _real_sbat_end  - _file_sbat_start;

	_file_head_size  = _file_text_start;
	_file_text_size  = _file_text_end  - _file_text_start;
	_file_reloc_size = _file_reloc_end - _file_reloc_start;
	_file_sbat_size  = _file_sbat_end  - _file_sbat_start;

	_sys_size  = (_real_text_size + 15) >> 4;
	_init_size = _real_text_size + _bss_size;

	_virt_head_size  = _file_head_size;
	_virt_text_size  = _init_size;
	_virt_reloc_size = _file_reloc_size;
	_virt_sbat_size  = _file_sbat_size;

	_virt_text_start  = _virt_head_size;
	_virt_reloc_start = _virt_text_start  + _virt_text_size;
	_virt_sbat_start  = _virt_reloc_start + _virt_reloc_size;

	_virt_img_size = _virt_sbat_start + _virt_sbat_size;
}
