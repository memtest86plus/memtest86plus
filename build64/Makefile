AS = as -64
CC = gcc

CFLAGS = -std=c11 -Wall -Wextra -Wshadow -m64 -march=x86-64 -mno-mmx -mno-sse -mno-sse2 \
         -fpic -fno-builtin -ffreestanding -fomit-frame-pointer -fno-stack-protector

INC_DIRS = -I../boot -I../system -I../lib -I../tests -I../app

SYS_OBJS = system/cpuid.o \
           system/cpuinfo.o \
           system/cpulocal.o \
           system/ehci.o \
           system/font.o \
           system/hwctrl.o \
           system/keyboard.o \
           system/ohci.o \
           system/pci.o \
           system/pmem.o \
           system/reloc.o \
           system/screen.o \
           system/smbios.o \
           system/smp.o \
           system/temperature.o \
           system/usbhcd.o \
           system/vmem.o \
           system/xhci.o

LIB_OBJS = lib/barrier.o \
           lib/ctype.o \
           lib/print.o \
           lib/read.o \
           lib/string.o \
           lib/unistd.o

TST_OBJS = tests/addr_walk1.o \
           tests/bit_fade.o \
           tests/block_move.o \
           tests/modulo_n.o \
           tests/mov_inv_fixed.o \
           tests/mov_inv_random.o \
           tests/mov_inv_walk1.o \
           tests/own_addr.o \
           tests/test_helper.o \
           tests/tests.o

APP_OBJS = app/badram.o \
           app/config.o \
           app/display.o \
           app/error.o \
           app/interrupt.o \
           app/main.o

OBJS = boot/startup.o boot/efisetup.o $(SYS_OBJS) $(LIB_OBJS) $(TST_OBJS) $(APP_OBJS)

all: memtest.bin memtest.efi

-include boot/efisetup.d
-include $(subst .o,.d,$(SYS_OBJS))
-include $(subst .o,.d,$(LIB_OBJS))
-include $(subst .o,.d,$(TST_OBJS))
-include $(subst .o,.d,$(APP_OBJS))

boot/%.o: boot/%.s
	$(AS) $< -o $@

boot/startup.s: ../boot/startup64.S ../boot/boot.h
	@mkdir -p boot
	$(CC) -E -traditional -I../boot -o $@ $<

boot/%.s: ../boot/%.S ../boot/boot.h
	@mkdir -p boot
	$(CC) -E -traditional -I../boot -o $@ $<

boot/efisetup.o: ../boot/efisetup.c
	@mkdir -p boot
	$(CC) -c $(CFLAGS) -Os $(INC_DIRS) -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

system/reloc.o: ../system/reloc64.c
	@mkdir -p system
	$(CC) -c $(CFLAGS) -fno-strict-aliasing -Os $(INC_DIRS) -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

system/%.o: ../system/%.c
	@mkdir -p system
	$(CC) -c $(CFLAGS) -Os $(INC_DIRS) -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

lib/%.o: ../lib/%.c
	@mkdir -p lib
	$(CC) -c $(CFLAGS) -Os $(INC_DIRS) -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

tests/%.o: ../tests/%.c
	@mkdir -p tests
	$(CC) -c $(CFLAGS) -O3 $(INC_DIRS) -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

app/%.o: ../app/%.c
	@mkdir -p app
	$(CC) -c $(CFLAGS) -Os $(INC_DIRS) -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

# Link it statically once so I know I don't have undefined symbols and
# then link it dynamically so I have full relocation information.

memtest_shared: $(OBJS) ldscripts/memtest_shared.lds Makefile
	$(LD) --warn-constructors --warn-common -static -T ldscripts/memtest_shared.lds -o $@ $(OBJS) && \
	$(LD) -shared -Bsymbolic -T ldscripts/memtest_shared.lds -o $@ $(OBJS)

memtest_shared.bin: memtest_shared
	objcopy -O binary $< memtest_shared.bin

memtest.bin: memtest_shared.bin boot/bootsect.o boot/setup.o ldscripts/memtest_bin.lds
	$(eval SIZES=$(shell size -B -d memtest_shared | grep memtest_shared))
	$(LD) --defsym=_bss_size=$(word 3,$(SIZES)) -T ldscripts/memtest_bin.lds boot/bootsect.o boot/setup.o -b binary memtest_shared.bin -o memtest.bin

memtest.efi: memtest_shared.bin boot/header.o boot/setup.o ldscripts/memtest_efi.lds
	$(eval SIZES=$(shell size -B -d memtest_shared | grep memtest_shared))
	$(LD) --defsym=_bss_size=$(word 3,$(SIZES)) -T ldscripts/memtest_efi.lds boot/header.o boot/setup.o -b binary memtest_shared.bin -o memtest.efi

memtest.mbr: memtest_shared.bin boot/mbr.o ldscripts/memtest_mbr.lds
	$(LD) -T ldscripts/memtest_mbr.lds boot/mbr.o -b binary memtest_shared.bin -o memtest.mbr

floppy.img: memtest.bin
	dd if=/dev/zero of=floppy.img bs=1474560 count=1
	dd if=memtest.bin of=floppy.img bs=1474560 conv=notrunc

esp.img: memtest.efi
	@mkdir -p iso/EFI/BOOT
	cp memtest.efi iso/EFI/BOOT/bootx64.efi
	@rm -f esp.img
	/sbin/mkdosfs -n MEMTEST-ESP -F12 -C esp.img 4096
	mcopy -s -i esp.img iso/EFI ::

memtest.iso: memtest.mbr floppy.img esp.img
	@mkdir -p iso/boot
	cp floppy.img iso/boot/floppy.img
	xorrisofs -pad -R -J -volid Memtest86+64 -graft-points -hide-rr-moved --grub2-mbr memtest.mbr \
		  -b /boot/floppy.img --efi-boot --interval:appended_partition_2:all:: \
		  -part_like_isohybrid -iso_mbr_part_type 0x00 -append_partition 2 0xef ./esp.img \
		  -o ./memtest.iso /boot=./iso/boot /EFI=./iso/EFI

iso: memtest.iso

clean:
	rm -rf boot system lib tests app *.img *.iso memtest* iso grub-*

# grub-memtest.iso can be used for testing the various different boot modes,
# using GRUB as an intermediate bootloader. Upstream GRUB only supports the
# 16-bit and 32-bit boot protocols, via the "linux16" and "linux" commands.
# Fedora add support for the EFI handover boot protocols, via the "linuxefi"
# command, and, since 2019, remove support for the 32-bit protocol, aliasing
# the "linux" command to either "linux16" or "linuxefi", depending on whether
# you boot in legacy or EFI mode. Mageia follows Fedora, but restores support
# for the 32-bit protocol, via the "linux32" command. Other distributions no
# doubt do their own thing. The boot menu on grub-memtest.iso attempts to
# support all these options.

GRUB_FONT_DIR ?= /usr/share/grub

GRUB_LIB_DIR ?= /usr/lib/grub

GRUB_MKIMAGE ?= grub2-mkimage

GRUB_MODULES = iso9660 fat part_msdos all_video font gfxterm gfxmenu \
               boot chain configfile echo ls

grub-eltorito.img:
	$(GRUB_MKIMAGE) --output $@ --prefix /boot/grub --format i386-pc-eltorito biosdisk $(GRUB_MODULES)

grub-bootx64.efi:
	$(GRUB_MKIMAGE) --output $@ --prefix /EFI/BOOT/grub --format x86_64-efi $(GRUB_MODULES)

grub-esp.img: memtest.efi grub-bootx64.efi ../grub/grub-efi.cfg
	@mkdir -p grub-iso/EFI/BOOT/grub/x86_64-efi grub-iso/EFI/BOOT/grub/fonts
	cp memtest.efi grub-iso/EFI/BOOT/memtest
	cp grub-bootx64.efi grub-iso/EFI/BOOT/bootx64.efi
	cp ../grub/grub-efi.cfg grub-iso/EFI/BOOT/grub/grub.cfg
	cp $(GRUB_FONT_DIR)/unicode.pf2 grub-iso/EFI/BOOT/grub/fonts/
	cp $(GRUB_LIB_DIR)/x86_64-efi/*.mod grub-iso/EFI/BOOT/grub/x86_64-efi/
	@rm -f grub-esp.img
	/sbin/mkdosfs -n MEMTEST-ESP -F12 -C grub-esp.img 8192
	mcopy -s -i grub-esp.img grub-iso/EFI ::

grub-memtest.iso: memtest.bin grub-eltorito.img ../grub/grub-legacy.cfg grub-esp.img
	@mkdir -p grub-iso/boot/grub/i386-pc grub-iso/boot/grub/fonts
	cp memtest.bin grub-iso/boot/memtest
	cp grub-eltorito.img grub-iso/boot/eltorito.img
	cp ../grub/grub-legacy.cfg grub-iso/boot/grub/grub.cfg
	cp $(GRUB_FONT_DIR)/unicode.pf2 grub-iso/boot/grub/fonts/
	cp $(GRUB_LIB_DIR)/i386-pc/*.mod grub-iso/boot/grub/i386-pc/
	xorrisofs -pad -R -J -volid Memtest86+64 -graft-points -hide-rr-moved \
		  --grub2-mbr $(GRUB_LIB_DIR)/i386-pc/boot_hybrid.img \
		  -b /boot/eltorito.img -no-emul-boot -boot-load-size 4 -boot-info-table --grub2-boot-info \
		  --efi-boot --interval:appended_partition_2:all:: \
		  -part_like_isohybrid -iso_mbr_part_type 0x00 -append_partition 2 0xef ./grub-esp.img \
		  -o ./grub-memtest.iso /boot=./grub-iso/boot /EFI=./grub-iso/EFI

grub-iso: grub-memtest.iso
