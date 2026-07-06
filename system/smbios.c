// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022-2026 Samuel Demeulemeester
//

#include "stdint.h"
#include "string.h"
#include "ctype.h"
#include "display.h"

#include "boot.h"
#include "bootparams.h"
#include "cpuinfo.h"
#include "efi.h"
#include "vmem.h"
#include "spd.h"
#include "smbios.h"

#define LINE_DMI 23

static const uint8_t *table_start = NULL;
static uint32_t table_length = 0; // 16-bit in SMBIOS v2, 32-bit in SMBIOS v3.

static const efi_guid_t SMBIOS2_GUID = { 0xeb9d2d31, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} };

// Some firmware (e.g. QEMU virt, ARM laptops) only publishes the 64-bit SMBIOS v3 entry point.
static const efi_guid_t SMBIOS3_GUID = { 0xf2fd1544, 0x9794, 0x4a2c, {0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94} };

// Consumers of dmi_memory_device read ->type without a NULL check, so point
// it at an all-zero struct (type 0 = undefined) until a real one is found.
static struct mem_dev null_mem_dev;

struct system_info *dmi_system_info;
struct baseboard_info *dmi_baseboard_info;
struct mem_dev *dmi_memory_device = &null_mem_dev;
struct cpu_info *dmi_cpu_info;

struct mem_dev *dmi_memory_devices[MAX_DMI_MEM_DEVICES];
int dmi_num_memory_devices = 0;

static char *get_tstruct_string(struct tstruct_header *header, uint16_t maxlen, int n)
{
    if (n < 1)
        return NULL;
    char *a = (char *) header + header->length;
    n--;
    do {
        if (! *a)
            n--;
        if (!n && *a)
            return a;
        a++;
    } while (a < ((char *) header + maxlen) && !( *a == 0 && *(a - 1) == 0));
    return NULL;
}

#if (ARCH_BITS == 64)
static uintptr_t find_in_efi64_system_table(efi64_system_table_t *system_table, const efi_guid_t *guid)
{
    efi64_config_table_t *config_tables = (efi64_config_table_t *) map_region(system_table->config_tables, system_table->num_config_tables * sizeof(efi64_config_table_t), true);
    if (config_tables == NULL) return 0;

    uintptr_t table_addr = 0;
    for (uint32_t i = 0; i < system_table->num_config_tables; i++) {
        if (memcmp( & config_tables[i].guid, guid, sizeof(efi_guid_t)) == 0) {
            table_addr = config_tables[i].table;
        }
    }
    return table_addr;
}
#endif

static uintptr_t find_in_efi32_system_table(efi32_system_table_t *system_table, const efi_guid_t *guid)
{
    efi32_config_table_t *config_tables = (efi32_config_table_t *) map_region(system_table->config_tables, system_table->num_config_tables * sizeof(efi32_config_table_t), true);
    if (config_tables == NULL) return 0;

    uintptr_t table_addr = 0;
    for (uint32_t i = 0; i < system_table->num_config_tables; i++) {
        if (memcmp( & config_tables[i].guid, guid, sizeof(efi_guid_t)) == 0) {
            table_addr = config_tables[i].table;
        }
    }
    return table_addr;
}

static uintptr_t find_efi_config_table(const efi_guid_t *guid)
{
    const boot_params_t *boot_params = (boot_params_t *) boot_params_addr;
    const efi_info_t *efi_info = & boot_params->efi_info;

    if (efi_info->loader_signature == EFI32_LOADER_SIGNATURE) {
        uintptr_t system_table_addr = map_region(efi_info->sys_tab, sizeof(efi32_system_table_t), true);
        if (system_table_addr != 0) {
            return find_in_efi32_system_table((efi32_system_table_t *) system_table_addr, guid);
        }
    }
#if (ARCH_BITS == 64)
    if (efi_info->loader_signature == EFI64_LOADER_SIGNATURE) {
        uintptr_t system_table_addr = (uintptr_t) efi_info->sys_tab_hi << 32 | (uintptr_t) efi_info->sys_tab;
        system_table_addr = map_region(system_table_addr, sizeof(efi64_system_table_t), true);
        if (system_table_addr != 0) {
            return find_in_efi64_system_table((efi64_system_table_t *) system_table_addr, guid);
        }
    }
#endif
    return 0;
}

static uintptr_t find_smbiosv2_adr(void)
{
    uintptr_t rp = find_efi_config_table(&SMBIOS2_GUID);

#if defined(__i386__) || defined(__x86_64__)
    if (rp == 0) {
        // BIOS. Only x86 has a legacy BIOS area; on other architectures
        // this region may not even be mapped.
        uint8_t *dmi, *dmi_search_start;
        dmi_search_start = (uint8_t *) 0x000F0000;

        for (dmi = dmi_search_start; dmi < dmi_search_start + 0xffff0; dmi += 16) {
            if ( *dmi == '_' && *(dmi + 1) == 'S' && *(dmi + 2) == 'M' && *(dmi + 3) == '_')
                return (uintptr_t) dmi;
        }
    }
#endif

    return rp;
}

static int parse_dmi(uint16_t numstructs)
{
    const uint8_t *dmi = table_start;
    int tstruct_count = 0;

    // Struct type 1 is one of the mandatory types, so we're dealing with invalid data
    // if its size is lower than that of a minimal type 1 struct (plus a couple bytes).
    if (table_length < sizeof(struct system_info)) {
        return -1;
    }

    // Parse structs
    while (dmi < table_start + table_length - 2) { // -2 for header type and length.
        const struct tstruct_header *header = (struct tstruct_header *) dmi;

        // Type 127 - End-of-Table. Mandatory with the v3 entry point, whose
        // table length is only an upper bound.
        if (header->type == 127) {
            break;
        }

        // Type 1 - System Information
        if (header->type == 1 && header->length > offsetof(struct system_info, wut)) {
            // Multiple type 1 structs are not allowed by the standard. Still, effectively pick up the last one.
            dmi_system_info = (struct system_info *) dmi;
        }
        // Type 2 - Baseboard Information
        else if (header->type == 2 && header->length > offsetof(struct baseboard_info, serialnumber)) {
            // Multiple type 2 structs are allowed by the standard. Effectively pick up the last one.
            dmi_baseboard_info = (struct baseboard_info *) dmi;
        }
        // Type 4 - Processor Information
        else if (header->type == 4 && header->length > offsetof(struct cpu_info, version)) {
            // One struct per socket; keep the first populated one.
            if (dmi_cpu_info == NULL) {
                dmi_cpu_info = (struct cpu_info *) dmi;
            }
        }
        // Type 17 - Memory Device
        else if (header->type == 17 && header->length > offsetof(struct mem_dev, partnum)) {
            struct mem_dev *mdev = (struct mem_dev *) dmi;
            // Multiple type 17 structs are allowed, with unpopulated slots sometimes
            // reported as type 2 (unknown). If type is 0 (uninitialized) or 1/2 (previously
            // initialized with unknown value) => set or overwrite the struct
            if (dmi_memory_device->type <= 2) {
                dmi_memory_device = mdev;
            }
            // Collect every populated device (size 0 means empty socket,
            // 0xFFFF means populated with unknown size, so keep the latter).
            if (mdev->size != 0 && dmi_num_memory_devices < MAX_DMI_MEM_DEVICES) {
                dmi_memory_devices[dmi_num_memory_devices++] = mdev;
            }
        }

        dmi += header->length;

        if (dmi >= table_start + table_length) {
            dmi_system_info = NULL;
            dmi_baseboard_info = NULL;
            dmi_cpu_info = NULL;
            dmi_num_memory_devices = 0;
            return -1;
        }

        while ((dmi < table_start + table_length - 1) && !(*dmi == 0 && *(dmi + 1) == 0)) {
            dmi++;
        }

        dmi += 2;

        if ((dmi > table_start + table_length) || (++tstruct_count > numstructs)) {
            dmi_system_info = NULL;
            dmi_baseboard_info = NULL;
            dmi_cpu_info = NULL;
            dmi_num_memory_devices = 0;
            return -1;
        }
    }
    return 0;
}

#if defined(__aarch64__)
// There is no architectural way to get the CPU marketing name on ARM, so
// the MIDR-derived name only identifies the microarchitecture (e.g.
// "Qualcomm Oryon"). Prefer the SMBIOS processor version string (e.g.
// "Snapdragon(R) X Elite - X1E80100") when the firmware provides one.

#define CPU_VERSION_MAX_LEN 50  // display width available for the CPU model

static char cpu_version_str[CPU_VERSION_MAX_LEN + 1];

static void override_cpu_model(void)
{
    if (dmi_cpu_info == NULL) {
        return;
    }

    uint16_t struct_length = table_length - ((uint8_t *)&dmi_cpu_info->header - (uint8_t *)table_start);

    const char *version = get_tstruct_string(&dmi_cpu_info->header, struct_length, dmi_cpu_info->version);
    if (version == NULL) {
        return;
    }

    size_t len = 0;
    int has_content = 0;
    while (version[len] != '\0' && len < CPU_VERSION_MAX_LEN) {
        if (version[len] != ' ') {
            has_content = 1;
        }
        cpu_version_str[len] = version[len];
        len++;
    }
    if (version[len] != '\0') {
        // Truncated: back up to the last word boundary.
        while (len > 0 && cpu_version_str[len - 1] != ' ') {
            len--;
        }
    }
    // Trim trailing spaces and dangling separators.
    while (len > 0 && (cpu_version_str[len - 1] == ' ' || cpu_version_str[len - 1] == '-')) {
        len--;
    }
    cpu_version_str[len] = '\0';

    if (has_content && len > 0) {
        cpu_model = cpu_version_str;
    }
}
#endif

static int8_t table_checksum(const uint8_t *start, uint8_t length)
{
    int8_t checksum = 0;

    for (const uint8_t *p = start; p < (start + length); p++) {
        checksum += *p;
    }
    return checksum;
}

int smbios_init(void)
{
    uintptr_t smb_adr;

    uint64_t dmi_table_addr = 0;
    uint32_t dmi_table_length = 0;
    uint16_t numstructs = 0;

    // Prefer the SMBIOS v2 (32-bit) entry point, which gives an exact struct
    // count, but fall back to the v3 (64-bit) one: some firmware (e.g. QEMU
    // virt, ARM laptops) only publishes the latter.
    smb_adr = find_smbiosv2_adr();
    if (smb_adr != 0) {
        // The entry point lives in firmware-reserved memory, which may not
        // be mapped yet.
        smb_adr = map_region(smb_adr, sizeof(smbiosv2_t), true);
        if (smb_adr == 0) {
            return -1;
        }
        const smbiosv2_t *eps = (const smbiosv2_t *) smb_adr;

        if (table_checksum((const uint8_t *) smb_adr, eps->length) != 0) {
            return -1;
        }

        // SMBIOS 2.3 required
        if (eps->majorversion < 2 && eps->minorversion < 3) {
            return -1;
        }

        dmi_table_addr = eps->tableaddress;
        dmi_table_length = eps->tablelength;
        numstructs = eps->numstructs;
    } else {
        smb_adr = find_efi_config_table(&SMBIOS3_GUID);
        if (smb_adr == 0) {
            return -1;
        }
        smb_adr = map_region(smb_adr, sizeof(smbiosv3_t), true);
        if (smb_adr == 0) {
            return -1;
        }
        const smbiosv3_t *eps = (const smbiosv3_t *) smb_adr;

        if (table_checksum((const uint8_t *) smb_adr, eps->length) != 0) {
            return -1;
        }

        dmi_table_addr = eps->tableaddress;
        dmi_table_length = eps->maxsize;
        // The v3 entry point has no struct count; parsing stops at the
        // mandatory end-of-table struct.
        numstructs = UINT16_MAX;
    }

#if (ARCH_BITS == 32)
    if (dmi_table_addr > UINT32_MAX) {
        return -1;
    }
#endif

    // The DMI structure table also lives in firmware-reserved memory.
    uintptr_t table_addr = map_region((uintptr_t)dmi_table_addr, dmi_table_length, true);
    if (table_addr == 0) {
        return -1;
    }
    table_start = (const uint8_t *)table_addr;
    table_length = dmi_table_length;

    int result = parse_dmi(numstructs);

#if defined(__aarch64__)
    if (result == 0) {
        override_cpu_model();
    }
#endif

    return result;
}

void print_smbios_startup_info(void)
{
    // Use baseboard info (struct type 2) as primary source of information,
    // and fall back to system info (struct type 1). Indeed, while the later
    // may contain less useful information than the former, its presence is
    // mandated by the successive revisions of the SMBIOS standard.
    // NOTE: we can get away with this ugly cast because the offsets of
    // .manufacturer and .productname are the same in system_info and baseboard_info.

    struct system_info *ptr = dmi_baseboard_info != NULL ?
                              (struct system_info *)dmi_baseboard_info : dmi_system_info;

    if (ptr != NULL) {
        char *sys_man, *sys_sku;

        int sl1, sl2, dmicol;

        uint16_t struct_length = table_length - ((uint8_t *)&ptr->header - (uint8_t *)table_start);

        sys_man = get_tstruct_string(&ptr->header, struct_length, ptr->manufacturer);
        if (sys_man != NULL) {
            sl1 = strlen(sys_man);

            sys_sku = get_tstruct_string(&ptr->header, struct_length, ptr->productname);
            if (sys_sku != NULL) {
                sl2 = strlen(sys_sku);

                if (sl1 && sl2) {
                    dmicol = 40 - ((sl1 + sl2) / 2);
                    dmicol = prints(LINE_DMI, dmicol, sys_man);
                    prints(LINE_DMI, dmicol + 1, sys_sku);
                }
            }
        }
    }
}

// ---------------------------------------------------
// DMI Type 17 fallback display (used when SPD fails)
// ---------------------------------------------------

#define DMI_MANUF_LEN   20
#define DMI_PART_LEN    26

// Type 17 structs are variable-length: fields beyond SMBIOS 2.3 are only
// present when the struct is long enough to contain them.
#define MEMDEV_HAS_FIELD(md, field) \
    ((md)->header.length >= offsetof(struct mem_dev, field) + sizeof((md)->field))

typedef struct {
    uint64_t    size_kb;                        // 0 = unknown
    uint32_t    speed;                          // MT/s, 0 = unknown
    uint8_t     type;                           // raw DMI memory type byte
    const char *manuf;                          // JEP-106 name, manuf_str or NULL
    char        manuf_str[DMI_MANUF_LEN + 1];
    char        part_num[DMI_PART_LEN + 1];
    int         count;
} dmi_mem_group_t;

static uint64_t memdev_size_kb(const struct mem_dev *md)
{
    if (md->size == 0 || md->size == 0xFFFF) {
        return 0;                               // empty socket / unknown size
    }
    if (md->size == 0x7FFF) {
        // Real size is in ext_size (SMBIOS 2.7+), in MB, bits 30:0.
        if (!MEMDEV_HAS_FIELD(md, ext_size)) {
            return 0;
        }
        return (uint64_t)(md->ext_size & 0x7FFFFFFF) * 1024;
    }
    if (md->size & 0x8000) {
        return md->size & 0x7FFF;               // value is in KB
    }
    return (uint64_t)md->size * 1024;           // value is in MB
}

static uint32_t memdev_speed_mts(const struct mem_dev *md)
{
    // Prefer the configured (actual) speed (SMBIOS 2.7+).
    if (MEMDEV_HAS_FIELD(md, conf_ram_speed)) {
        uint16_t conf = md->conf_ram_speed;
        if (conf == 0xFFFF) {
            if (MEMDEV_HAS_FIELD(md, extended_conf_speed) && md->extended_conf_speed != 0) {
                return md->extended_conf_speed;
            }
        } else if (conf != 0) {
            return conf;
        }
    }
    // Fall back to the maximum rated speed.
    if (md->speed == 0xFFFF) {
        if (MEMDEV_HAS_FIELD(md, extended_speed)) {
            return md->extended_speed;
        }
        return 0;
    }
    return md->speed;
}

static const char *memdev_type_str(uint8_t type)
{
    switch (type) {
      case DMI_SDR:
        return "SDRAM";
      case DMI_RDRAM:
        return "RDRAM";
      case DMI_DDR:
        return "DDR";
      case DMI_DDR2:
      case DMI_DDR2_FBDIMM:
        return "DDR2";
      case DMI_DDR3:
        return "DDR3";
      case DMI_DDR4:
        return "DDR4";
      case DMI_LPDDR:
        return "LPDDR";
      case DMI_LPDDR2:
        return "LPDDR2";
      case DMI_LPDDR3:
        return "LPDDR3";
      case DMI_LPDDR4:
        return "LPDDR4";
      case DMI_DDR5:
        return "DDR5";
      case DMI_LPDDR5:
        return "LPDDR5";
      default:
        return NULL;                            // "Unknown", "Other", "RAM", ...
    }
}

static const char *memdev_jep106_name(const struct mem_dev *md)
{
    if (!MEMDEV_HAS_FIELD(md, module_manufacturer_id)) {
        return NULL;
    }
    // SMBIOS 3.2+: the two SPD manufacturer bytes, LSB first. The low byte
    // of the (little-endian) word is the continuation-code count, the high
    // byte the manufacturer code, each with an odd-parity bit 7.
    uint16_t id = md->module_manufacturer_id;
    if (id == 0 || id == 0xFFFF) {
        return NULL;
    }
    uint8_t cont = id & 0x7F;
    uint8_t code = (id >> 8) & 0x7F;
    if (code == 0 || code == 0x7F) {
        return NULL;
    }
    return get_jep106_name(((uint16_t)(cont & 0x1F) << 8) | code);
}

static bool dmi_string_is_junk(const char *s, size_t len)
{
    static const char *const junk[] = {
        "Unknown", "Not Specified", "Not Available", "To Be Filled By O.E.M.",
        "NO DIMM", "No Module Installed", "None", "N/A", "Undefined",
        "Default string", "Part Num", "PartNum", "Manufacturer", "Module Manufacturer"
    };

    if (len == 0) {
        return true;
    }
    for (size_t i = 0; i < sizeof(junk) / sizeof(junk[0]); i++) {
        const char *b = junk[i];
        size_t k = 0;
        while (k < len && b[k] != '\0'
               && toupper((unsigned char)s[k]) == toupper((unsigned char)b[k])) {
            k++;
        }
        if (k == len && b[k] == '\0') {
            return true;
        }
    }
    return false;
}

static bool copy_dmi_string(char *dst, size_t dst_size, struct mem_dev *md, uint8_t string_idx)
{
    dst[0] = '\0';

    uint32_t remaining = table_length - (uint32_t)((const uint8_t *)md - table_start);
    uint16_t maxlen = remaining > UINT16_MAX ? UINT16_MAX : remaining;

    const char *src = get_tstruct_string(&md->header, maxlen, string_idx);
    if (src == NULL) {
        return false;
    }
    while (*src == ' ') {                       // skip leading spaces
        src++;
    }
    size_t src_len = strlen(src);
    while (src_len > 0 && src[src_len - 1] == ' ') {  // ignore trailing spaces
        src_len--;
    }

    // Filter placeholders on the full string, before any truncation.
    if (dmi_string_is_junk(src, src_len)) {
        return false;
    }

    size_t len = src_len < dst_size - 1 ? src_len : dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
    return true;
}

static bool dmi_manuf_match(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }
    return strncmp(a, b, DMI_MANUF_LEN + 1) == 0;
}

static void print_dmi_mem_group(const dmi_mem_group_t *group, int row)
{
    int col = prints(row, 0, " -");

    if (group->count > 1) {
        col = printf(row, col + 1, "%ix", group->count);
    }
    if (group->size_kb != 0) {
        col = printf(row, col + 1, "%kB", (uintptr_t)group->size_kb);
    }

    const char *tstr = memdev_type_str(group->type);
    if (tstr != NULL && group->speed != 0) {
        col = printf(row, col + 1, "%s-%i", tstr, group->speed);
    } else if (tstr != NULL) {
        col = prints(row, col + 1, tstr);
    } else if (group->speed != 0) {
        col = printf(row, col + 1, "%i MT/s", group->speed);
    }

    if (group->manuf != NULL) {
        col = printf(row, col + 1, "- %s", group->manuf);
    }
    if (group->part_num[0] != '\0' && col + 1 + (int)strlen(group->part_num) < SCREEN_WIDTH) {
        prints(row, col + 1, group->part_num);
    }
}

void print_dmi_memory_info(void)
{
    // Static: called once at startup, single-threaded; keeps ~2 kB off the stack.
    static dmi_mem_group_t groups[MAX_DMI_MEM_DEVICES];
    int num_groups = 0;

    for (int i = 0; i < dmi_num_memory_devices; i++) {
        struct mem_dev *md = dmi_memory_devices[i];
        dmi_mem_group_t g;

        memset(&g, 0, sizeof(g));
        g.size_kb = memdev_size_kb(md);
        g.speed   = memdev_speed_mts(md);
        g.type    = md->type;
        g.manuf   = memdev_jep106_name(md);
        if (g.manuf == NULL && copy_dmi_string(g.manuf_str, sizeof(g.manuf_str), md, md->manufacturer)) {
            g.manuf = g.manuf_str;
        }
        if (!copy_dmi_string(g.part_num, sizeof(g.part_num), md, md->partnum)) {
            g.part_num[0] = '\0';
        }

        // Skip devices carrying no usable info at all.
        if (g.size_kb == 0 && g.speed == 0 && memdev_type_str(g.type) == NULL
            && g.manuf == NULL && g.part_num[0] == '\0') {
            continue;
        }

        // Collapse identical modules into a single group.
        int j;
        for (j = 0; j < num_groups; j++) {
            if (groups[j].size_kb == g.size_kb && groups[j].speed == g.speed
                && groups[j].type == g.type && dmi_manuf_match(groups[j].manuf, g.manuf)
                && strncmp(groups[j].part_num, g.part_num, DMI_PART_LEN + 1) == 0) {
                groups[j].count++;
                break;
            }
        }
        if (j == num_groups) {
            g.count = 1;
            groups[num_groups] = g;
            if (g.manuf == g.manuf_str) {
                groups[num_groups].manuf = groups[num_groups].manuf_str;
            }
            num_groups++;
        }
    }

    if (num_groups == 0) {
        return;
    }

    // Header, with the common type and max speed when all groups agree.
    const char *tstr = memdev_type_str(groups[0].type);
    uint32_t max_speed = 0;
    bool same_type = true;
    for (int i = 0; i < num_groups; i++) {
        if (groups[i].type != groups[0].type) {
            same_type = false;
        }
        if (groups[i].speed > max_speed) {
            max_speed = groups[i].speed;
        }
    }

    int hdr_len;
    if (same_type && tstr != NULL && max_speed != 0) {
        hdr_len = printf(ROW_SPD - 2, 0, "Memory DMI Information (%s-%i)", tstr, max_speed);
    } else {
        hdr_len = prints(ROW_SPD - 2, 0, "Memory DMI Information");
    }

    char dashes[SCREEN_WIDTH];
    if (hdr_len >= SCREEN_WIDTH) {
        hdr_len = SCREEN_WIDTH - 1;
    }
    memset(dashes, '-', hdr_len);
    dashes[hdr_len] = '\0';
    prints(ROW_SPD - 1, 0, dashes);

    // Same row budget as the SPD display.
    for (int i = 0; i < num_groups && i < MAX_SPD_SLOT; i++) {
        print_dmi_mem_group(&groups[i], ROW_SPD + i);
    }
}
