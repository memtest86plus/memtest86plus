// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
//
// Derived from memtest86+ reloc.c:
//
// reloc.c - MemTest-86  Version 3.3
//
// Released under version 2 of the Gnu Public License.
// By Eric Biederman

#include <stddef.h>
#include <stdint.h>

#include "assert.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

// Dynamic section tag values

#define DT_NULL         0           // End of dynamic section
#define DT_PLTRELSZ     2           // Size in bytes of PLT relocs
#define DT_RELA         7           // Address of Rel relocs
#define DT_RELASZ       8           // Total size of Rel relocs
#define DT_RELAENT	9
#define DT_PLTREL       20          // Type of reloc in PLT
#define DT_JMPREL       23          // Address of PLT relocs
#define DT_NUM          34          // Number of tag values

// Relocation types

#define R_X86_64_NONE      0
#define R_X86_64_RELATIVE  8
#define R_LARCH_NONE       0
#define R_LARCH_RELATIVE   3
#define R_AARCH64_NONE     0
#define R_AARCH64_RELATIVE 1027

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef uint64_t    Elf64_Addr;
typedef int64_t     Elf64_Sxword;
typedef uint64_t    Elf64_Xword;

typedef struct
{
    Elf64_Sxword    d_tag;
    union
    {
        Elf64_Xword d_val;
        Elf64_Addr  d_ptr;
    } d_un;
} Elf64_Dyn;

typedef struct
{
    Elf64_Addr      r_offset;
    Elf64_Xword     r_info;
    Elf64_Sxword    r_addend;
} Elf64_Rela;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

#define ELF64_R_TYPE(r_info)    ((r_info) & 0xffffffff)

/*
 * Return the run-time load address of the shared object.
 */
static inline Elf64_Addr __attribute__ ((unused)) get_load_address(void)
{
    Elf64_Addr addr;
#if defined(__x86_64__)
    __asm__ __volatile__ (
        "leaq _start(%%rip), %0"
        : "=r" (addr)
        :
        : "cc"
    );
#elif defined(__loongarch_lp64)
    __asm__ __volatile__ (
        "la.pcrel %0, _start"
        : "=r" (addr)
        :
        : "memory"
    );
#elif defined(__aarch64__)
    __asm__ __volatile__ (
        "adrp %0, _start            \n\t"
        "add  %0, %0, :lo12:_start"
        : "=r" (addr)
        :
        : "memory"
    );
#endif
    return addr;
}

/*
 * Return the link-time address of _DYNAMIC. Conveniently, this is the first
 * element of the GOT.
 */
static inline Elf64_Addr __attribute__ ((unused)) get_dynamic_section_offset(void)
{
    Elf64_Addr offs;
#if defined(__x86_64__)
    __asm__ __volatile__ (
        "movq _GLOBAL_OFFSET_TABLE_(%%rip), %0"
        : "=r" (offs)
        :
        : "cc"
    );
#elif defined(__loongarch_lp64)
    __asm__ __volatile__ (
        "la.pcrel $t0, _GLOBAL_OFFSET_TABLE_ \n\t"
        "ld.d %0, $t0, 0x0"
        : "=r" (offs)
        :
        : "$t0", "memory"
    );
#elif defined(__aarch64__)
    // On AArch64 the GOT[0] convention is not reliable (the entry itself
    // needs a dynamic relocation), so compute the link-time offset of
    // _DYNAMIC PC-relatively instead. The program is linked at address 0,
    // so the offset is basically the distance from _start
    Elf64_Addr start_addr;
    __asm__ __volatile__ (
        "adrp %0, _DYNAMIC          \n\t"
        "add  %0, %0, :lo12:_DYNAMIC"
        : "=r" (offs)
        :
        : "memory"
    );
    __asm__ __volatile__ (
        "adrp %0, _start            \n\t"
        "add  %0, %0, :lo12:_start"
        : "=r" (start_addr)
        :
        : "memory"
    );
    offs -= start_addr;
#endif
    return offs;
}

static void get_dynamic_info(Elf64_Dyn *dyn_section, Elf64_Addr load_offs, Elf64_Dyn *dyn_info[DT_NUM])
{
    Elf64_Dyn *dyn = dyn_section;
    while (dyn->d_tag != DT_NULL) {
        if (dyn->d_tag < DT_NUM) {
            dyn_info[dyn->d_tag] = dyn;
        }
        dyn++;
    }

    if (dyn_info[DT_RELA] != NULL) {
        assert(dyn_info[DT_RELAENT]->d_un.d_val == sizeof(Elf64_Rela));
        dyn_info[DT_RELA]->d_un.d_ptr += load_offs;
    }
    if (dyn_info[DT_PLTREL] != NULL) {
        assert(dyn_info[DT_PLTREL]->d_un.d_val == DT_RELA);
    }
    if (dyn_info[DT_JMPREL] != NULL) {
        dyn_info[DT_JMPREL]->d_un.d_ptr += load_offs;
    }
}

/*
 * Slots are adjusted by the move delta, so a statically initialised pointer
 * variable must only ever hold pointers into the program image; anything
 * else (firmware tables, heap, MMIO) belongs in a BSS variable.
 */
static void do_relocation(Elf64_Addr load_addr, Elf64_Addr load_offs, const Elf64_Rela *rel)
{
    Elf64_Addr *target_addr = (Elf64_Addr *)(load_addr + rel->r_offset);
    if ((ELF64_R_TYPE(rel->r_info) == R_X86_64_RELATIVE) ||
        (ELF64_R_TYPE(rel->r_info) == R_LARCH_RELATIVE)  ||
        (ELF64_R_TYPE(rel->r_info) == R_AARCH64_RELATIVE)) {
        if (load_offs == load_addr) {
            *target_addr = load_addr + rel->r_addend;
        } else {
            *target_addr += load_offs;
        }
        return;
    }
    if ((ELF64_R_TYPE(rel->r_info) == R_X86_64_NONE) ||
        (ELF64_R_TYPE(rel->r_info) == R_LARCH_NONE)) {
        return;
    }
    assert(! "unexpected dynamic reloc type");
}

static void do_relocations(Elf64_Addr load_addr, Elf64_Addr load_offs, Elf64_Addr rel_addr, Elf64_Addr rel_size)
{
    const Elf64_Rela *rel_start = (const Elf64_Rela *)(rel_addr);
    const Elf64_Rela *rel_end   = (const Elf64_Rela *)(rel_addr + rel_size);

    for (const Elf64_Rela *rel = rel_start; rel < rel_end; rel++) {
        do_relocation(load_addr, load_offs, rel);
    }
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void reloc(void)
{
    static volatile Elf64_Addr last_load_addr = 0;

    Elf64_Dyn *dyn_info[DT_NUM];

    for (int i = 0; i < DT_NUM; i++) {
        dyn_info[i] = NULL;
    }

    Elf64_Addr load_addr = get_load_address();
    Elf64_Addr load_offs = load_addr - last_load_addr;
    if (load_addr == last_load_addr) {
        return;
    }
    last_load_addr = load_addr;

    Elf64_Dyn *dyn_section = (Elf64_Dyn *)(load_addr + get_dynamic_section_offset());
    get_dynamic_info(dyn_section, load_offs, dyn_info);

    do_relocations(load_addr, load_offs, dyn_info[DT_RELA]->d_un.d_ptr, dyn_info[DT_RELASZ]->d_un.d_val);

    // The PLT-related tags are absent if there are no PLT relocations
    // (e.g. on AArch64).
    if (dyn_info[DT_PLTREL] != NULL && dyn_info[DT_PLTREL]->d_un.d_val == DT_RELA) {
        do_relocations(load_addr, load_offs, dyn_info[DT_JMPREL]->d_un.d_ptr, dyn_info[DT_PLTRELSZ]->d_un.d_val);
    }
}
