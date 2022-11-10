# Automating Memtest86+

When using Memtest86+ for example on a production line or for regularly
testing the RAM of systems during off-hours as part of preventive maintenance,
it could make sense to fully automate running Memtest86+ and collecting the
results.

In case of a failure it is often best to leave Memtest86+ running and let
an operator determine the next steps. A failure during Memtest86+ is usually
the rare exception.

In case of success you want to get the positive feedback from Memtest86+ that
really all tests passed.

Since automation usually has to be adapted to the scenario and infrastructure
at hand, this document will provide **building blocks and hints** and the user
is expected to combine them into a complete solution as they see fit.

## Autoreboot

The `autoreboot=n` commandline option will let Memtest86+ run as normal. Once
the given number *n* of successful passes of all tests is reached, the system
will reboot automatically.

In case of failure Memtest86+ will continue to run until an operator manually
takes a look.

Be aware that faulty hardware can sometimes also trigger a reboot, so a reboot
can not be taken as a guarantee for a successful test run.

## rtc_result

The `rtc_result` commandline option extends the `autoreboot` option.

When set and an automatic reboot due to the "autoreboot" option should be triggered,
the date in the hardware real time clock (RTC) is set to 2012-01-01. This acts
as indicator that the memtest really was successful and the reboot did not occur
due to faulty hardware, power loss or similar things.
      
It is suggested that you boot a Linux live system afterwards, read out the RTC
there (`hwclock` command) and act depending on the date you get. Then restort the
actual date, see below.

The RTC is used for result transmission because it is easy to modify from a OS-less
program like Memtest86+ and is available with a nearly unchanged interface since 
PCs of the 286/AT times.

## grub `date` command

The grub bootloader contains the [date](https://www.gnu.org/software/grub/manual/grub/grub.html#date)
command. It allows to set the date stored in the RTC.

## grub `datehook` module

This module is part of grub, but often must be explicitly loaded (`insmod datehook`).
When loaded it provides the variables $YEAR, $MONTH, $DAY, $HOUR, $MINUTE, $SECOND, $WEEKDAY
that can then be used in grub.cfg to change how grub behaves.

Example snippet for grub.cfg:

```
insmod datehook
if [ "${YEAR}" == "2012" ]; then
    set default=result_handler
elif [ "${YEAR}" == "2010" ]; then
    set default=memtest
else    
    set default=regular_system
fi

menuentry "Memtest86+" --id memtest {
    if [ ${grub_platform} == "pc" ]; then
        linux /boot/memtest.bin autoreboot=1 rtc_result
    else
        linuxefi /boot/memtest.efi autoreboot=1 rtc_result
    fi
}

menuentry "Memtest result handling" --id result_handler {
    if [ ${grub_platform} == "pc" ]; then
        linux /boot/some_vmlinuz some_linux_parameters
        initrd /boot/some_initrd
    else
        linuxefi /boot/some_vmlinuz some_linux_parameters
        initrdefi /boot/some_initrd
    fi
}

```

When using this example and your automation tools decide to start a memtest, they
first set the year to 2010 (`hwclock --set --date "2010-01-01"`) and then issue a
reboot. Grub will then switch to the `memtest` menu entry. Once memtest passed with
success, it will set the year is to 2012 and reboot. Grub will then boot the Linux
system defined in the `result_handler` menu entry.

## starting Memtest86+ via kexec

When using this example and your automation tools decide to start a memtest and you
don't want to use the grub datehook method outlined above, you can also directly
start Memtest86+ from within Linux.

  * Boot Linux with the `nomodeset` boot commandline option. This keeps the original screen settings
    of the BIOS / UEFI firmware while Linux is running. Necessary to allow Memtest86+
    to use the display.
  * The booted Linux should be a live Linux (see below) that does not mount any filesystems 
    for writing, as this could lead to filesystem corruption (kexec does no clean unmount or shutdown)
  * Start Memtest86+ via kexec:

```
if [[ -d /sys/firmware/efi ]]; then
    # we are on a UEFI system
    kexec -l memtest.efi "--command-line=autoreboot=1 rtc_result"
else
    # we are on system with classic BIOS
    kexec -l memtest.bin "--command-line=autoreboot=1 rtc_result"
fi

kexec -e

```

## Network boot and remote automation control

When you prefer to control hardware testing from a remote controller system, you can
achieve this with network booting (PXE). You usually load [PXELINUX](https://wiki.syslinux.org/wiki/index.php?title=PXELINUX)
and this downloads a boot configuration file from your TFTP server. The loaded
filename depends on the MAC address of the client, allowing you to have different configs for different clients.

Grub can also be compiled to be loaded via TFTP. You can then use the regular scripting
methods in grub.cfg or even access network files via TFTP or HTTP.

So a mechanism for remote control could look like this:

1. An automation script running on the client PC decides to do a memory test
2. It sends a command to the TFTP server that includes the MAC address
3. The TFTP server writes a config file to `pxelinux.cfg` that starts memtest with the `autoreboot` and `rtc_result` parameters. The filename
is the MAC address of the client.
4. reboot
5. The client loads PXELINUX and the config, resulting in Memtest86+ being run
6. After a few minutes, when a reboot cycle should really be finished, the TFTP server switches the config back to one that boots
  a live Linux
7. When Memtest86+ was successful, it sets the RTC date and reboots 
8. The client loads PXELINUX and the config, resulting in the live Linux being run
9. The live Linux checks the date, restores the current date and continues with the next automation step

## Restoring the RTC to the current date

```
ntpdate pool.ntp.org
hwclock --systohc
```

## Live Linux optimized for automation

While any Linux system can be adapted for the task of automating memtest,
[SystemRescue](https://www.system-rescue.org/) offers several features that make
it convenient for automating hardware testing:

  * It is a live Linux, meaning it doesn't need partitions on disk, but everything necessary
     is loaded from a few files into RAM.
  * Provides many tools for hardware testing and inventory
  * Can easily be [network booted via PXE](https://www.system-rescue.org/manual/PXE_network_booting/)
  * [autorun](https://www.system-rescue.org/manual/Run_your_own_scripts_with_autorun/)
  * [autoterminal](https://www.system-rescue.org/manual/autoterminal_scripts_on_virtual_terminal/)
