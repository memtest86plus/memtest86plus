OUTPUT_FORMAT("binary")
OUTPUT_ARCH("i386")

ENTRY(boot);
SECTIONS {
	. = 0;
	.mbr : {
		*(.mbr)
	}
	.memtest (NOLOAD) : {
		_start = . ;
		*(.data)
		_end = . ;
	}
	_sys_size = (_end - _start + 15) >> 4;
}
