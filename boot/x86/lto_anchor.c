// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 MemTest86+ contributors
//
// This translation unit exists only to support LTO building. The symbols
// referenced below are defined by the startup assembly code or by the
// linker script, i.e. outside of the C world, and some of them are not
// referenced from the C code at all. When the LTO bytecode is linked, the
// linker trusts the symbol resolution information provided by the compiler
// and treats symbols that were never seen by it as preemptible, even if
// they carry hidden visibility in the object that defines them. Any
// PC-relative relocation given to such a symbol is then rejected with
// "recompile with -fPIC" when creating the shared image. To prevent that,
// we claim every one of these symbols here, with hidden visibility, which
// makes the compiler describe them as defined, non-preemptible, and binds
// all references to them statically. The value of each claim is consumed
// by an empty asm statement, so this code generates no data and no
// behaviour; the function itself is a handful of leaq instructions whose
// only purpose is to force the symbol references to exist.

#include <stdint.h>
#include "boot.h"

#define MT_HIDDEN __attribute__((visibility("hidden")))

extern uint8_t	gdt[] MT_HIDDEN;
extern uint8_t	gdt_end[] MT_HIDDEN;
extern uint8_t	_bss[] MT_HIDDEN;
extern uint8_t	efi_boot[] MT_HIDDEN;
extern uint8_t	efi_handover[] MT_HIDDEN;
#if !defined(__x86_64__)
extern uint8_t	idt[] MT_HIDDEN;
extern uint8_t	idt_descr[] MT_HIDDEN;
#endif

#define MT_ANCHOR(sym) asm volatile ("" : : "r" ((uintptr_t)(sym)))

__attribute__((used, noinline)) void lto_symbol_anchor(void)
{
    MT_ANCHOR(gdt);
    MT_ANCHOR(gdt_end);
    MT_ANCHOR(_bss);
    MT_ANCHOR(_stacks);
    MT_ANCHOR(_start);
    MT_ANCHOR(_end);
    MT_ANCHOR(startup32);
#if defined(__x86_64__)
    MT_ANCHOR(startup64);
#endif
    MT_ANCHOR(startup);
    MT_ANCHOR(efi_boot);
    MT_ANCHOR(efi_handover);
#if !defined(__x86_64__)
    MT_ANCHOR(idt);
    MT_ANCHOR(idt_descr);
#endif
    MT_ANCHOR(ap_trampoline);
    MT_ANCHOR(ap_trampoline_end);
    MT_ANCHOR(&boot_params_addr);
    MT_ANCHOR(&ap_startup_addr);
    MT_ANCHOR(pml4);
    MT_ANCHOR(pdp);
    MT_ANCHOR(pd0);
    MT_ANCHOR(pd1);
    MT_ANCHOR(pd2);
    MT_ANCHOR(pd3);
}

#undef MT_HIDDEN
#undef MT_ANCHOR
