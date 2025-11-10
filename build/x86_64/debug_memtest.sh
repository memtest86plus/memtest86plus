#! /bin/bash

###############################################################################
#
# Description   : `debug_memtest.sh` is a script that allows memtest86plus
#       developers to set up a debugging environment with GDB in QEMU. It calls
#       the `make debug` target of a modified Makefile to create an additional
#       debug-symbol file called `memtest.debug`. The symbols of this file are
#       loaded with an offset (in accordance with the loading location of the
#       efi-image) into GDB to match the exact addresses of the symbols in memory.
#
#       For more detailed information, please read 'HOW_TO_DEBUG_WITH_GDB'
#
# Author        : Regina König
# Year          : 2022
# Email         : koenig_regina@arcor.de
#
##############################################################################

Help() {
    echo "Syntax: $0 [-h|-c|-t <terminal><execution_command>]"
    echo "options:"
    echo " -h   Print this help"
    echo " -c   Delete all debugging related files"
    echo " -t   Define an alternative terminal"
    echo "  You can define your own terminal inclusive its execution command via:"
    echo "      ./debug_script.sh -t \"<terminal> <execution_command> \""
    echo "  See following examples:"
    echo "      ./debug_script.sh -t \"x-terminal-emulator -e \""
    echo "      ./debug_script.sh -t \"gnome-terminal --  \""
    echo "      ./debug_script.sh -t \"xterm -e \""
}

Clear() {
    echo "Deleting files..."
    rm -rf hda-contents
    rm -f debug.log
    rm -f gdbscript
    rm -f QemuKill
    make clean
    rm -f OVMF.fd
    rm -f OVMF_VARS.fd
    rm -f OVMF_CODE.fd
    rm -f memtest_shared_debug.lds
}

while getopts ":hct:" option; do

    case $option in
        h) # display Help
            Help
            exit;;
        c) # clear directory
            Clear
            exit;;
        t) # define own terminal
            TERMINAL="$OPTARG"
            if ! $TERMINAL ls; then
                echo "Your entered command is not valid. Please check it again"
                echo "Or type \"./debug_memtest.sh -h\" for help"
                exit 1
            fi
            exit;;
        \?) # invalid option
            echo "Error: Invalid option"
            echo "Type $0 -h for more information"
            exit;;
    esac

done

Check() {
    # Check if QEMU and OVMF are installed
    if ! command -v qemu-system-x86_64 > /dev/null 2>&1; then
        echo "Qemu not installed"
        exit 1
    fi

    # Check for presence of OVMF.fd, OVMF_VARS.fd and OVMF_CODE.fd
    if [ ! -f OVMF.fd ] && [ ! -f /usr/share/ovmf/OVMF.fd ]; then
        echo "Package ovmf not installed. Type 'sudo apt install ovmf'."
        echo "Or copy your own versions of OVMF.fd, OVMF_VARS.fd and OVMF_CODE.fd into this directory"
        exit 1
    fi

    # Check if gdb is installed
    if ! command -v gdb > /dev/null 2>&1; then
        echo "GDB not installed"
        exit 1
    fi

    # Check for various terminals. Do not define TERMINAL if already defined by commandline
    if [ -z $TERMINAL ]; then
        if command -v x-terminal-emulator &> /dev/null; then
            echo "x-terminal-emulator found"
            TERMINAL="x-terminal-emulator -e "
        elif command -v gnome-terminal &> /dev/null; then
            echo "gnome-terminal found"
            TERMINAL="gnome-terminal -- "
        elif command -v xterm &> /dev/null; then
            echo "xterm found"
            TERMINAL="xterm -e "
        else
            echo "No terminal recognized. Please install x-terminal-emulator or gnome-terminal or xterm."
            echo "Or define your own terminal alternatively."
            echo "Type ./debug_memtest.sh -h for more information"
            exit 1
        fi
    fi
}

Make() {
    make debug DEBUG=1
    ret_val=$?

    if [[ $ret_val -ne 0 ]] ; then
        echo "Make failed with return value: $ret_val"
        exit 1
    fi
}


# Retrieve addresses from code
Get_Offsets() {
    IMAGEBASE=$(grep -P '#define\tIMAGE_BASE' ../../boot/x86/header.S | cut -f4)
    BASEOFCODE=$(grep -P '#define\tBASE_OF_CODE' ../../boot/x86/header.S | cut -f3)
    DATA=0x$(objdump -t memtest.debug | grep -w _data | cut -d" " -f1)

    # TODO: get BASEOFCODE and RELOCADDR (LOW_LOAD_LIMIT in app/main.c)
}

Init_Gdb() {

    QEMU="qemu-system-x86_64"
    QEMU_FLAGS=" -bios OVMF.fd"
    QEMU_FLAGS+=" -hda fat:rw:hda-contents -net none"
    QEMU_FLAGS+=" -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE.fd"
    QEMU_FLAGS+=" -drive if=pflash,format=raw,file=OVMF_VARS.fd"

    # Define offsets for loading of symbol-table
    Get_Offsets
    BASEOFCODE=0x1000
    RELOCADDR=0x400000

    printf -v OFFSET "0x%X" $(($IMAGEBASE + $BASEOFCODE))
    printf -v DATAOFFSET "0x%X" $(($IMAGEBASE + $BASEOFCODE + $DATA))
    printf -v RELOCDATA "0x%X" $(($RELOCADDR + $DATA))

    GDB_FILE="gdbscript"

    # Check if gdbscript exists. If not, create one.
    if [ ! -f $GDB_FILE ]
    then
        echo "Creating gdbscript.."

        echo "set pagination off" > $GDB_FILE

        if [ -z "$OFFSET" ] || [ -z "$RELOCADDR" ]; then
            echo "OFFSET or RELOCADDR is not set."
            exit 1
        fi

        echo "add-symbol-file memtest.debug $OFFSET -s .data $DATAOFFSET" >> $GDB_FILE
        echo "add-symbol-file memtest.debug $RELOCADDR -s .data $RELOCDATA" >> $GDB_FILE

        echo "b main" >> $GDB_FILE
        echo "commands" >> $GDB_FILE
        echo "layout src" >> $GDB_FILE
        echo "delete 1" >> $GDB_FILE
        echo "end" >> $GDB_FILE

        echo "b run_at" >> $GDB_FILE

        echo "shell sleep 0.5" >> $GDB_FILE
        echo "target remote localhost:1234" >> $GDB_FILE
        echo "info b" >> $GDB_FILE
        echo "c" >> $GDB_FILE
    fi
}

Init_Lds() {
    if [ ! -f ldscripts/memtest_shared.lds ]; then
        echo "'memtest_shared.lds' does not exist."
        exit 1
    fi

    sed '/DISCARD/d' ldscripts/memtest_shared.lds > memtest_shared_debug.lds

    if [ ! -f memtest_shared_debug.lds ]; then
        echo "Creation of 'memtest_shared_debug.lds' failed."
        exit 1
    fi
}

Prepare_Directory() {
    # Create dir hda-contents and a boot directory
    mkdir -p hda-contents/EFI/boot

    if [ ! -d hda-contents ]; then
        echo "Creation of directory hda-contents failed."
        exit 1
    fi

    # Copy mt86plus to hda-contents
    cp mt86plus hda-contents/mt86plus.efi
    cp mt86plus hda-contents/EFI/boot/BOOT_X64.efi

    # Copy OVMF* files from /usr/share
    if [ ! -f OVMF.fd ] || [ ! -f OVMF_VARS.fd ] || [ ! -f OVMF_CODE.fd ]; then
        cp /usr/share/ovmf/OVMF.fd .
        cp /usr/share/OVMF/OVMF_CODE.fd .
        cp /usr/share/OVMF/OVMF_VARS.fd .
    fi
}

# Global checks
Check

# Prepare linker script
Init_Lds

# Build
Make

# Prepare gdb script
Init_Gdb

# Create needed directories and move efi binary to appropriate location
Prepare_Directory

# Run QEMU and launch second terminal,
# wait for connection via gdb
$TERMINAL gdb -x $GDB_FILE &
$QEMU $QEMU_FLAGS -s -S
