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
    if [ "x$MACHINE" = "x" ] || [ "x$MEMSIZE" = "x" ]; then
        echo "Please set MACHINE and MEMSIZE"
        exit 1
    fi

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


# Retrieve addresses from code (not used in this version)
# Get_Offsets() {
# IMAGEBASE=$(grep -P '#define\tIMAGE_BASE' header.S | cut -f3)
# BASEOFCODE=$(grep -P '#define\tBASE_OF_CODE' header.S | cut -f3)

# TODO: get RELOCADDR and DATA
# }

Init() {

    QEMU="qemu-system-x86_64"
    QEMU_FLAGS=" -bios OVMF.fd"
    QEMU_FLAGS+=" -hda fat:rw:hda-contents -net none"
    QEMU_FLAGS+=" -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE.fd"
    QEMU_FLAGS+=" -drive if=pflash,format=raw,file=OVMF_VARS.fd"
#   QEMU_FLAGS+=" -machine q35 -acpitable file=../ACPI/RS904A/hpet.dat:../ACPI/RS904A/srat_32c.dat:../ACPI/RS904A/apic_32c.dat:../ACPI/RS904A/slit.dat -smbios file=../SPD/dmidecode_rs904a2_dump.bin"
#   QEMU_FLAGS+=" -machine q35 -acpitable file=../ACPI/RS904A/apic_32c.dat:../ACPI/RS904A/slit.dat -smbios file=../SPD/dmidecode_rs904a2_dump.bin"
    QEMU_FLAGS+=" -machine q35" # q35,accel=kvm

    if [ "x$MACHINE" = "x4S32CBulldozer" ]; then

        QEMU_FLAGS+=" -m ${MEMSIZE}M -cpu Opteron_G4" # -cpu host Opteron_G5
        QEMU_FLAGS+=" -smp 32,sockets=4,cores=8,maxcpus=32"

        MEMSIZE8=$((MEMSIZE / 8))
        QEMU_FLAGS+=" -object memory-backend-ram,size=${MEMSIZE8}M,id=m0,prealloc=on"
        QEMU_FLAGS+=" -object memory-backend-ram,size=${MEMSIZE8}M,id=m1,prealloc=on"
        QEMU_FLAGS+=" -object memory-backend-ram,size=${MEMSIZE8}M,id=m2,prealloc=on"
        QEMU_FLAGS+=" -object memory-backend-ram,size=${MEMSIZE8}M,id=m3,prealloc=on"
        QEMU_FLAGS+=" -object memory-backend-ram,size=${MEMSIZE8}M,id=m4,prealloc=on"
        QEMU_FLAGS+=" -object memory-backend-ram,size=${MEMSIZE8}M,id=m5,prealloc=on"
        QEMU_FLAGS+=" -object memory-backend-ram,size=${MEMSIZE8}M,id=m6,prealloc=on"
        QEMU_FLAGS+=" -object memory-backend-ram,size=${MEMSIZE8}M,id=m7,prealloc=on"

        QEMU_FLAGS+=" -numa node,nodeid=0,memdev=m0,cpus=0-3"
        QEMU_FLAGS+=" -numa node,nodeid=1,memdev=m1,cpus=4-7"
        QEMU_FLAGS+=" -numa node,nodeid=2,memdev=m2,cpus=8-11"
        QEMU_FLAGS+=" -numa node,nodeid=3,memdev=m3,cpus=12-15"
        QEMU_FLAGS+=" -numa node,nodeid=4,memdev=m4,cpus=16-19"
        QEMU_FLAGS+=" -numa node,nodeid=5,memdev=m5,cpus=20-23"
        QEMU_FLAGS+=" -numa node,nodeid=6,memdev=m6,cpus=24-27"
        QEMU_FLAGS+=" -numa node,nodeid=7,memdev=m7,cpus=28-31"

        QEMU_FLAGS+=" -numa dist,src=0,dst=1,val=16"
        QEMU_FLAGS+=" -numa dist,src=0,dst=2,val=16"
        QEMU_FLAGS+=" -numa dist,src=0,dst=3,val=22"
        QEMU_FLAGS+=" -numa dist,src=0,dst=4,val=16"
        QEMU_FLAGS+=" -numa dist,src=0,dst=5,val=22"
        QEMU_FLAGS+=" -numa dist,src=0,dst=6,val=16"
        QEMU_FLAGS+=" -numa dist,src=0,dst=7,val=22"

        QEMU_FLAGS+=" -numa dist,src=1,dst=0,val=16"
        QEMU_FLAGS+=" -numa dist,src=1,dst=2,val=16"
        QEMU_FLAGS+=" -numa dist,src=1,dst=3,val=22"
        QEMU_FLAGS+=" -numa dist,src=1,dst=4,val=22"
        QEMU_FLAGS+=" -numa dist,src=1,dst=5,val=16"
        QEMU_FLAGS+=" -numa dist,src=1,dst=6,val=22"
        QEMU_FLAGS+=" -numa dist,src=1,dst=7,val=16"

        QEMU_FLAGS+=" -numa dist,src=2,dst=0,val=16"
        QEMU_FLAGS+=" -numa dist,src=2,dst=1,val=16"
        QEMU_FLAGS+=" -numa dist,src=2,dst=3,val=16"
        QEMU_FLAGS+=" -numa dist,src=2,dst=4,val=16"
        QEMU_FLAGS+=" -numa dist,src=2,dst=5,val=22"
        QEMU_FLAGS+=" -numa dist,src=2,dst=6,val=16"
        QEMU_FLAGS+=" -numa dist,src=2,dst=7,val=16"

        QEMU_FLAGS+=" -numa dist,src=3,dst=0,val=22"
        QEMU_FLAGS+=" -numa dist,src=3,dst=1,val=22"
        QEMU_FLAGS+=" -numa dist,src=3,dst=2,val=16"
        QEMU_FLAGS+=" -numa dist,src=3,dst=4,val=22"
        QEMU_FLAGS+=" -numa dist,src=3,dst=5,val=16"
        QEMU_FLAGS+=" -numa dist,src=3,dst=6,val=16"
        QEMU_FLAGS+=" -numa dist,src=3,dst=7,val=16"

        QEMU_FLAGS+=" -numa dist,src=4,dst=0,val=16"
        QEMU_FLAGS+=" -numa dist,src=4,dst=1,val=22"
        QEMU_FLAGS+=" -numa dist,src=4,dst=2,val=16"
        QEMU_FLAGS+=" -numa dist,src=4,dst=3,val=22"
        QEMU_FLAGS+=" -numa dist,src=4,dst=5,val=16"
        QEMU_FLAGS+=" -numa dist,src=4,dst=6,val=16"
        QEMU_FLAGS+=" -numa dist,src=4,dst=7,val=22"

        QEMU_FLAGS+=" -numa dist,src=5,dst=0,val=22"
        QEMU_FLAGS+=" -numa dist,src=5,dst=1,val=16"
        QEMU_FLAGS+=" -numa dist,src=5,dst=2,val=22"
        QEMU_FLAGS+=" -numa dist,src=5,dst=3,val=16"
        QEMU_FLAGS+=" -numa dist,src=5,dst=4,val=16"
        QEMU_FLAGS+=" -numa dist,src=5,dst=6,val=16"
        QEMU_FLAGS+=" -numa dist,src=5,dst=7,val=22"

        QEMU_FLAGS+=" -numa dist,src=6,dst=0,val=16"
        QEMU_FLAGS+=" -numa dist,src=6,dst=1,val=22"
        QEMU_FLAGS+=" -numa dist,src=6,dst=2,val=16"
        QEMU_FLAGS+=" -numa dist,src=6,dst=3,val=16"
        QEMU_FLAGS+=" -numa dist,src=6,dst=4,val=16"
        QEMU_FLAGS+=" -numa dist,src=6,dst=5,val=16"
        QEMU_FLAGS+=" -numa dist,src=6,dst=7,val=16"

        QEMU_FLAGS+=" -numa dist,src=7,dst=0,val=22"
        QEMU_FLAGS+=" -numa dist,src=7,dst=1,val=16"
        QEMU_FLAGS+=" -numa dist,src=7,dst=2,val=16"
        QEMU_FLAGS+=" -numa dist,src=7,dst=3,val=16"
        QEMU_FLAGS+=" -numa dist,src=7,dst=4,val=22"
        QEMU_FLAGS+=" -numa dist,src=7,dst=5,val=22"
        QEMU_FLAGS+=" -numa dist,src=7,dst=6,val=16"

#[02Ch 0044   8]                 Locality   0 : 0A 10 10 16 10 16 10 16
#[034h 0052   8]                 Locality   1 : 10 0A 10 16 16 10 16 10
#[03Ch 0060   8]                 Locality   2 : 10 10 0A 10 10 16 10 10
#[044h 0068   8]                 Locality   3 : 16 16 10 0A 16 10 10 10
#[04Ch 0076   8]                 Locality   4 : 10 16 10 16 0A 10 10 16
#[054h 0084   8]                 Locality   5 : 16 10 16 10 10 0A 10 16
#[05Ch 0092   8]                 Locality   6 : 10 16 10 10 10 10 0A 10
#[064h 0100   8]                 Locality   7 : 16 10 10 10 16 16 10 0A

    elif [ "x$MACHINE" = "x2S12CWestmere" ]; then

        QEMU_FLAGS+=" -m ${MEMSIZE}M -cpu Westmere-v2"
        QEMU_FLAGS+=" -smp 12,sockets=2,cores=6,maxcpus=12"

        MEMSIZE2=$((MEMSIZE / 2))
        QEMU_FLAGS+=" -object memory-backend-ram,size=${MEMSIZE2}M,id=m0,prealloc=on"
        QEMU_FLAGS+=" -object memory-backend-ram,size=${MEMSIZE2}M,id=m1,prealloc=on"

        QEMU_FLAGS+=" -numa node,nodeid=0,memdev=m0,cpus=0-5"
        QEMU_FLAGS+=" -numa node,nodeid=1,memdev=m1,cpus=6-11"

        QEMU_FLAGS+=" -numa dist,src=0,dst=1,val=20"
        QEMU_FLAGS+=" -numa dist,src=1,dst=0,val=20"

    elif [ "x$MACHINE" = "x2S2CPenryn" ]; then

        QEMU_FLAGS+=" -m ${MEMSIZE}M -cpu Penryn-v1"
        QEMU_FLAGS+=" -smp 2,sockets=2,cores=1,maxcpus=2"

        MEMSIZE2=$((MEMSIZE / 2))
        QEMU_FLAGS+=" -object memory-backend-ram,size=${MEMSIZE2}M,id=m0,prealloc=on"
        QEMU_FLAGS+=" -object memory-backend-ram,size=${MEMSIZE2}M,id=m1,prealloc=on"

        QEMU_FLAGS+=" -numa node,nodeid=0,memdev=m0,cpus=0"
        QEMU_FLAGS+=" -numa node,nodeid=1,memdev=m1,cpus=1"

        QEMU_FLAGS+=" -numa dist,src=0,dst=1,val=21"
        QEMU_FLAGS+=" -numa dist,src=1,dst=0,val=21"

    elif [ "x$MACHINE" = "x1S1CPhenom" ]; then

        QEMU_FLAGS+=" -m ${MEMSIZE}M -cpu phenom-v1"
        QEMU_FLAGS+=" -smp 1,sockets=1,cores=1,maxcpus=1"

    fi

    # Define offsets for loading of symbol-table
    IMAGEBASE=0x200000
    BASEOFCODE=0x1000
    DATA=0x23000
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

    # Copy memtest.efi to hda-contents
    cp memtest.efi hda-contents/
    cp memtest.efi hda-contents/EFI/boot/BOOT_X64.efi

    # Copy OVMF* files from /usr/share
    if [ ! -f OVMF.fd ] || [ ! -f OVMF_VARS.fd ] || [ ! -f OVMF_CODE.fd ]; then
        cp /usr/share/ovmf/OVMF.fd .
        cp /usr/share/OVMF/OVMF_CODE.fd .
        cp /usr/share/OVMF/OVMF_VARS.fd .
    fi
}

# Global checks
Check

# Initialize
Init

# Build
Make

# Create needed directories and move efi binary to appropriate location
Prepare_Directory

# Run QEMU and launch second terminal,
# wait for connection via gdb
$TERMINAL gdb -x $GDB_FILE &
$QEMU $QEMU_FLAGS -s -S
