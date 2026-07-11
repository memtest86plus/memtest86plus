// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020-2022 Martin Whitaker.
// Copyright (C) 2026 Sam Demeulemeester.
//
// PCI configuration space access on ARM64 via the ECAM region described by
// the ACPI MCFG table. If no MCFG table is present (eg platform devices
// only, all reads return all-1 and writes are ignored

#include <stdbool.h>
#include <stdint.h>

#include "acpi.h"

#include "memsize.h"
#include "mmio.h"
#include "pmem.h"
#include "vmem.h"

#include "registers.h"

#include "pci.h"

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef struct __attribute__((packed)) {
    uint64_t    base_addr;
    uint16_t    segment;
    uint8_t     start_bus;
    uint8_t     end_bus;
    uint32_t    reserved;
} mcfg_entry_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static uintptr_t    ecam_base = 0;
static int          ecam_start_bus = 0;
static int          ecam_end_bus = -1;

static uint64_t     ecam_phys = 0;

// Ranges handed out by pci_alloc_mmio, to avoid handing out overlaps
#define MAX_MMIO_ALLOCS 8

static struct {
    uintptr_t   base;
    uintptr_t   size;
} mmio_allocs[MAX_MMIO_ALLOCS];

static int num_mmio_allocs = 0;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static void ecam_init(void);

static bool ecam_initialized = false;

static uintptr_t ecam_addr(int bus, int dev, int func, int reg)
{
    // pci_init() is called before acpi_init(), so the MCFG table is not
    // known at that point. Initialize lazily instead
    if (!ecam_initialized) {
        ecam_init();
    }
    if (ecam_base == 0 || bus < ecam_start_bus || bus > ecam_end_bus) {
        return 0;
    }
    return ecam_base + (((uintptr_t)(bus - ecam_start_bus) << 20)
                     |  ((uintptr_t)dev  << 15)
                     |  ((uintptr_t)func << 12)
                     |  (reg & 0xFFF));
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

static bool overlaps_ram(uintptr_t base, uintptr_t size)
{
    for (int i = 0; i < pm_map_size; i++) {
        uint64_t region_start = (uint64_t)pm_map[i].start << PAGE_SHIFT;
        uint64_t region_end   = (uint64_t)pm_map[i].end   << PAGE_SHIFT;
        if (base < region_end && region_start < (base + size)) {
            return true;
        }
    }
    return false;
}

// Allocate top-down within [win_start, win_end), aligned to size (BAR
// alignment equals BAR size), avoiding our own previous allocations.
// Firmware allocates bottom-up, so the top of a window is most likely free.
static uintptr_t alloc_in_window(uintptr_t win_start, uintptr_t win_end, uintptr_t size)
{
    if (num_mmio_allocs == MAX_MMIO_ALLOCS || size == 0 || win_end < size) {
        return 0;
    }

    uintptr_t candidate = (win_end - size) & ~(size - 1);
    bool conflict = true;
    while (conflict && candidate >= win_start) {
        conflict = false;
        for (int i = 0; i < num_mmio_allocs; i++) {
            if (candidate < (mmio_allocs[i].base + mmio_allocs[i].size)
             && mmio_allocs[i].base < (candidate + size)) {
                conflict = true;
                candidate = (mmio_allocs[i].base - size) & ~(size - 1);
                break;
            }
        }
    }
    if (candidate < win_start || overlaps_ram(candidate, size)) {
        return 0;
    }

    mmio_allocs[num_mmio_allocs].base = candidate;
    mmio_allocs[num_mmio_allocs].size = size;
    num_mmio_allocs++;

    return candidate;
}

// Find the bridge whose secondary bus is the given bus, and make sure its
// memory window and command register allow access to the devices behind it.
static bool find_bridge_window(int bus, uintptr_t *win_start, uintptr_t *win_end)
{
    for (int b = 0; b < PCI_MAX_BUS; b++) {
        for (int d = 0; d < PCI_MAX_DEV; d++) {
            for (int f = 0; f < PCI_MAX_FUNC; f++) {
                uint16_t vendor_id = pci_config_read16(b, d, f, 0x00);
                uint8_t  hdr_type  = pci_config_read8 (b, d, f, 0x0e);
                if (vendor_id == 0xffff) {
                    if (f == 0) break;
                    continue;
                }
                if ((hdr_type & 0x7f) == 1 && pci_config_read8(b, d, f, 0x19) == bus) {
                    uint16_t mem_base  = pci_config_read16(b, d, f, 0x20);
                    uint16_t mem_limit = pci_config_read16(b, d, f, 0x22);
                    uintptr_t base  =  (uintptr_t)(mem_base  & 0xFFF0) << 16;
                    uintptr_t limit = ((uintptr_t)(mem_limit & 0xFFF0) << 16) | 0xFFFFF;
                    if (base == 0 || base > limit) {
                        return false;
                    }
                    // Enable memory decode and bus mastering on the bridge.
                    uint16_t command = pci_config_read16(b, d, f, 0x04);
                    pci_config_write16(b, d, f, 0x04, command | 0x0006);
                    *win_start = base;
                    *win_end   = limit + 1;
                    return true;
                }
                if (f == 0 && (hdr_type & 0x80) == 0) break;
            }
        }
    }
    return false;
}

uintptr_t pci_alloc_mmio(int bus, int dev, int func, int bar_reg, uintptr_t size)
{
    if (ecam_base == 0 || size == 0 || (size & (size - 1)) != 0) {
        return 0;
    }

    uintptr_t win_start = 0;
    uintptr_t win_end   = 0;

    if (!find_bridge_window(bus, &win_start, &win_end)) {
        // Device on a root bus: the host bridge MMIO aperture is only
        // described in AML, which we can't parse. Use known windows.
        uint32_t host_bridge_id = pci_config_read32(0, 0, 0, 0x00);
        if (host_bridge_id == 0x00081b36) {
            // Fixed low MMIO window for QEMU (DBG))
            win_start = 0x10000000;
            win_end   = 0x3EFF0000;
        } else if (ecam_phys != 0 && ecam_phys <= 0x100000000ULL) {
            // Heuristic: on many platforms with a low ECAM, the MMIO window
            // sits directly below it. overlaps_ram() guards against RAM.
            win_end   = ecam_phys;
            win_start = win_end >= 0x10000000 ? win_end - 0x10000000 : 0;
        } else {
            return 0;
        }
    }

    uintptr_t base = alloc_in_window(win_start, win_end, size);
    if (base == 0) {
        return 0;
    }

    // Program the BAR (and the upper half for a 64-bit BAR)
    bool is_64bit = (pci_config_read32(bus, dev, func, bar_reg) & 0x6) == 0x4;
    pci_config_write32(bus, dev, func, bar_reg, base);
    if (is_64bit) {
        pci_config_write32(bus, dev, func, bar_reg + 4, 0);
    }

    return base;
}

static void ecam_init(void)
{
    if (acpi_config.mcfg_addr == 0) {
        return;
    }
    ecam_initialized = true;

    // Qualcomm PCIe root complexes are not ECAM-compliant: config space
    // accesses to unclocked controllers or unimplemented devices stall the
    // interconnect (hard hang, then watchdog reset), even though the
    // firmware publishes an MCFG table. Nothing we need lives on PCI on
    // these SoCs (USB is a platform device), so leave the ECAM disabled.
    if (MIDR_IMPLEMENTER(read_sysreg(midr_el1)) == 0x51) {
        return;
    }

    rsdt_header_t *mcfg = (rsdt_header_t *)map_region(acpi_config.mcfg_addr, sizeof(rsdt_header_t), true);
    if (mcfg == NULL) return;

    // Reject truncated tables, so the entry count below can't underflow.
    if (mcfg->length < sizeof(rsdt_header_t) + 8) {
        return;
    }

    mcfg = (rsdt_header_t *)map_region(acpi_config.mcfg_addr, mcfg->length, true);
    if (mcfg == NULL) return;

    if (acpi_checksum(mcfg, mcfg->length) != 0) {
        return;
    }

    // The MCFG entries start after the table header and an 8 byte reserved
    // field. Use the first entry for PCI segment 0.
    uintptr_t first_entry = (uintptr_t)mcfg + sizeof(rsdt_header_t) + 8;
    int num_entries = (mcfg->length - sizeof(rsdt_header_t) - 8) / sizeof(mcfg_entry_t);
    for (int i = 0; i < num_entries; i++) {
        mcfg_entry_t *entry = (mcfg_entry_t *)(first_entry + i * sizeof(mcfg_entry_t));
        if (entry->segment == 0) {
            if (entry->start_bus > entry->end_bus) {
                continue;   // malformed entry
            }
            size_t ecam_size = ((size_t)(entry->end_bus - entry->start_bus) + 1) << 20;
            ecam_phys      = entry->base_addr;
            ecam_base      = map_region(entry->base_addr, ecam_size, false);
            ecam_start_bus = entry->start_bus;
            ecam_end_bus   = entry->end_bus;
            break;
        }
    }
}

void pci_init(void)
{
    // The ACPI tables have not been parsed yet when this is called, so the
    // ECAM setup is done lazily on the first config space access instead.
}

// Qualcomm Snapdragon X (X1E80100 family): the USB controllers are DWC3
// platform devices, which expose the standard XHCI register interface at
// the core base address when operating in host mode. Base addresses from
// the Linux devicetree (arch/arm64/boot/dts/qcom/x1e80100.dtsi). Only the
// two primary USB-C controllers for now: accessing an unclocked block
// stalls the interconnect, and these two are the most likely to have been
// initialized by the firmware (boot keyboard support).
static const uintptr_t x1e80100_usb_bases[] = {
    0x0a600000,     // usb_1_ss0 (USB-C port 0)
    0x0a800000,     // usb_1_ss1 (USB-C port 1)
    0x0a400000,     // usb_mp (multiport: USB-A)
    // 0x0aa00000,  // usb_1_ss2 (USB-C port 2, not on all machines)
    // 0x0a200000,  // usb_2
};

bool platform_usb_controller(int index, uintptr_t *base_addr)
{
    uint64_t midr = read_sysreg(midr_el1);

    if (MIDR_IMPLEMENTER(midr) != 0x51 || MIDR_PART_NUM(midr) != 0x001) {
        return false;
    }
    if (index < 0 || index >= (int)(sizeof(x1e80100_usb_bases) / sizeof(x1e80100_usb_bases[0]))) {
        return false;
    }
    *base_addr = x1e80100_usb_bases[index];
    return true;
}

uint8_t pci_config_read8(int bus, int dev, int func, int reg)
{
    uintptr_t addr = ecam_addr(bus, dev, func, reg);
    if (addr == 0) return 0xFF;
    return mmio_read8((uint8_t *)addr);
}

uint16_t pci_config_read16(int bus, int dev, int func, int reg)
{
    uintptr_t addr = ecam_addr(bus, dev, func, reg);
    if (addr == 0) return 0xFFFF;
    return mmio_read16((uint16_t *)addr);
}

uint32_t pci_config_read32(int bus, int dev, int func, int reg)
{
    uintptr_t addr = ecam_addr(bus, dev, func, reg);
    if (addr == 0) return 0xFFFFFFFF;
    return mmio_read32((uint32_t *)addr);
}

void pci_config_write8(int bus, int dev, int func, int reg, uint8_t value)
{
    uintptr_t addr = ecam_addr(bus, dev, func, reg);
    if (addr == 0) return;
    mmio_write8((uint8_t *)addr, value);
}

void pci_config_write16(int bus, int dev, int func, int reg, uint16_t value)
{
    uintptr_t addr = ecam_addr(bus, dev, func, reg);
    if (addr == 0) return;
    mmio_write16((uint16_t *)addr, value);
}

void pci_config_write32(int bus, int dev, int func, int reg, uint32_t value)
{
    uintptr_t addr = ecam_addr(bus, dev, func, reg);
    if (addr == 0) return;
    mmio_write32((uint32_t *)addr, value);
}

// -------------
// LPC Functions
// -------------

// There is no LPC bus on this architecture.

void lpc_outb(uint8_t cmd __attribute__((unused)), uint8_t data __attribute__((unused)))
{
}

uint8_t lpc_inb(uint8_t reg __attribute__((unused)))
{
    return 0xFF;
}
