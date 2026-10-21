// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 MemTest86+ contributors
//
// See boot/x86/lto_anchor.c for the rationale. This translation unit claims
// the startup assembly and linker-script symbols of the AArch64 build that
// are not referenced from the C code, so that LTO links bind all references
// to them statically instead of treating them as preemptible.

#include <stdint.h>
#include "boot.h"

#define MT_HIDDEN __attribute__((visibility("hidden")))

extern uint8_t	_start[] MT_HIDDEN;
extern uint8_t	efi_boot[] MT_HIDDEN;
extern uint8_t	vectors[] MT_HIDDEN;
extern uint8_t	_bss[] MT_HIDDEN;
extern uint8_t	_etext[] MT_HIDDEN;
extern uint8_t	_etext_ro[] MT_HIDDEN;
extern uint8_t	_data[] MT_HIDDEN;
extern uint8_t	_edata[] MT_HIDDEN;
extern uint8_t	_DYNAMIC[] MT_HIDDEN;

#define MT_ANCHOR(sym) asm volatile ("" : : "r" ((uintptr_t)(sym)))

__attribute__((used, noinline)) void lto_symbol_anchor(void)
{
    MT_ANCHOR(_start);
    MT_ANCHOR(_end);
    MT_ANCHOR(startup32);
    MT_ANCHOR(startup64);
    MT_ANCHOR(startup);
    MT_ANCHOR(efi_boot);
    MT_ANCHOR(vectors);
    MT_ANCHOR(&boot_params_addr);
    MT_ANCHOR(&ap_startup_addr);
    MT_ANCHOR(_stacks);
    MT_ANCHOR(_bss);
    MT_ANCHOR(_etext);
    MT_ANCHOR(_etext_ro);
    MT_ANCHOR(_data);
    MT_ANCHOR(_edata);
    MT_ANCHOR(_DYNAMIC);
}

#undef MT_HIDDEN
#undef MT_ANCHOR
