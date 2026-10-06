// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Lionel Debroux
//
// ------------------------
//
// Read-only Skylake / Cascade Lake / Cooper Lake Xeon DDR4 frequency, population and timings.
//


/* Channel functions: DID 2040h fn0 / 2044h fn4 / 2048h fn0 (device 0x0B/0x0D).
 * PMON function: fn2 (DID 2042h) on each channel device. */
static /*__attribute__((noinline))*/ void get_imc_config_intel_skx(void)
{
    unsigned int model = cpuid_info.version.extendedModel << 4 | cpuid_info.version.model;
    if (model != 0x55) return;

    for (int bus = 0; bus < PCI_MAX_BUS; bus++) {
        unsigned int channels = 0;
        bool valid = true;
        int pmon_dev = -1, pmon_func = 0;   // first populated MC channel monitor
        for (int mc = 0; mc < 2 && valid; mc++) {
            for (int ch = 0; ch < 3; ch++) {
                int dev = 10 + 2 * mc + ch / 2;
                int func = (ch % 2) * 4;
                uint32_t id = (uint32_t)(0x2040 + ch * 4) << 16 | PCI_VID_INTEL;
                if (pci_config_read32(bus, dev, func, 0) != id) continue;
                // Channel monitor (LMS/PMON) fn = func+2, DID 2042h + 4*ch (2042/2046/204A).
                if (pci_config_read32(bus, dev, func + 2, 0) != ((uint32_t)(0x2042 + ch * 4) << 16 | PCI_VID_INTEL)) {
                    valid = false;
                    break;
                }
                bool populated = false;
                for (int slot = 0; slot < 2; slot++) {
                    uint32_t mtr = pci_config_read32(bus, dev, func, 0x80 + 4 * slot);
                    if (mtr & (1 << 15)) populated = true;
                }
                if (!valid) break;
                if (populated) {
                    channels++;
                    if (pmon_dev < 0) { pmon_dev = dev; pmon_func = func + 2; }
                }
            }
        }
        if (!valid || !channels) continue;
        imc.type = "DDR4";
        imc.width = channels * 64;
        // Timings live in the extended configuration space of the
        // channel thermal-control function.
        // tRCD[4:0], tCL[13:9], tRAS[24:19], tRP[31:27].
        uintptr_t addr = pci_config_mmio_addr(bus, pmon_dev, pmon_func);
        if (addr && (*(volatile uint32_t *)addr & 0xFFFF) == PCI_VID_INTEL) {
            uint32_t timing = *(volatile uint32_t *)(addr + 0x200);
            imc.tCL_dec = 0;
            imc.tCL     = (timing >> 9)  & 0x1F;
            imc.tRCD    =  timing        & 0x1F;
            imc.tRP     = (timing >> 27) & 0x1F;
            imc.tRAS    = (timing >> 19) & 0x3F;
        }
        // Measure DCLK through the channel PMON fixed counter.
        imc.freq = get_imc_freq_intel_ep(bus, pmon_dev, pmon_func);
    }
}
