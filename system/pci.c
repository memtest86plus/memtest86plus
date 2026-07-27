// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2021 Martin Whitaker.
//
// Derived from memtest86+ pci.c:
//
// MemTest86+ V5.00 Specific code (GPL V2.0)
// By Samuel DEMEULEMEESTER, sdemeule@memtest.org
// http://www.x86-secret.com - http://www.memtest.org
// ----------------------------------------------------
// pci.c - MemTest-86  Version 3.2
//
// Released under version 2 of the Gnu Public License.
// By Chris Brady

#include <stdbool.h>
#include <stdint.h>

#include "boot.h"
#include "bootparams.h"

#include "acpi.h"
#include "cpuid.h"
#include "io.h"
#include "memsize.h"
#include "pmem.h"

#include "pci.h"
#include "unistd.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define PCI_CLASS_DEVICE        0x0a

#define PCI_CLASS_BRIDGE_HOST   0x0600

#define MAX_MMIO_ALLOCS         8
#define MAX_MMIO_CLAIMS         64

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef enum {
    PCI_CONFIG_TYPE_NONE  = 0,
    PCI_CONFIG_TYPE_1     = 1,
    PCI_CONFIG_TYPE_2     = 2
} pci_config_type_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static pci_config_type_t pci_config_type = PCI_CONFIG_TYPE_NONE;

// Ranges handed out by pci_alloc_mmio, to avoid handing out overlaps.
static struct {
    uintptr_t   base;
    uintptr_t   size;
} mmio_allocs[MAX_MMIO_ALLOCS];

static int num_mmio_allocs = 0;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static bool pci_sanity_check(void)
{
    // Do a trivial check to make certain we can see a host bridge.
    // There are reportedly some buggy chipsets from Intel and
    // Compaq where this test does not work, I will worry about
    // that when we support them.
    return pci_config_read16(0, 0, 0, PCI_CLASS_DEVICE) == PCI_CLASS_BRIDGE_HOST;
}

static void probe_config_type(void)
{
    uint8_t  tmpCFB;
    uint32_t tmpCF8;

    if (cpuid_info.vendor_id.str[0] == 'A' && cpuid_info.version.family == 0xf) {
        pci_config_type = PCI_CONFIG_TYPE_1;
        return;
    }

    // Check if configuration type 1 works.
    pci_config_type = PCI_CONFIG_TYPE_1;
    tmpCFB = inb(0xcfb);
    outb(0x01, 0xcfb);
    tmpCF8 = inl(0xcf8);
    outl(0x80000000, 0xcf8);
    if (inl(0xcf8) == 0x80000000 && pci_sanity_check()) {
        outl(tmpCF8, 0xcf8);
        outb(tmpCFB, 0xcfb);
        return;
    }
    outl(tmpCF8, 0xcf8);

    // Check if configuration type 2 works.
    pci_config_type = PCI_CONFIG_TYPE_2;
    outb(0x00, 0xcfb);
    outb(0x00, 0xcf8);
    outb(0x00, 0xcfa);
    if (inb(0xcf8) == 0x00 && inb(0xcfa) == 0x00 && pci_sanity_check()) {
        outb(tmpCFB, 0xcfb);
        return;
    }
    outb(tmpCFB, 0xcfb);

    // Nothing worked.
    pci_config_type = PCI_CONFIG_TYPE_NONE;
}

static void set_pci_config1_addr(int bus, int dev, int func, int reg)
{
    uint32_t addr = 0x80000000
                  | (reg  & 0xf00) << 16
                  | (bus  & 0xff)  << 16
                  | (dev  & 0x1f)  << 11
                  | (func & 0x07)  << 8
                  | (reg  & 0xfc);

    outl(addr, 0xcf8);
}

static void set_pci_config2_bus_func(int bus, int func)
{
    outb(0xf0 | (func & 0x7) << 1, 0xcf8);
    outb(bus, 0xcfa);
}

static int pci_config2_access_addr(int dev, int reg)
{
    return 0xc000 | (dev & 0x1f) << 8 | (reg & 0xff);
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void pci_init(void)
{
    const boot_params_t *boot_params = (boot_params_t *)boot_params_addr;
    if (boot_params->efi_info.loader_signature) {
        // On UEFI systems we can assume configuration type 1.
        pci_config_type = PCI_CONFIG_TYPE_1;
    } else {
        probe_config_type();
    }
}

uint8_t pci_config_read8(int bus, int dev, int func, int reg)
{
    uint8_t value;

    switch (pci_config_type) {
      case PCI_CONFIG_TYPE_1:
        set_pci_config1_addr(bus, dev, func, reg);
        return inb(0xcfc + (reg & 0x3));
      case PCI_CONFIG_TYPE_2:
        set_pci_config2_bus_func(bus, func);
        value = inb(pci_config2_access_addr(dev, reg));
        outb(0, 0xcf8);
        return value;
      default:
        return 0xFF;
    }
}

uint16_t pci_config_read16(int bus, int dev, int func, int reg)
{
    uint16_t value;

    switch (pci_config_type) {
      case PCI_CONFIG_TYPE_1:
        set_pci_config1_addr(bus, dev, func, reg);
        return inw(0xcfc + (reg & 0x2));
      case PCI_CONFIG_TYPE_2:
        set_pci_config2_bus_func(bus, func);
        value = inw(pci_config2_access_addr(dev, reg));
        outb(0, 0xcf8);
        return value;
      default:
        return 0xFFFF;
    }
}

uint32_t pci_config_read32(int bus, int dev, int func, int reg)
{
    uint32_t value;

    switch (pci_config_type) {
      case PCI_CONFIG_TYPE_1:
        set_pci_config1_addr(bus, dev, func, reg);
        return inl(0xcfc);
      case PCI_CONFIG_TYPE_2:
        set_pci_config2_bus_func(bus, func);
        value = inl(pci_config2_access_addr(dev, reg));
        outb(0, 0xcf8);
        return value;
      default:
        return 0xFFFFFFFF;
    }
}

void pci_config_write8(int bus, int dev, int func, int reg, uint8_t value)
{
    switch (pci_config_type)
    {
      case PCI_CONFIG_TYPE_1:
        set_pci_config1_addr(bus, dev, func, reg);
        outb(value, 0xcfc + (reg & 0x3));
        break;
      case PCI_CONFIG_TYPE_2:
        set_pci_config2_bus_func(bus, func);
        outb(value, pci_config2_access_addr(dev, reg));
        outb(0, 0xcf8);
        break;
      default:
        break;
    }
}

void pci_config_write16(int bus, int dev, int func, int reg, uint16_t value)
{
    switch (pci_config_type)
    {
      case PCI_CONFIG_TYPE_1:
        set_pci_config1_addr(bus, dev, func, reg);
        outw(value, 0xcfc + (reg & 0x2));
        break;
      case PCI_CONFIG_TYPE_2:
        set_pci_config2_bus_func(bus, func);
        outw(value, pci_config2_access_addr(dev, reg));
        outb(0, 0xcf8);
        break;
      default:
        break;
    }
}

void pci_config_write32(int bus, int dev, int func, int reg, uint32_t value)
{
    switch (pci_config_type)
    {
      case PCI_CONFIG_TYPE_1:
        set_pci_config1_addr(bus, dev, func, reg);
        outl(value, 0xcfc);
        break;
      case PCI_CONFIG_TYPE_2:
        set_pci_config2_bus_func(bus, func);
        outl(value, pci_config2_access_addr(dev, reg));
        outb(0, 0xcf8);
        break;
      default:
        break;
    }
}


uintptr_t pci_alloc_mmio(int bus, int dev, int func, int bar_reg, uintptr_t size)
{
    if (num_mmio_allocs == MAX_MMIO_ALLOCS || size == 0 || (size & (size - 1)) != 0) {
        return 0;
    }

    // Collect the sub-4GB ranges already claimed by our allocations, the MCFG table, PCI BARs,
    // and PCI-PCI bridge windows. BAR sizes can't be probed non-destructively, so use the size
    // implied by the address alignment, clamped to [4KB, 64MB].
    uint64_t claimed[MAX_MMIO_CLAIMS][2];
    int num_claimed = 0;

    for (int i = 0; i < num_mmio_allocs; i++) {
        claimed[num_claimed][0] = mmio_allocs[i].base;
        claimed[num_claimed][1] = mmio_allocs[i].base + mmio_allocs[i].size;
        num_claimed++;
    }
    if (acpi_config.mcfg_addr != 0) {
        claimed[num_claimed][0] = acpi_config.mcfg_addr;
        claimed[num_claimed][1] = (uint64_t)acpi_config.mcfg_addr + 0x10000000;
        num_claimed++;
    }
    for (int b = 0; b < PCI_MAX_BUS; b++) {
        for (int d = 0; d < PCI_MAX_DEV; d++) {
            for (int f = 0; f < PCI_MAX_FUNC; f++) {
                uint16_t vendor_id = pci_config_read16(b, d, f, 0x00);
                uint8_t  hdr_type  = pci_config_read8 (b, d, f, 0x0e);
                if (vendor_id == 0xffff) {
                    if (f == 0) break;
                    continue;
                }
                int last_bar = 0;
                if ((hdr_type & 0x7f) == 0) last_bar = 0x24;
                if ((hdr_type & 0x7f) == 1) last_bar = 0x14;
                for (int reg = 0x10; reg <= last_bar; reg += 4) {
                    uint32_t bar_val = pci_config_read32(b, d, f, reg);
                    bool skip = (b == bus && d == dev && f == func && reg == bar_reg);
                    if (bar_val & 0x1) continue;
                    uint64_t addr = bar_val & ~(uint64_t)0xf;
                    if ((bar_val & 0x6) == 0x4) {
                        addr |= (uint64_t)pci_config_read32(b, d, f, reg + 4) << 32;
                        reg += 4;
                    }
                    if (skip || addr == 0 || addr > 0xffffffff) continue;
                    uint64_t extent = addr & ~(addr - 1);
                    if (extent < 0x1000)     extent = 0x1000;
                    if (extent > 0x04000000) extent = 0x04000000;
                    if (num_claimed == MAX_MMIO_CLAIMS) return 0;
                    claimed[num_claimed][0] = addr;
                    claimed[num_claimed][1] = addr + extent;
                    num_claimed++;
                }
                if ((hdr_type & 0x7f) == 1) {
                    // Both bridge memory windows; a prefetchable window based above 4GB is irrelevant.
                    uint64_t base  =  (uint64_t)(pci_config_read16(b, d, f, 0x20) & 0xfff0) << 16;
                    uint64_t limit = ((uint64_t)(pci_config_read16(b, d, f, 0x22) & 0xfff0) << 16) | 0xfffff;
                    uint16_t pref_base_reg = pci_config_read16(b, d, f, 0x24);
                    uint64_t pref_base  =  (uint64_t)(pref_base_reg & 0xfff0) << 16;
                    uint64_t pref_limit = ((uint64_t)(pci_config_read16(b, d, f, 0x26) & 0xfff0) << 16) | 0xfffff;
                    if ((pref_base_reg & 0xf) == 1 && pci_config_read32(b, d, f, 0x28) != 0) {
                        pref_base = pref_limit + 1;  // disables the claim below
                    }
                    if (num_claimed + 2 > MAX_MMIO_CLAIMS) return 0;
                    if (base <= limit) {
                        claimed[num_claimed][0] = base;
                        claimed[num_claimed][1] = limit + 1;
                        num_claimed++;
                    }
                    if (pref_base <= pref_limit) {
                        claimed[num_claimed][0] = pref_base;
                        claimed[num_claimed][1] = pref_limit + 1;
                        num_claimed++;
                    }
                }
                if (f == 0 && (hdr_type & 0x80) == 0) break;
            }
        }
    }

    // Allocate top-down in the gap between the top of low RAM and the I/O APIC, aligned to the
    // BAR size. Firmware allocates bottom-up, so the top of the gap is most likely free.
    uint64_t win_start = 0;
    for (int i = 0; i < pm_map_size; i++) {
        uint64_t region_end = (uint64_t)pm_map[i].end << PAGE_SHIFT;
        if (region_end <= 0x100000000ULL && region_end > win_start) {
            win_start = region_end;
        }
    }
    uint64_t win_end = 0xfec00000;
    if (win_start == 0 || win_start >= win_end) {
        return 0;
    }

    uint64_t candidate = (win_end - size) & ~((uint64_t)size - 1);
    bool conflict = true;
    while (conflict && candidate >= win_start) {
        conflict = false;
        for (int i = 0; i < num_claimed; i++) {
            if (candidate < claimed[i][1] && claimed[i][0] < candidate + size) {
                conflict = true;
                candidate = claimed[i][0] >= size ? (claimed[i][0] - size) & ~((uint64_t)size - 1) : 0;
                break;
            }
        }
    }
    if (conflict || candidate < win_start) {
        return 0;
    }

    mmio_allocs[num_mmio_allocs].base = candidate;
    mmio_allocs[num_mmio_allocs].size = size;
    num_mmio_allocs++;

    // Program the BAR (and the upper half for a 64-bit BAR).
    bool is_64bit = (pci_config_read32(bus, dev, func, bar_reg) & 0x6) == 0x4;
    pci_config_write32(bus, dev, func, bar_reg, candidate);
    if (is_64bit) {
        pci_config_write32(bus, dev, func, bar_reg + 4, 0);
    }

    return candidate;
}

// -------------
// LPC Functions
// -------------

void lpc_outb(uint8_t cmd, uint8_t data)
{
    outb(cmd, 0x2E);
    usleep(100);
    outb(data, 0x2F);
    usleep(100);
}

uint8_t lpc_inb(uint8_t reg)
{
    outb(reg, 0x2E);
    usleep(100);
    return inb(0x2F);
}

// ---------------------------------------
// AMD System Management Network Functions
// ---------------------------------------

uint32_t amd_smn_read(uint32_t adr)
{
  pci_config_write32(0, 0, 0, 0x60, adr);

  return pci_config_read32(0, 0, 0, 0x64);
}

void amd_smn_write(uint32_t adr, uint32_t data)
{
  pci_config_write32(0, 0, 0, 0x60, adr);
  pci_config_write32(0, 0, 0, 0x64, data);
}
