#		How to debug mt86plus in QEMU with GDB

`tools/mtqemu.py debug` sets up a debugging environment with GDB in QEMU for the memtest86plus developers.
It calls the `make debug` target of the per-port Makefile (with `DEBUG=1`) to create an additional debug-symbol file called `memtest.debug`.
The symbols of this file are loaded with an offset (in accordance with the loading location of the efi-image) into GDB to match the exact addresses of the symbols in memory.


##	Prerequisites

* this approach was tested on Ubuntu 18.04 - 22.04
* qemu-system-<arch> and GDB must be installed (GDB with a host target; for the aarch64/loongarch64 ports use `gdb-multiarch` or the cross GDB, or pass `--gdb-cmd`)
* UEFI: ovmf must be installed; UEFI is the default for x86_64 debug, i586 debug always needs the ia32 OVMF32 firmware
* BIOS/floppy debugging is available on the x86 ports via `--bootmode floppy` (the symbol offsets only match once the image reaches its load location)

##	How to run

* navigate to the repo root
* run `python3 tools/mtqemu.py debug` (defaults: x86_64, UEFI + GDB on port 1234)
* or read `tools/mtqemu.py --help` / `tools/README.md` for the full option list

The script launches QEMU (paused via `-S`) and a GDB terminal — `x-terminal-emulator -e`, `gnome-terminal --` or `xterm -e` are auto-detected; override with `--terminal-cmd`. On a headless machine GDB is attached to the current terminal instead. All build/staging artifacts land in the selected `build/<arch>` directory, which is where `gdbscript` is generated too.

### Remarks - create own gdbscript

It is possible to provide an own gdb-script. Name it 'gdbscript' and place it in the build/<arch> directory (e.g. `build/x86_64/gdbscript`).
This script will automatically be loaded when gdb starts (the script is only generated when it does not exist yet).
!! But be careful when cleaning the directory by `python3 tools/mtqemu.py debug --clean`. It also removes 'gdbscript'.

##	Navigate inside Qemu/UEFI

* wait until UEFI-Shell is loaded
* type "fs0:" - Enter
* type "mt86plus.efi" - Enter

###	Inside GDB

When GDB is running, it stops at the first breakpoint at main(). Feel free to add further breakpoints or continue with `c`.

###	Remarks - auto-boot memtest86+

In step **Navigate inside QEMU/UEFI**, you have to navigate to the directory which contains mt86plus.efi and manually launch it.

If you want to automatically boot from mt86plus.efi, there is an additional step required to add memtest to the first place at the bootorder:

When the UEFI Shell is running, type
		`bcfg boot add 0 FS0:\EFI\boot\BOOT_X64.efi "memtest"`
and confirm with Enter.
The directory "\EFI\boot" and the file "BOOT_X64.efi" are automatically
created by mtqemu.py (in `build/<arch>/hda-contents`).

You can add memtest command line parameters as "optional data" to the boot entry created above, e.g. for `usbdebug`:
		`bcfg boot -opt 0x0 ^"usbdebug^"`
Note the UEFI-shell escaped quotes. Alternatively you can create a UTF-16 file with the parameters, e.g. with `edit params.txt` and add its contents:
		`bcfg boot -opt 0x0 FS0:\params.txt`

When you run mtqemu.py debug the next time, mt86plus.efi should run without
previous user interaction.

!! But be careful when cleaning the directory by `python3 tools/mtqemu.py debug --clean`. It also removes this setting.
!! Make sure that you have made a copy of 'OVMF*'-files when running this command.

##	Clean directory

mtqemu.py has its own clean procedure which cleans additional files not mentioned in Makefile's
'make clean' target: `python3 tools/mtqemu.py debug --clean`. When you run this command, make sure that you have saved 'gdbscript' and/or OVMF* files if there are custom changes.

(Formerly, this was `./debug_memtest.sh -c` inside the build directory.)

##	Possible features/alternatives and further considerations

###	Detection of Image Base 

To assign the correct address for all debugging symbols, it is neccessary to add an offset to the values in memtest.debug (the file containing the debug symbols). This offset consists of the IMAGE_BASE and the BASE_OF_CODE.  
Both values are defined in `memtest86plus/boot/header.S` 

* IMMUTABILITY OF ALL CONDITIONS

if you assume, that these values will never change during the development phase of memtest86plus AND mt86plus.efi is always loaded at this preferred address in qemu-system-x86_64 (which seems to be the case) then it is possible to hardcode the offset in the script (for the implementation see debug_memtest_simple.sh)

* ADAPTABILITY TO DEVELOPMENT CHANGES

if there is a chance, that these values WILL change during the development phase but mt86plus.efi is always loaded at this preferred address then the value can be read from header.S by the debug script just right before starting the debugging (for an example, see debug_memtest_full.sh)

* EXPECTED ERRATIC BEHAVIOUR OF QEMU

If it is expected that mt86plus.efi is NOT always loaded at the same address, it is inevitable to determine the actual loading address first. This approach comprises a DEBUG-build of OVMF.fd (which requires the cloning of the whole edk2-repository and manually build OVMF.fd). With this DEBUG-version of OVMF.fd it is possible to write the loading addresses of all modules into a debug.log.
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
