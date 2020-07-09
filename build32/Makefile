AS = as -32
CC = gcc

CFLAGS = -std=c11 -Wall -Wextra -Wshadow -m32 -march=i586 -fpic -fno-builtin \
         -ffreestanding -fomit-frame-pointer -fno-stack-protector

INC_DIRS = -I../boot -I../system -I../lib -I../tests -I../app

SYS_OBJS = system/cpuid.o \
           system/cpuinfo.o \
           system/font.o \
           system/hwctrl.o \
           system/keyboard.o \
           system/pci.o \
           system/pmem.o \
           system/reloc.o \
           system/screen.o \
           system/smp.o \
           system/temperature.o \
           system/vmem.o

LIB_OBJS = lib/barrier.o \
           lib/ctype.o \
           lib/div64.o \
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

boot/startup.s: ../boot/startup32.S ../boot/boot.h
	@mkdir -p boot
	$(CC) -m32 -E -traditional -I../boot -o $@ $<

boot/%.s: ../boot/%.S ../boot/boot.h
	@mkdir -p boot
	$(CC) -m32 -E -traditional -I../boot -o $@ $<

boot/efisetup.o: ../boot/efisetup.c
	@mkdir -p boot
	$(CC) -c $(CFLAGS) -Os $(INC_DIRS) -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

system/reloc.o: ../system/reloc32.c
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
	$(eval SIZES=$(shell size -G -d memtest_shared | grep memtest_shared))
	$(LD) --defsym=_bss_size=$(word 3,$(SIZES)) -T ldscripts/memtest_bin.lds boot/bootsect.o boot/setup.o -b binary memtest_shared.bin -o memtest.bin

memtest.efi: memtest_shared.bin boot/header.o boot/setup.o ldscripts/memtest_efi.lds
	$(eval SIZES=$(shell size -G -d memtest_shared | grep memtest_shared))
	$(LD) --defsym=_bss_size=$(word 3,$(SIZES)) -T ldscripts/memtest_efi.lds boot/header.o boot/setup.o -b binary memtest_shared.bin -o memtest.efi

memtest.mbr: memtest_shared.bin boot/mbr.o ldscripts/memtest_mbr.lds
	$(LD) -T ldscripts/memtest_mbr.lds boot/mbr.o -b binary memtest_shared.bin -o memtest.mbr

floppy.img: memtest.bin
	dd if=/dev/zero of=floppy.img bs=1474560 count=1
	dd if=memtest.bin of=floppy.img bs=1474560 conv=notrunc

esp.img: memtest.efi
	@mkdir -p iso/EFI/BOOT
	cp memtest.efi iso/EFI/BOOT/bootia32.efi
	@rm -f esp.img
	/sbin/mkdosfs -n MEMTEST-ESP -F12 -C esp.img 4096
	mcopy -s -i esp.img iso/EFI ::

iso: memtest.mbr floppy.img esp.img
	@mkdir -p iso/boot
	cp floppy.img iso/boot/floppy.img
	xorrisofs -pad -R -J -volid PCMemTest32 -graft-points -hide-rr-moved --grub2-mbr memtest.mbr \
		  -b /boot/floppy.img --efi-boot --interval:appended_partition_2:all:: \
		  -part_like_isohybrid -iso_mbr_part_type 0x00 -append_partition 2 0xef ./esp.img \
		  -o ./memtest.iso /boot=./iso/boot

clean:
	rm -rf boot system lib tests app *.img *.iso memtest* iso
