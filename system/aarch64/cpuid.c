// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester
//
// ARM64 (AArch64) CPU identification via the MIDR_EL1 register

#include <stdbool.h>
#include <stdint.h>

#include "cpuid.h"

#include "string.h"

#include "registers.h"

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef struct {
    uint16_t    part_num;
    const char  name[16];   // 15 chars + NUL max; GCC < 15 doesn't warn if the NUL gets dropped
} cpu_part_t;

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

static const cpu_part_t arm_parts[] = {
    { 0xD00, "Foundation"   },
    { 0xD03, "Cortex-A53"   },
    { 0xD04, "Cortex-A35"   },
    { 0xD05, "Cortex-A55"   },
    { 0xD07, "Cortex-A57"   },
    { 0xD08, "Cortex-A72"   },
    { 0xD09, "Cortex-A73"   },
    { 0xD0A, "Cortex-A75"   },
    { 0xD0B, "Cortex-A76"   },
    { 0xD0C, "Neoverse N1"  },
    { 0xD0D, "Cortex-A77"   },
    { 0xD0E, "Cortex-A76AE" },
    { 0xD0F, "AEMv8"        },
    { 0xD40, "Neoverse V1"  },
    { 0xD41, "Cortex-A78"   },
    { 0xD42, "Cortex-A78AE" },
    { 0xD44, "Cortex-X1"    },
    { 0xD46, "Cortex-A510"  },
    { 0xD47, "Cortex-A710"  },
    { 0xD48, "Cortex-X2"    },
    { 0xD49, "Neoverse N2"  },
    { 0xD4B, "Cortex-A78C"  },
    { 0xD4C, "Cortex-X1C"   },
    { 0xD4D, "Cortex-A715"  },
    { 0xD4E, "Cortex-X3"    },
    { 0xD4F, "Neoverse V2"  },
    { 0xD80, "Cortex-A520"  },
    { 0xD81, "Cortex-A720"  },
    { 0xD82, "Cortex-X4"    },
    { 0xD83, "Neoverse V3AE"},
    { 0xD84, "Neoverse V3"  },
    { 0xD85, "Cortex-X925"  },
    { 0xD87, "Cortex-A725"  },
    { 0xD89, "Cortex-A720AE"},
    { 0xD8E, "Neoverse N3"  },
    { 0, "" }
};

static const cpu_part_t qcom_parts[] = {
    { 0x001, "Oryon X1"     },
    { 0x800, "Kryo 2xx Au"  },
    { 0x801, "Kryo 2xx Ag"  },
    { 0x802, "Kryo 3xx Au"  },
    { 0x803, "Kryo 3xx Ag"  },
    { 0x804, "Kryo 4xx Au"  },
    { 0x805, "Kryo 4xx Ag"  },
    { 0xC00, "Falkor"       },
    { 0, "" }
};

static const cpu_part_t apple_parts[] = {
    { 0x022, "M1 Icestorm"  },
    { 0x023, "M1 Firestorm" },
    { 0x024, "M1 Ice Pro"   },
    { 0x025, "M1 Fire Pro"  },
    { 0x028, "M1 Ice Max"   },
    { 0x029, "M1 Fire Max"  },

    { 0x032, "M2 Blizzard"  },
    { 0x033, "M2 Avalanche" },
    { 0x034, "M2 Bliz Pro"  },
    { 0x035, "M2 Aval Pro"  },
    { 0x038, "M2 Bliz Max"  },
    { 0x039, "M2 Aval Max"  },

    { 0x042, "M3 Sawtooth"  },
    { 0x043, "M3 Everest"   },
    { 0x044, "M3 Saw Pro"   },
    { 0x045, "M3 Ever Pro"  },
    { 0x048, "M3 Saw Max"   },
    { 0x049, "M3 Ever Max"  },

    { 0x052, "M4 Sawtooth"  },
    { 0x053, "M4 Everest"   },
    { 0x054, "M4 Saw Pro"   },
    { 0x055, "M4 Ever Pro"  },
    { 0x058, "M4 Saw Max"   },
    { 0x059, "M4 Ever Max"  },
    { 0, "" }
};

static const cpu_part_t nvidia_parts[] = {
    { 0x000, "Denver"   },
    { 0x003, "Denver 2" },
    { 0x004, "Carmel"   },
    { 0x010, "Olympus"  },
    { 0, "" }
};

static const cpu_part_t microsoft_parts[] = {
    { 0xD49, "Cobalt 100"   },
    { 0, "" }
};

static const cpu_part_t cavium_parts[] = {
    { 0x0A1, "ThunderX"     },
    { 0x0A2, "ThunderX 81"  },
    { 0x0A3, "ThunderX 83"  },
    { 0x0AF, "ThunderX2"    },
    { 0x0B1, "OcteonTx2 98" },
    { 0x0B2, "OcteonTx2 96" },
    { 0x0B3, "OcteonTx2 95" },
    { 0x0B4, "OcteonTx2 95N"},
    { 0x0B5, "OcteonTx2 95M"},
    { 0x0B6, "OcteonTx2 95O"},
    { 0, "" }
};

//------------------------------------------------------------------------------
// Public Variables
//------------------------------------------------------------------------------

cpuid_info_t cpuid_info;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static const char *lookup_part(const cpu_part_t *parts, uint16_t part_num)
{
    for (int i = 0; parts[i].name[0] != '\0'; i++) {
        if (parts[i].part_num == part_num) {
            return parts[i].name;
        }
    }
    return NULL;
}

static size_t append_str(char *buffer, size_t offset, size_t size, const char *str)
{
    while (*str != '\0' && offset < (size - 1)) {
        buffer[offset++] = *str++;
    }
    buffer[offset] = '\0';
    return offset;
}

static size_t append_hex(char *buffer, size_t offset, size_t size, uint32_t value, int digits)
{
    for (int i = digits - 1; i >= 0 && offset < (size - 1); i--) {
        uint32_t digit = (value >> (i * 4)) & 0xF;
        buffer[offset++] = digit < 10 ? '0' + digit : 'A' + digit - 10;
    }
    buffer[offset] = '\0';
    return offset;
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void cpuid_init(void)
{
    uint64_t midr = read_sysreg(midr_el1);

    const char *vendor = NULL;
    const char *part   = NULL;

    switch (MIDR_IMPLEMENTER(midr)) {
      case 0x41:
        vendor = "Arm";
        part   = lookup_part(arm_parts, MIDR_PART_NUM(midr));
        break;
      case 0x51:
        vendor = "Qualcomm";
        part   = lookup_part(qcom_parts, MIDR_PART_NUM(midr));
        break;
      case 0x61:
        vendor = "Apple";
        part   = lookup_part(apple_parts, MIDR_PART_NUM(midr));
        break;
      case 0xC0:
        vendor = "Ampere";
        break;
      case 0x4E:
        vendor = "NVIDIA";
        part   = lookup_part(nvidia_parts, MIDR_PART_NUM(midr));
        break;
      case 0x46:
        vendor = "Fujitsu";
        break;
      case 0x43:
        vendor = "Cavium";
        part   = lookup_part(cavium_parts, MIDR_PART_NUM(midr));
        break;
      case 0x6D:
        vendor = "Microsoft";
        part   = lookup_part(microsoft_parts, MIDR_PART_NUM(midr));
        break;
      case 0x00:
        vendor = "QEMU";
        break;
      default:
        vendor = NULL;
        break;
    }

    (void)append_str(cpuid_info.vendor_id.str, 0, CPUID_VENDOR_STR_LENGTH,
                     vendor != NULL ? vendor : "Unknown");

    // Build the brand string.
    char *brand = cpuid_info.brand_id.str;
    size_t brand_size = CPUID_BRAND_STR_LENGTH;
    size_t len = 0;
    brand[0] = '\0';
    if (vendor != NULL) {
        len = append_str(brand, len, brand_size, vendor);
        len = append_str(brand, len, brand_size, " ");
    }
    if (part != NULL) {
        len = append_str(brand, len, brand_size, part);
    } else {
        // Fall back to the raw part number.
        len = append_str(brand, len, brand_size, "0x");
        len = append_hex(brand, len, brand_size, MIDR_PART_NUM(midr), 3);
    }

    cpuid_info.version.raw[0]        = (uint32_t)midr;
    cpuid_info.version.stepping      = MIDR_REVISION(midr);

    cpuid_info.flags.htt             = false;

    // CNTVCT_EL0 (read by get_tsc()) is always available, so advertise a TSC;
    // this lets the random-pattern tests seed their PRSG from a live counter.
    cpuid_info.flags.rdtsc           = true;

    cpuid_info.topology.core_count   = -1;
    cpuid_info.topology.thread_count = -1;
    cpuid_info.topology.is_hybrid    =  0;
    cpuid_info.topology.ecore_count  = -1;
    cpuid_info.topology.pcore_count  = -1;
}

core_type_t get_ap_hybrid_type(void)
{
    return CORE_PCORE;
}
