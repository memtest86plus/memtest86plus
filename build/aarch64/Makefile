# Default to the cross toolchain when not building on an AArch64 host.
# Overridable from the command line (make CC=... LD=... OBJCOPY=...).
ifeq ($(shell uname -m),aarch64)
  CC = gcc
  LD = ld
  OBJCOPY = objcopy
else
  CC = aarch64-linux-gnu-gcc
  LD = aarch64-linux-gnu-ld
  OBJCOPY = aarch64-linux-gnu-objcopy
endif

GIT ?= git

ifeq ($(GIT),none)
  GIT_AVAILABLE = false
else
  GIT_AVAILABLE = true
endif

CFLAGS = -std=gnu11 -Wall -Wextra -Wshadow -march=armv8-a -mno-outline-atomics -fpic \
         -fno-builtin -ffreestanding -fomit-frame-pointer -fno-stack-protector -DARCH_BITS=64

LDFLAGS = -z max-page-size=4096

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

INC_DIRS = -I../../boot -I../../system -I ../../system/imc -I../../system/aarch64 -I../../lib -I../../tests -I../../app -Iapp

SYS_OBJS = system/acpi.o \
           system/cpulocal.o \
           system/ehci.o \
           system/fat32.o \
           system/font.o \
           system/heap.o \
           system/hwquirks.o \
           system/keyboard.o \
           system/ohci.o \
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
           system/usbmsd.o \
           system/xhci.o \
           system/aarch64/i2c.o \
           system/aarch64/cpuid.o \
           system/aarch64/cpuinfo.o \
           system/aarch64/hwctrl.o \
           system/aarch64/memctrl.o \
           system/aarch64/simd.o \
           system/aarch64/pci.o \
           system/aarch64/psci.o \
           system/aarch64/temperature.o \
           system/aarch64/vmem.o

IMC_SRCS = $(wildcard ../../system/imc/aarch64/*.c)
IMC_OBJS = $(subst ../../,,$(IMC_SRCS:.c=.o))

LIB_OBJS = lib/barrier.o \
           lib/print.o \
           lib/read.o \
           lib/string.o \
           lib/unistd.o

TST_OBJS = tests/addr_walk1.o \
           tests/bit_fade.o \
           tests/block_move.o \
           tests/bus_stress.o \
           tests/modulo_n.o \
           tests/mov_inv_fixed.o \
           tests/mov_inv_rng.o \
           tests/vec_prsg.o \
           tests/aarch64/vec_prsg_neon.o \
           tests/mov_inv_walk1.o \
           tests/own_addr.o \
           tests/rowhammer.o \
           tests/test_helper.o \
           tests/tests.o

APP_OBJS = app/badram.o \
           app/config.o \
           app/display.o \
           app/error.o \
           app/main.o \
           app/reports.o \
           app/aarch64/interrupt.o

OBJS = boot/startup.o boot/efisetup.o $(SYS_OBJS) $(IMC_OBJS) $(LIB_OBJS) $(TST_OBJS) $(APP_OBJS)

all: mt86plus

check:
	@if [ -z ${DEBUG} ]; then\
		echo "Macro DEBUG is not defined. Run debug_memtest.sh to invoke debug target";\
		exit 1;\
	fi

debug: check memtest.debug

-include boot/efisetup.d
-include $(subst .o,.d,$(SYS_OBJS))
-include $(subst .o,.d,$(IMC_OBJS))
-include $(subst .o,.d,$(LIB_OBJS))
-include $(subst .o,.d,$(TST_OBJS))
-include $(subst .o,.d,$(APP_OBJS))

boot/header.o : | ../../boot/sbat.csv

boot/startup.o: ../../boot/aarch64/startup.S ../../boot/boot.h
	@mkdir -p boot
	$(CC) -x assembler-with-cpp -c -I../../boot -I../../system/aarch64 -o $@ $<

boot/%.o: ../../boot/%.S ../../boot/boot.h app/build_version.h
	@mkdir -p boot
	$(CC) -x assembler-with-cpp -c -I../../boot -Iapp -o $@ $<

boot/aarch64/%.o: ../../boot/aarch64/%.S ../../boot/boot.h app/build_version.h
	@mkdir -p boot/aarch64
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

system/aarch64/%.o: ../../system/aarch64/%.c
	@mkdir -p system/aarch64/
	$(CC) -c $(CFLAGS) $(OPT_SMALL)  $(INC_DIRS)  -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

system/imc/aarch64/%.o: ../../system/imc/aarch64/%.c
	@mkdir -p system/imc/aarch64/
	$(CC) -c $(CFLAGS) $(OPT_SMALL) $(INC_DIRS) -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

lib/%.o: ../../lib/%.c
	@mkdir -p lib
	$(CC) -c $(CFLAGS) $(OPT_SMALL)  $(INC_DIRS)  -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

tests/%.o: ../../tests/%.c
	@mkdir -p tests
	$(CC) -c $(CFLAGS) $(OPT_FAST) $(INC_DIRS)  -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

tests/aarch64/%.o: ../../tests/aarch64/%.c
	@mkdir -p tests/aarch64
	$(CC) -c $(CFLAGS) $(OPT_FAST) $(INC_DIRS)  -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

app/%.o: ../../app/%.c app/build_version.h
	@mkdir -p app
	$(CC) -c $(CFLAGS) $(OPT_SMALL)  $(INC_DIRS)  -o $@ $< -MMD -MP -MT $@ -MF $(@:.o=.d)

app/aarch64/%.o: ../../app/aarch64/%.c app/build_version.h
	@mkdir -p app/aarch64
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
	$(LD) $(LDFLAGS) --warn-common -static -T $(MS_LDS) -o $@ $(OBJS) && \
	$(LD) $(LDFLAGS) -shared -Bsymbolic -T $(MS_LDS) -o $@ $(OBJS)

memtest_shared.bin: memtest_shared
	$(OBJCOPY) -O binary $< memtest_shared.bin

memtest.debug: memtest_shared
	objcopy --only-keep-debug memtest_shared memtest.debug
	strip -R .eh_frame memtest_shared
	strip -R .comment memtest_shared

# Build the PE/EFI binary with W^X sections
# The flat binary is split at the _etext_ro boundary (page-aligned in
# memtest_shared.lds) into an RX half (.text) and an RW half (.data)
# Both are then used by ld as raw binary inputs. The EFI linker script
# push them into separate PE sections via filename match

mt86plus: memtest_shared.bin boot/aarch64/header.o ldscripts/memtest_efi.lds
	$(eval SIZES=$(shell size -B -d memtest_shared | grep memtest_shared))
	$(eval SPLIT=$(shell nm -t d memtest_shared | awk '/ _etext_ro$$/{print $$1+0}'))
	dd if=memtest_shared.bin of=memtest_text.bin bs=$(SPLIT) count=1 2>/dev/null
	dd if=memtest_shared.bin of=memtest_data.bin bs=$(SPLIT) skip=1 2>/dev/null
	$(LD) --defsym=_bss_size=$(word 3,$(SIZES)) -T ldscripts/memtest_efi.lds boot/aarch64/header.o -b binary memtest_text.bin memtest_data.bin -o mt86plus.elf
	$(OBJCOPY) -O binary mt86plus.elf mt86plus

esp.img: mt86plus
	@mkdir -p iso/EFI/BOOT
	cp mt86plus iso/EFI/BOOT/BOOTAA64.EFI
	@rm -f esp.img
	/sbin/mkdosfs -n MEMTEST-ESP -F12 -C esp.img 4096
	mcopy -s -i esp.img iso/EFI ::

memtest.iso: esp.img
	xorrisofs -pad -R -J -volid MT86PLUS_64_EFI_ONLY -no-emul-boot -graft-points \
		  -append_partition 2 0xef ./esp.img \
		  -o ./memtest.iso /EFI=./iso/EFI

iso: memtest.iso

clean:
	rm -rf boot system lib tests app *.img *.iso *.elf memtest* mt86plus iso grub-*

# grub-memtest.iso uses GRUB as an intermediate bootloader to allow Memtest86+
# to be started with the native USB keyboard drivers either enabled or disabled,
# or to be started in a fail-safe mode with SMP and memory identification &
# speed testing disabled.

GRUB_CFG ?= grub

GRUB_FONT_DIR ?= /usr/share/grub

GRUB_LIB_DIR ?= /usr/lib/grub

GRUB_MKIMAGE := $(shell command -v grub2-mkimage || command -v grub-mkimage)

GRUB_MODULES = iso9660 fat part_msdos part_gpt all_video font gfxterm gfxmenu \
               boot chain configfile echo ls

grub-bootaa64.efi:
	$(GRUB_MKIMAGE) --output $@ --prefix /EFI/BOOT/grub --format arm64-efi $(GRUB_MODULES)

grub-esp.img: mt86plus grub-bootaa64.efi ../../grub/${GRUB_CFG}-efi.cfg
	@mkdir -p grub-iso/EFI/BOOT/grub/arm64-efi grub-iso/EFI/BOOT/grub/fonts
	cp mt86plus grub-iso/EFI/BOOT/memtest
	cp grub-bootaa64.efi grub-iso/EFI/BOOT/BOOTAA64.EFI
	cp ../../grub/${GRUB_CFG}-efi.cfg grub-iso/EFI/BOOT/grub/grub.cfg
	cp $(GRUB_FONT_DIR)/unicode.pf2 grub-iso/EFI/BOOT/grub/fonts/
	cp $(GRUB_LIB_DIR)/arm64-efi/*.mod grub-iso/EFI/BOOT/grub/arm64-efi/
	@rm -f grub-esp.img
	/sbin/mkdosfs -n MT86P_ESP -F12 -C grub-esp.img 8192
	mcopy -s -i grub-esp.img grub-iso/EFI ::

grub-memtest.iso: mt86plus  grub-esp.img
	@mkdir -p grub-iso/boot/grub/fonts
	cp $(GRUB_FONT_DIR)/unicode.pf2 grub-iso/boot/grub/fonts/
	xorrisofs -pad -R -J -volid MT86PLUS_64_EFI_ONLY  -no-emul-boot -graft-points \
		  -boot-load-size 4 -boot-info-table --grub2-boot-info \
		  -append_partition 2 0xef ./grub-esp.img \
		  -o ./grub-memtest.iso /EFI=./grub-iso/EFI

grub-iso: grub-memtest.iso
