// SPDX-License-Identifier: GPL-2.0
//
// startup64.S contains the 64-bit startup code for both the BSP and APs.
// It initialises stacks, memory management, and exception handling, clears
// the BSS, completes relocation, and finally calls the main application.
//
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2024 Loongson Technology Corporation Limited. All rights reserved.
//

#define __ASSEMBLY__

#include "boot.h"
#inlucde "interrupt.h"

#define RSIZE                 8           // 64 bit mode register size
#define GP_REG_CONTEXT_SIZE   32 * RSIZE  // General-purpose registers size
#define FP_REG_CONTEXT_SIZE   34 * RSIZE  // Floating-point registers size
#define CSR_REG_CONTEXT_SIZE  9  * RSIZE  // CSR registers size

//
// CSR definitions
//
#define LOONGARCH_CSR_CRMD       0x0
#define LOONGARCH_CSR_PRMD       0x1
#define LOONGARCH_CSR_EUEN       0x2
#define LOONGARCH_CSR_MISC       0x3
#define LOONGARCH_CSR_ECFG       0x4
#define LOONGARCH_CSR_ESTAT      0x5
#define LOONGARCH_CSR_ERA        0x6
#define LOONGARCH_CSR_BADV       0x7
#define LOONGARCH_CSR_BADI       0x8
#define LOONGARCH_CSR_EBASE      0xC     // Exception entry base address
#define LOONGARCH_CSR_CPUNUM     0x20    // CPU core number
#define LOONGARCH_CSR_KS0        0x30
#define LOONGARCH_CSR_TLBREBASE  0x88    // TLB refill exception entry
#define LOONGARCH_CSR_DMWIN0     0x180   // 64 direct map win0: MEM & IF
#define LOONGARCH_CSR_DMWIN1     0x181   // 64 direct map win1: MEM & IF
#define LOONGARCH_CSR_DMWIN2     0x182   // 64 direct map win2: MEM
#define LOONGARCH_CSR_DMWIN3     0x183   // 64 direct map win3: MEM

	.text

exception_entry:
  csrwr   $sp, LOONGARCH_CSR_KS0

  csrrd   $sp, LOONGARCH_CSR_KS0

  //
  // Push GP registers
  //
  addi.d  $sp, $sp, -(GP_REG_CONTEXT_SIZE + FP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE)
  st.d    $zero, $sp, 0 * RSIZE
  st.d    $ra, $sp, 1 * RSIZE
  st.d    $tp, $sp, 2 * RSIZE
  st.d    $a0, $sp, 4 * RSIZE
  st.d    $a1, $sp, 5 * RSIZE
  st.d    $a2, $sp, 6 * RSIZE
  st.d    $a3, $sp, 7 * RSIZE
  st.d    $a4, $sp, 8 * RSIZE
  st.d    $a5, $sp, 9 * RSIZE
  st.d    $a6, $sp, 10 * RSIZE
  st.d    $a7, $sp, 11 * RSIZE
  st.d    $t0, $sp, 12 * RSIZE
  st.d    $t1, $sp, 13 * RSIZE
  st.d    $t2, $sp, 14 * RSIZE
  st.d    $t3, $sp, 15 * RSIZE
  st.d    $t4, $sp, 16 * RSIZE
  st.d    $t5, $sp, 17 * RSIZE
  st.d    $t6, $sp, 18 * RSIZE
  st.d    $t7, $sp, 19 * RSIZE
  st.d    $t8, $sp, 20 * RSIZE
  st.d    $r21, $sp, 21 * RSIZE
  st.d    $fp, $sp, 22 * RSIZE
  st.d    $s0, $sp, 23 * RSIZE
  st.d    $s1, $sp, 24 * RSIZE
  st.d    $s2, $sp, 25 * RSIZE
  st.d    $s3, $sp, 26 * RSIZE
  st.d    $s4, $sp, 27 * RSIZE
  st.d    $s5, $sp, 28 * RSIZE
  st.d    $s6, $sp, 29 * RSIZE
  st.d    $s7, $sp, 30 * RSIZE
  st.d    $s8, $sp, 31 * RSIZE
  csrrd   $t0, LOONGARCH_CSR_KS0     // Read the old stack pointer.
  st.d    $t0, $sp, 3 * RSIZE

  //
  // Push CSR registers
  //
  addi.d  $sp, $sp, GP_REG_CONTEXT_SIZE

  csrrd   $t0, LOONGARCH_CSR_CRMD
  st.d    $t0, $sp, LOONGARCH_CSR_CRMD * RSIZE
  csrrd   $t0, LOONGARCH_CSR_PRMD
  st.d    $t0, $sp, LOONGARCH_CSR_PRMD * RSIZE
  csrrd   $t0, LOONGARCH_CSR_EUEN
  st.d    $t0, $sp, LOONGARCH_CSR_EUEN * RSIZE
  csrrd   $t0, LOONGARCH_CSR_MISC
  st.d    $t0, $sp, LOONGARCH_CSR_MISC * RSIZE
  csrrd   $t0, LOONGARCH_CSR_ECFG
  st.d    $t0, $sp, LOONGARCH_CSR_ECFG * RSIZE
  csrrd   $t0, LOONGARCH_CSR_ESTAT
  st.d    $t0, $sp, LOONGARCH_CSR_ESTAT * RSIZE
  csrrd   $t0, LOONGARCH_CSR_ERA
  st.d    $t0, $sp, LOONGARCH_CSR_ERA * RSIZE
  csrrd   $t0, LOONGARCH_CSR_BADV
  st.d    $t0, $sp, LOONGARCH_CSR_BADV * RSIZE
  csrrd   $t0, LOONGARCH_CSR_BADI
  st.d    $t0, $sp, LOONGARCH_CSR_BADI * RSIZE

  //
  // Push FP registers
  //
  addi.d  $sp, $sp, CSR_REG_CONTEXT_SIZE

  csrrd   $t0, LOONGARCH_CSR_EUEN
  andi    $t0, $t0, 0x1
  beqz    $t0, PushRegDone

  fst.d  $fa0, $sp, 0 * RSIZE
  fst.d  $fa1, $sp, 1 * RSIZE
  fst.d  $fa2, $sp, 2 * RSIZE
  fst.d  $fa3, $sp, 3 * RSIZE
  fst.d  $fa4, $sp, 4 * RSIZE
  fst.d  $fa5, $sp, 5 * RSIZE
  fst.d  $fa6, $sp, 6 * RSIZE
  fst.d  $fa7, $sp, 7 * RSIZE
  fst.d  $ft0, $sp, 8 * RSIZE
  fst.d  $ft1, $sp, 9 * RSIZE
  fst.d  $ft2, $sp, 10 * RSIZE
  fst.d  $ft3, $sp, 11 * RSIZE
  fst.d  $ft4, $sp, 12 * RSIZE
  fst.d  $ft5, $sp, 13 * RSIZE
  fst.d  $ft6, $sp, 14 * RSIZE
  fst.d  $ft7, $sp, 15 * RSIZE
  fst.d  $ft8, $sp, 16 * RSIZE
  fst.d  $ft9, $sp, 17 * RSIZE
  fst.d  $ft10, $sp, 18 * RSIZE
  fst.d  $ft11, $sp, 19 * RSIZE
  fst.d  $ft12, $sp, 20 * RSIZE
  fst.d  $ft13, $sp, 21 * RSIZE
  fst.d  $ft14, $sp, 22 * RSIZE
  fst.d  $ft15, $sp, 23 * RSIZE
  fst.d  $fs0, $sp, 24 * RSIZE
  fst.d  $fs1, $sp, 25 * RSIZE
  fst.d  $fs2, $sp, 26 * RSIZE
  fst.d  $fs3, $sp, 27 * RSIZE
  fst.d  $fs4, $sp, 28 * RSIZE
  fst.d  $fs5, $sp, 29 * RSIZE
  fst.d  $fs6, $sp, 30 * RSIZE
  fst.d  $fs7, $sp, 31 * RSIZE

  movfcsr2gr  $t3, $fcsr0
  st.d        $t3, $sp, 32 * RSIZE  // Push the FCSR0 register.

  //
  // Push the fcc0-fcc7 registers.
  //
  movcf2gr    $t3, $fcc0
  move        $t2, $t3
  movcf2gr    $t3, $fcc1
  bstrins.d   $t2, $t3, 0xf, 0x8
  movcf2gr    $t3, $fcc2
  bstrins.d   $t2, $t3, 0x17, 0x10
  movcf2gr    $t3, $fcc3
  bstrins.d   $t2, $t3, 0x1f, 0x18
  movcf2gr    $t3, $fcc4
  bstrins.d   $t2, $t3, 0x27, 0x20
  movcf2gr    $t3, $fcc5
  bstrins.d   $t2, $t3, 0x2f, 0x28
  movcf2gr    $t3, $fcc6
  bstrins.d   $t2, $t3, 0x37, 0x30
  movcf2gr    $t3, $fcc7
  bstrins.d   $t2, $t3, 0x3f, 0x38
  st.d        $t2, $sp, 33 * RSIZE
  //
  // Push exception context down
  //

PushRegDone:
  addi.d  $sp, $sp, -(GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE)
  move    $a0, $sp
  bl      interrupt

  # If returned, POP the REG

  //
  // Pop CSR reigsters
  //
  addi.d  $sp, $sp, GP_REG_CONTEXT_SIZE

  ld.d    $t0, $sp, LOONGARCH_CSR_CRMD * RSIZE
  csrwr   $t0, LOONGARCH_CSR_CRMD
  ld.d    $t0, $sp, LOONGARCH_CSR_PRMD * RSIZE
  csrwr   $t0, LOONGARCH_CSR_PRMD
  ld.d    $t0, $sp, LOONGARCH_CSR_ECFG * RSIZE
  csrwr   $t0, LOONGARCH_CSR_ECFG
  ld.d    $t0, $sp, LOONGARCH_CSR_ERA * RSIZE
  csrwr   $t0, LOONGARCH_CSR_ERA

  addi.d  $sp, $sp, CSR_REG_CONTEXT_SIZE  // Fource change the stack pointer befor pop the FP registers.

  csrrd   $t1, LOONGARCH_CSR_EUEN
  andi    $t1, $t1, 0x1
  beqz    $t1, PopGP                      // If the FPE not set, only pop the GP registers.

  //
  // Pop FP registers
  //
  fld.d  $fa0, $sp, 0 * RSIZE
  fld.d  $fa1, $sp, 1 * RSIZE
  fld.d  $fa2, $sp, 2 * RSIZE
  fld.d  $fa3, $sp, 3 * RSIZE
  fld.d  $fa4, $sp, 4 * RSIZE
  fld.d  $fa5, $sp, 5 * RSIZE
  fld.d  $fa6, $sp, 6 * RSIZE
  fld.d  $fa7, $sp, 7 * RSIZE
  fld.d  $ft0, $sp, 8 * RSIZE
  fld.d  $ft1, $sp, 9 * RSIZE
  fld.d  $ft2, $sp, 10 * RSIZE
  fld.d  $ft3, $sp, 11 * RSIZE
  fld.d  $ft4, $sp, 12 * RSIZE
  fld.d  $ft5, $sp, 13 * RSIZE
  fld.d  $ft6, $sp, 14 * RSIZE
  fld.d  $ft7, $sp, 15 * RSIZE
  fld.d  $ft8, $sp, 16 * RSIZE
  fld.d  $ft9, $sp, 17 * RSIZE
  fld.d  $ft10, $sp, 18 * RSIZE
  fld.d  $ft11, $sp, 19 * RSIZE
  fld.d  $ft12, $sp, 20 * RSIZE
  fld.d  $ft13, $sp, 21 * RSIZE
  fld.d  $ft14, $sp, 22 * RSIZE
  fld.d  $ft15, $sp, 23 * RSIZE
  fld.d  $fs0, $sp, 24 * RSIZE
  fld.d  $fs1, $sp, 25 * RSIZE
  fld.d  $fs2, $sp, 26 * RSIZE
  fld.d  $fs3, $sp, 27 * RSIZE
  fld.d  $fs4, $sp, 28 * RSIZE
  fld.d  $fs5, $sp, 29 * RSIZE
  fld.d  $fs6, $sp, 30 * RSIZE
  fld.d  $fs7, $sp, 31 * RSIZE

  ld.d        $t0, $sp, 32 * RSIZE
  movgr2fcsr  $fcsr0, $t0             // Pop the fcsr0 register.

  //
  // Pop the fcc0-fcc7 registers.
  //
  ld.d        $t0, $sp, 33 * RSIZE
  bstrpick.d  $t1, $t0, 7, 0
  movgr2cf    $fcc0, $t1
  bstrpick.d  $t1, $t0, 15, 8
  movgr2cf    $fcc1, $t1
  bstrpick.d  $t1, $t0, 23, 16
  movgr2cf    $fcc2, $t1
  bstrpick.d  $t1, $t0, 31, 24
  movgr2cf    $fcc3, $t1
  bstrpick.d  $t1, $t0, 39, 32
  movgr2cf    $fcc4, $t1
  bstrpick.d  $t1, $t0, 47, 40
  movgr2cf    $fcc5, $t1
  bstrpick.d  $t1, $t0, 55, 48
  movgr2cf    $fcc6, $t1
  bstrpick.d  $t1, $t0, 63, 56
  movgr2cf    $fcc7, $t1

PopGP:

  //
  // Pop GP registers
  //
  addi.d  $sp, $sp, -(GP_REG_CONTEXT_SIZE + CSR_REG_CONTEXT_SIZE)

  ld.d    $ra, $sp, 1 * RSIZE
  ld.d    $tp, $sp, 2 * RSIZE
  ld.d    $a0, $sp, 4 * RSIZE
  ld.d    $a1, $sp, 5 * RSIZE
  ld.d    $a2, $sp, 6 * RSIZE
  ld.d    $a3, $sp, 7 * RSIZE
  ld.d    $a4, $sp, 8 * RSIZE
  ld.d    $a5, $sp, 9 * RSIZE
  ld.d    $a6, $sp, 10 * RSIZE
  ld.d    $a7, $sp, 11 * RSIZE
  ld.d    $t0, $sp, 12 * RSIZE
  ld.d    $t1, $sp, 13 * RSIZE
  ld.d    $t2, $sp, 14 * RSIZE
  ld.d    $t3, $sp, 15 * RSIZE
  ld.d    $t4, $sp, 16 * RSIZE
  ld.d    $t5, $sp, 17 * RSIZE
  ld.d    $t6, $sp, 18 * RSIZE
  ld.d    $t7, $sp, 19 * RSIZE
  ld.d    $t8, $sp, 20 * RSIZE
  ld.d    $r21, $sp, 21 * RSIZE
  ld.d    $fp, $sp, 22 * RSIZE
  ld.d    $s0, $sp, 23 * RSIZE
  ld.d    $s1, $sp, 24 * RSIZE
  ld.d    $s2, $sp, 25 * RSIZE
  ld.d    $s3, $sp, 26 * RSIZE
  ld.d    $s4, $sp, 27 * RSIZE
  ld.d    $s5, $sp, 28 * RSIZE
  ld.d    $s6, $sp, 29 * RSIZE
  ld.d    $s7, $sp, 30 * RSIZE
  ld.d    $s8, $sp, 31 * RSIZE
  ld.d    $sp, $sp, 3 * RSIZE

  ertn // Returen from exception.

	.globl	startup32

startup32:
    idle 0  # Should be unreachable.

# The EFI PE32+ boot entry point. The entry point for MP boot.

	.org	0x400
	.globl	efi_boot
efi_boot:
    move    $a2, $zero       # the boot params pointer (0 = not yet allocated)

    bl      efi_setup

    # Save the boot params pointer.
    la.pcrel  $t0, boot_params_addr
    st.d    $a0, $t0, 0x0
    b       startup64

# The 64-bit boot entry point and the entry point for AP boot.

	.org	0x440
	.globl	startup64
startup64:
    # Disable global intrrupts
    li.w    $t0, (0x1 << 2)
    csrxchg $zero, $t0, LOONGARCH_CSR_CRMD

    # Init Core
    li.w    $t0, 0xA8        # DA mode, load/store cacheable, instructions cacheable, interrupts disabled, DMWn has no effect.
    csrwr   $t0, LOONGARCH_CSR_CRMD

    //
    // Make sure the legacy mode works properly.
    //
    la.pcrel  $t0, Jmp
    bstrins.d $t0, $zero, 63, 32
    jr        $t0

Jmp:
    invtlb  0x0, $zero, $zero

    # Set the PG and DMWn
    li.d    $t0, 0x8000000000000001
    csrwr   $t0, LOONGARCH_CSR_DMWIN0   # Uncache able address window, MMIO base: 0x8000000000000000
    li.d    $t0, 0x11                   # Cache able address window, PA == VA
    csrwr   $t0, LOONGARCH_CSR_DMWIN1

    li.w    $t0, 0xB0        # PG mode, load/store cacheable, instructions cacheable, disable intrrupts, DMWn effected.
    csrwr   $t0, LOONGARCH_CSR_CRMD

    //
    // Set PMCNT1.
    //
    li.w      $t0, (0x1 << 16)
    li.w      $t1, ((0x1 << 16) | 0x3FF)
    csrxchg   $t0, $t1, 0x200
    csrwr     $zero, 0x201

    # Enable all IPI
    li.w       $t0, 0xFFFFFFFF
    li.w       $t1, 0x1004
    iocsrwr.w  $t0, $t1

    # Enable FPE
    li.w    $t0, (0x1 << 0)
    csrxchg $t0, $t0, LOONGARCH_CSR_EUEN

    b startup

# The 64-bit main entry point and for restart after relocation.

	.org	0x500
	.globl	startup
startup:
    # Disable global intrrupts
    li.w    $t0, (0x1 << 2)
    csrxchg $zero, $t0, LOONGARCH_CSR_CRMD

    # Some of the startup actions are not thread safe. Use a mutex
    # to protect this section of code.
    la.pcrel  $t0, startup_mutex
    li.w        $t1, 0x1
Locked:
    amswap_db.w $t2, $t1, $t0
    bnez        $t2, Locked

Critical:
    # Set cores stack
    csrrd     $t0, LOONGARCH_CSR_CPUNUM
    li.d      $t1, AP_STACK_SIZE
    mul.d     $t0, $t0, $t1
    li.d      $t1, BSP_STACK_SIZE - LOCALS_SIZE
    add.d     $t0, $t0, $t1
    la.pcrel  $sp, _stacks
    add.d     $sp, $sp, $t0

SetException:
    # Only enable IPI intrrupt
    li.w    $t0, 0x1FFF
    csrxchg $zero, $t0, LOONGARCH_CSR_ECFG
    li.w    $t0, (0x1 << 12)
    csrxchg $t0, $t0, LOONGARCH_CSR_ECFG

    # Set exception base
    csrrd     $t0, LOONGARCH_CSR_ECFG
    bstrins.w $t0, $zero, 18, 16
    csrwr     $t0, LOONGARCH_CSR_ECFG
    la.pcrel  $t0, exception_entry
    csrwr     $t0, LOONGARCH_CSR_EBASE        # Exception and interrupts
    la.pcrel  $t0, exception_entry
    csrwr     $t0, LOONGARCH_CSR_TLBREBASE    # TLB refill

    # Enable global intrrupts
    li.w    $t0, (0x1 << 2)
    csrxchg $t0, $t0, LOONGARCH_CSR_CRMD

    bl  reloc

    # Release the startup mutex.
    la.pcrel    $t0, startup_mutex
    li.w        $t1, 0x0
    amswap_db.w $zero, $t1, $t0

    # Run the application.
    bl  main

	.previous

# Variables.

	.data
	.align	4

	.globl	ap_startup_addr
ap_startup_addr:
	.quad	0			# filled in at run time

	.globl	boot_params_addr
boot_params_addr:
	.quad	0

startup_mutex:
	.long	0

first_boot:
	.long	1

	.previous

# Startup stack.

	.bss
	.align	16

startup_stack_base:
	. = . + 64
startup_stack_top:

	.previous

# Main stack area.

	.section    ".stacks", "aw", @nobits
	.align  16

	. = . + STACKS_SIZE

	.previous
