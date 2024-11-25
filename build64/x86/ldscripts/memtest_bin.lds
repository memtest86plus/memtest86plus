OUTPUT_FORMAT("binary")
OUTPUT_ARCH(i386:x86-64)

ENTRY(boot);
SECTIONS {
	. = 0;
	.bootsect : {
		*(.bootsect)
	}
	.setup : {
		*(.setup)
	}
	.memtest : {
		_start = . ;
		*(.data)
		_end = . ;
	}
	/DISCARD/ : { *(*) }

	_sys_size  = (_end - _start + 15) >> 4;
	_init_size = (_end - _start) + _bss_size;
}
