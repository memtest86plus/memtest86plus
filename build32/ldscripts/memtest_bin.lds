OUTPUT_FORMAT("binary")
OUTPUT_ARCH("i386")

ENTRY(boot);
SECTIONS {
	. = 0;
	.bootsect : { *(.bootsect) }
	.setup : { *(.setup) }
	.memtest : {
		_start = . ;
		*(.data)
		_end = . ;
	}
	_syssize = (_end - _start + 15) >> 4;
	_initsize = (_end - _start);
}
