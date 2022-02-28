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
#define DT_REL          17          // Address of Rel relocs
#define DT_RELSZ        18          // Total size of Rel relocs
#define DT_RELENT	19
#define DT_PLTREL       20          // Type of reloc in PLT
#define DT_JMPREL       23          // Address of PLT relocs
#define DT_NUM          34          // Number of tag values

// Relocation types

#define R_386_NONE      0
#define R_386_RELATIVE  8

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef uint32_t    Elf32_Addr;
typedef int32_t     Elf32_Sword;
typedef uint32_t    Elf32_Word;

typedef struct
{
    Elf32_Sword     d_tag;
    union
    {
        Elf32_Word  d_val;
        Elf32_Addr  d_ptr;
    } d_un;
} Elf32_Dyn;

typedef struct
{
    Elf32_Addr      r_offset;
    Elf32_Word      r_info;
} Elf32_Rel;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

#define ELF32_R_TYPE(r_info)    ((r_info) & 0xff)

/*
 * Return the run-time load address of the shared object. This must be inlined
 * in a function which uses global data.
 */
static inline Elf32_Addr __attribute__ ((unused)) get_load_address(void)
{
    Elf32_Addr addr;
    __asm__ __volatile__ (
        "leal _start@GOTOFF(%%ebx), %0"
        : "=r" (addr)
        :
        : "cc"
    );
    return addr;
}

/*
 * Return the link-time address of _DYNAMIC. Conveniently, this is the first
 * element of the GOT. This must be inlined in a function which uses global
 * data.
 */
static inline Elf32_Addr __attribute__ ((unused)) get_dynamic_section_offset(void)
{
    register Elf32_Addr *got __asm__ ("%ebx");
    return *got;
}

static void get_dynamic_info(Elf32_Dyn *dyn_section, Elf32_Addr load_offs, Elf32_Dyn *dyn_info[DT_NUM])
{
    Elf32_Dyn *dyn = dyn_section;
    while (dyn->d_tag != DT_NULL) {
        if (dyn->d_tag < DT_NUM) {
            dyn_info[dyn->d_tag] = dyn;
        }
        dyn++;
    }

    if (dyn_info[DT_REL] != NULL) {
        assert(dyn_info[DT_RELENT]->d_un.d_val == sizeof(Elf32_Rel));
        dyn_info[DT_REL]->d_un.d_ptr += load_offs;
    }
    if (dyn_info[DT_PLTREL] != NULL) {
        assert(dyn_info[DT_PLTREL]->d_un.d_val == DT_REL);
    }
    if (dyn_info[DT_JMPREL] != NULL) {
        dyn_info[DT_JMPREL]->d_un.d_ptr += load_offs;
    }
}

static void do_relocation(Elf32_Addr load_addr, Elf32_Addr load_offs, const Elf32_Rel *rel)
{
    Elf32_Addr *target_addr = (Elf32_Addr *)(load_addr + rel->r_offset);
    if (ELF32_R_TYPE(rel->r_info) == R_386_RELATIVE) {
        *target_addr += load_offs;
        return;
    }
    if (ELF32_R_TYPE(rel->r_info) == R_386_NONE) {
        return;
    }
    assert(! "unexpected dynamic reloc type");
}

static void do_relocations(Elf32_Addr load_addr, Elf32_Addr load_offs, Elf32_Addr rel_addr, Elf32_Addr rel_size)
{
    const Elf32_Rel *rel_start = (const Elf32_Rel *)(rel_addr);
    const Elf32_Rel *rel_end   = (const Elf32_Rel *)(rel_addr + rel_size);

    for (const Elf32_Rel *rel = rel_start; rel < rel_end; rel++) {
        do_relocation(load_addr, load_offs, rel);
    }
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void reloc(void)
{
    static volatile Elf32_Addr last_load_addr = 0;

    Elf32_Dyn *dyn_info[DT_NUM];

    for (int i = 0; i < DT_NUM; i++) {
        dyn_info[i] = NULL;
    }

    Elf32_Addr load_addr = get_load_address();
    Elf32_Addr load_offs = load_addr - last_load_addr;
    if (load_addr == last_load_addr) {
        return;
    }
    last_load_addr = load_addr;

    Elf32_Dyn *dyn_section = (Elf32_Dyn *)(load_addr + get_dynamic_section_offset());
    get_dynamic_info(dyn_section, load_offs, dyn_info);

    do_relocations(load_addr, load_offs, dyn_info[DT_REL]->d_un.d_ptr, dyn_info[DT_RELSZ]->d_un.d_val);

    if (dyn_info[DT_PLTREL]->d_un.d_val == DT_REL) {
        do_relocations(load_addr, load_offs, dyn_info[DT_JMPREL]->d_un.d_ptr, dyn_info[DT_PLTRELSZ]->d_un.d_val);
    }
}
