AS = as -64
CC = gcc

CFLAGS = -std=c11 -Wall -Wextra -Wshadow -m64 -march=x86-64 -mno-mmx -mno-sse -mno-sse2 \
         -fpic -fno-builtin -ffreestanding -fomit-frame-pointer -fno-stack-protector

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
	$(LD) -T ldscripts/memtest_bin.lds boot/bootsect.o boot/setup.o -b binary memtest_shared.bin -o memtest.bin

memtest.efi: memtest_shared.bin boot/header.o boot/setup.o ldscripts/memtest_efi.lds
	$(LD) -T ldscripts/memtest_efi.lds boot/header.o boot/setup.o -b binary memtest_shared.bin -o memtest.efi

memtest.img: memtest.bin
	dd if=/dev/zero of=memtest.img bs=1474560 count=1
	dd if=memtest.bin of=memtest.img bs=1474560 conv=notrunc

iso: memtest.img memtest.efi
	@mkdir -p iso/boot iso/EFI/BOOT
	cp memtest.img iso/boot/memtest.img
	cp memtest.efi iso/EFI/BOOT/bootx64.efi
	xorrisofs -pad -R -J -volid PCMemTest64 -graft-points -hide-rr-moved \
		  -b /boot/memtest.img --efi-boot /EFI/BOOT/bootx64.efi \
		  -o ./memtest.iso /boot=./iso/boot /EFI=./iso/EFI

clean:
	rm -rf boot system lib tests app *.iso memtest* iso
