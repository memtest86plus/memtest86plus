#		How to debug memtest.efi in QEMU with GDB

`debug_memtest.sh` is a script that allows memtest86plus developers to set up a debugging environment with GDB in QEMU.  
It calls the `make debug` target of a modified Makefile to create an additional debug-symbol file called `memtest.debug`.  
The symbols of this file are loaded with an offset (in accordance with the loading location of the efi-image) into GDB to match the exact addresses of the symbols in memory.


##	Prerequisites

* this approach was tested on Ubuntu 18.04 - 22.04
* the debug script is created only for the efi 64-bit version
* qemu-system-x86_64 and ovmf must be installed

##	How to run

* navigate to build64 directory
* run `./debug_memtest.sh`
* or type `./debug_memtest.sh -h` for help

### Remarks - create own gdbscript

It is possible to provide an own gdb-script. Name it 'gdbscript' and place it in the build64 directory.
This script will automatically be loaded when gdb starts.
!! But be careful when cleaning the directory by './debug_memtest.sh -c'. It also removes 'gdbscript'.
!! Make sure that you have made a copy of 'gdbscript' when running this command.

##	Navigate inside Qemu/UEFI

* wait until UEFI-Shell is loaded
* type "fs0:" - Enter
* type "memtest.efi" - Enter

###	Inside GDB

When GDB is running, it stops at the first breakpoint at main(). Feel free to add further breakpoints or continue with `c`.

###	Remarks - auto-boot memtest86+

In step **Navigate inside QEMU/UEFI**, you have to navigate to the directory which contains memtest.efi and manually launch it.

If you want to automatically boot from memtest.efi, there is an additional step required to add memtest to the first place at the bootorder:

When the UEFI Shell is running, type
		`bcfg boot add 0 FS0:\EFI\boot\BOOT_X64.efi "memtest"`
and confirm with Enter.
The directory "\EFI\boot" and the file "BOOT_X64.efi" are automatically
created by the debug-script.

When you run the script the next time, memtest.efi should run without
previous user interaction.

!! But be careful when cleaning the directory by './debug_memtest.sh -c'. It also removes this setting.
!! Make sure that you have made a copy of 'OVMF*'-files when running this command.

##	Clean directory

'debug_memtest.sh' has an own clean procedure which cleans additional files not mentioned in Makefile's
'make clean' target. When you run this command, make sure that you have saved 'gdbscript' and/or OVMF* files if there are custom changes.

To clean the directory, type `./debug_memtest.sh -c`

##	Possible features/alternatives and further considerations

###	Detection of Image Base 

To assign the correct address for all debugging symbols, it is neccessary to add an offset to the values in memtest.debug (the file containing the debug symbols). This offset consists of the IMAGE_BASE and the BASE_OF_CODE.  
Both values are defined in `memtest86plus/boot/header.S` 

* IMMUTABILITY OF ALL CONDITIONS

if you assume, that these values will never change during the development phase of memtest86plus AND memtest.efi is always loaded at this preferred address in qemu-system-x86_64 (which seems to be the case) then it is possible to hardcode the offset in the script (for the implementation see debug_memtest_simple.sh)

* ADAPTABILITY TO DEVELOPMENT CHANGES

if there is a chance, that these values WILL change during the development phase but memtest.efi is always loaded at this preferred address then the value can be read from header.S by the debug script just right before starting the debugging (for an example, see debug_memtest_full.sh)

* EXPECTED ERRATIC BEHAVIOUR OF QEMU

If it is expected that memtest.efi is NOT always loaded at the same address, it is inevitable to determine the actual loading address first. This approach comprises a DEBUG-build of OVMF.fd (which requires the cloning of the whole edk2-repository and manually build OVMF.fd). With this DEBUG-version of OVMF.fd it is possible to write the loading addresses of all modules into a debug.log.
This proceeding has been tested successfully but is actually not implemented in one of the srcipts.

###	Handle relocation of memtest

memtest86plus relocates itself during the test procedures. As the script loads the symbol table with a given offset, debugging is only possible when the code is located at the original position. There are several ways to deal with relocation:

* IGNORE RELOCATION

Just ignore the fact that at a part of the time the symbols are not recognized by gdb as gdb has no information about the symbols, when memtest86plus has been relocated. It is still possible to debug the code since memtest86plus jumps sooner or later back to the original position and all precedures which are executed at one location are also executed at the other position.
BUT: If a bug is position-dependent (i.e. it occurs only at the relocated position), you are not able to debug it.

* DISABLE RELOCATION

TODO: Is it possible to deactivate relocation? E.g. by outcommenting some code or setting a flag? Does it have benefits over the first approach?

* FOLLOW RELOCATION

If the position after relocation is expected to be always the same, then you can just load the symbol table twice. This is done in debug_memtest_simple.sh (the offsets are 0x201000 and 0x400000). If the locations can vary then the offsets must be determined dynamically ... todo: how?