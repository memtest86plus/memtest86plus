CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy

GIT ?= git

ifeq ($(GIT),none)
  GIT_AVAILABLE = false
else
  GIT_AVAILABLE = true
endif

CFLAGS = -std=gnu11 -Wall -Wextra -Wshadow -march=loongarch64 -fpic -fno-builtin \
         -ffreestanding -fomit-frame-pointer -fno-stack-protector -DARCH_BITS=64

ifeq ($(DEBUG), 1)
  CFLAGS+=-ggdb3 -DDEBUG_GDB
  OPT_SMALL=-Og
  OPT_FAST=-Og
  MS_LDS=memtest_shared_debug.lds
else
  OPT_SMALL=-Os
  OPT_FAST=-O3
  MS_LDS=ldscripts/memtest_shared.lds
endif

INC_DIRS = -I../../boot -I../../system -I ../../system/imc -I../../system/loongarch -I../../lib -I../../tests -I../../app -Iapp

SYS_OBJS = system/acpi.o \
           system/cpulocal.o \
           system/ehci.o \
           system/font.o \
           system/heap.o \
           system/hwquirks.o \
           system/keyboard.o \
           system/ohci.o \
           system/pci_mmio.o \
           system/pmem.o \
           system/reloc.o \
           system/screen.o \
           system/serial.o \
           system/smbios.o \
           system/spd.o \
           system/smp.o \
           system/timers.o \
           system/uhci.o \
           system/usbhcd.o \
           system/xhci.o \
           system/loongarch/i2c.o \
           system/loongarch/cpuid.o \
           system/loongarch/cpuinfo.o \
           system/loongarch/hwctrl.o \
           system/loongarch/memctrl.o \
           system/loongarch/temperature.o \
           system/loongarch/vmem.o

IMC_SRCS = $(wildcard ../../system/imc/loongson/*.c)
IMC_OBJS = $(subst ../../,,$(IMC_SRCS:.c=.o))

LIB_OBJS = lib/barrier.o \
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
           app/main.o \
           app/loongarch/interrupt.o

OBJS = boot/startup.o boot/efisetup.o $(SYS_OBJS) $(IMC_OBJS) $(LIB_OBJS) $(TST_OBJS) $(APP_OBJS)

all: memtest.efi

check:
	@if [ -z ${DEBUG} ]; then\
		echo "Macro DEBUG is not defined. Run debug_memtest.sh to invoke debug target";\
		exit 1;\
	fi

debug: check memtest.debug memtestloongarch.efi

-include boot/efisetup.d
-include $(subst .o,.d,$(SYS_OBJS))
-include $(subst .o,.d,$(IMC_OBJS))
-include $(subst .o,.d,$(LIB_OBJS))
-include $(subst .o,.d,$(TST_OBJS))
-include $(subst .o,.d,$(APP_OBJS))

boot/header.o : | ../../boot/sbat.csv

boot/startup.o: ../../boot/loongarch/startup64.S ../../boot/boot.h
	@mkdir -p boot
	$(CC) -x assembler-with-cpp -c -I../../boot -I../../system/loongarch -o $@ $<

boot/%.o: ../../boot/%.S ../../boot/boot.h app/build_version.h
	@mkdir -p boot
	$(CC) -x assembler-with-cpp -c -I../../boot -Iapp -o $@ $<

boot/loongarch/%.o: ../../boot/loongarch/%.S ../../boot/boot.h app/build_version.h
	@mkdir -p boot/loongarch
	$(CC) -x assembler-with-cpp -c -I../../boot -Iapp -o $@ $<

boot/efisetup.o: ../../boot/efisetup.c
	@mkdir -p boot
	$(CC) -c $(CFLAGS) $(OPT_SMALL)  $(INC_DIRS)  -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

system/reloc.o: ../../system/reloc64.c
	@mkdir -p system
	$(CC) -c $(CFLAGS) -fno-strict-aliasing $(OPT_SMALL)  $(INC_DIRS)  -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

system/%.o: ../../system/%.c
	@mkdir -p system
	$(CC) -c $(CFLAGS) $(OPT_SMALL)  $(INC_DIRS)  -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

system/loongarch/%.o: ../../system/loongarch/%.c
	@mkdir -p system/loongarch/
	$(CC) -c $(CFLAGS) $(OPT_SMALL)  $(INC_DIRS)  -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

system/imc/loongson/%.o: ../../system/imc/loongson/%.c
	@mkdir -p system/imc/loongson/
	$(CC) -c $(CFLAGS) $(OPT_SMALL) $(INC_DIRS) -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

lib/%.o: ../../lib/%.c
	@mkdir -p lib
	$(CC) -c $(CFLAGS) $(OPT_SMALL)  $(INC_DIRS)  -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

tests/%.o: ../../tests/%.c
	@mkdir -p tests
	$(CC) -c $(CFLAGS) $(OPT_FAST) $(INC_DIRS)  -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

app/%.o: ../../app/%.c app/build_version.h
	@mkdir -p app
	$(CC) -c $(CFLAGS) $(OPT_SMALL)  $(INC_DIRS)  -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

app/loongarch/%.o: ../../app/loongarch/%.c app/build_version.h
	@mkdir -p app/loongarch
	$(CC) -c $(CFLAGS) $(OPT_SMALL)  $(INC_DIRS)  -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

app/build_version.h: FORCE
	@mkdir -p app
	@( \
	  cp -f ../../app/version.h $@.tmp; \
	  if $(GIT_AVAILABLE) && test -d ../../.git ; then \
	    hash=`git rev-parse HEAD | cut -c1-7`; \
	    sed -i 's/GIT_HASH\s\".*"/GIT_HASH "'$$hash'"/' $@.tmp; \
	  else \
	    sed -i 's/GIT_HASH\s\".*"/GIT_HASH "unknown"/' $@.tmp; \
	  fi; \
	  cmp $@ $@.tmp 2>/dev/null || cp -f $@.tmp $@; \
	  rm -f $@.tmp; \
	)

FORCE:

# Link it statically once so I know I don't have undefined symbols and
# then link it dynamically so I have full relocation information.

memtest_shared: $(OBJS) $(MS_LDS) Makefile
	$(LD) --warn-constructors --warn-common -static -T $(MS_LDS) -o $@ $(OBJS) && \
	$(LD) -shared -Bsymbolic -T $(MS_LDS) -o $@ $(OBJS)

memtest_shared.bin: memtest_shared
	$(OBJCOPY) -O binary $< memtest_shared.bin

memtest.debug: memtest_shared
	objcopy --only-keep-debug memtest_shared memtest.debug
	strip -R .eh_frame memtest_shared
	strip -R .comment memtest_shared

memtest.efi: memtest_shared.bin boot/loongarch/header.o ldscripts/memtest_efi.lds
	$(eval SIZES=$(shell size -B -d memtest_shared | grep memtest_shared))
	$(LD) --defsym=_bss_size=$(word 3,$(SIZES)) -T ldscripts/memtest_efi.lds boot/loongarch/header.o -b binary memtest_shared.bin -o memtest.elf
	$(OBJCOPY) -O binary memtest.elf memtest.efi
	rm -f memtest.elf

esp.img: memtest.efi
	@mkdir -p iso/EFI/BOOT
	cp memtest.efi iso/EFI/BOOT/BOOTLOONGARCH64.EFI
	@rm -f esp.img
	/sbin/mkdosfs -n MEMTEST-ESP -F12 -C esp.img 4096
	mcopy -s -i esp.img iso/EFI ::

memtest.iso: esp.img
	xorrisofs -pad -R -J -volid MT86PLUS_64 -graft-points \
		  -part_like_isohybrid -iso_mbr_part_type 0x00 -append_partition 2 0xef ./esp.img \
		  -o ./memtest.iso /EFI=./iso/EFI

iso: memtest.iso

clean:
	rm -rf boot system lib tests app *.img *.iso memtest* iso grub-*

# grub-memtest.iso will be added in future. TODO
