OUTPUT_FORMAT("binary")
OUTPUT_ARCH(i386:x86-64)

ENTRY(boot);
SECTIONS {
	. = 0;
	.header : {
		*(.header)
	}
	.setup : {
		*(.setup)
	}
	. = ALIGN(512);
	.text : {
		_text_start = . ;
		*(.data)
		. = ALIGN(512);
		_text_end = . ;
	}

	_text_size = (_text_end  - _text_start);

	_sys_size  = _text_size >> 4;
	_init_size = _text_size;
}
