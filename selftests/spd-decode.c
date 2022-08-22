// SPDX-License-Identifier: GPL-2.0

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "smbus.h"
#include "selftests.h"
#include "jedec_id.h"

// Make the linker happy.
struct mem_dev *dmi_memory_device;
void *pci_config_read16;
void *pci_config_read8;
void *pci_config_write8;
void *imc_type;
void *quirk;
void *memtest_printf_mock;
void *memtest_prints_mock;

#define MAX_SPD_SIZE 1024
uint8_t spd[MAX_SPD_SIZE];
uint16_t spd_size;

#define parse_spd parse_spd_wrapper
void parse_spd(spd_info *spdi, uint8_t slot_idx);

int memtest_printf(int row, int col, const char *fmt, ...);
#define mprintf(...) memtest_printf(-1, -1, __VA_ARGS__)

int memtest_prints(int row, int col, const char *str);
#define mprints(str) memtest_printf(-1, -1, str)

void print_char_mock(int row, int col, char ch) {

    (void)row;      // Silent "unused parameter" warning
    (void)col;      // Silent "unused parameter" warning

    putchar(ch);
}

void load_spd(char *filename) {
    int f = open(filename, O_RDONLY);

    if (f == -1) {
        fprintf(stderr, "Cannot open %s\n", filename);
        exit(1);
    }

    spd_size = read(f, spd, MAX_SPD_SIZE);
    close(f);

    printf("Loaded %s, size=%u\n", filename, spd_size);
}

uint8_t mock_read_spd_byte(uint8_t slot_idx, uint16_t spd_adr) {

    (void)slot_idx;   // Silent "unused parameter" warning

    if (spd_adr < spd_size)
        return spd[spd_adr];

    fprintf(stderr, "Unbound SPD read - addr %u\n", spd_adr);
    exit(1);
}

static char *no_yes[] = {"No", "Yes"};

int main(int argc, char *argv[]) {

    spd_info spdi;
    uint16_t i;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.spd>\n", argv[0]);
        return 1;
    }

    load_spd(argv[1]);
    parse_spd(&spdi, 0);

    if (!spdi.isValid) {
        printf("Invalid SPD data.\n");
        return 1;
    }

    mprintf("Type: %s-%u %u-%u-%u-%u-%u\n",
                spdi.type,
                spdi.freq, spdi.tCL, spdi.tRCD, spdi.tRP, spdi.tRAS);

    mprintf("Size: %kiB\n", spdi.module_size * 1024);


    for (i = 0; i < JEP106_CNT; i++) {
        if (spdi.jedec_code == jep106[i].jedec_code)
            break;
    }

    mprints("Vendor: "); 
    if (spdi.jedec_code == 0) {
        mprints("Noname");
    } else if (i == JEP106_CNT) {
        mprintf("Unknown (0x%04x)", spdi.jedec_code);
    } else {
        mprintf("%s (0x%04x)", jep106[i].name, spdi.jedec_code);
    }
    mprints("\n");

    if (*spdi.sku)
        mprintf("Part Number: \"%s\"\n", spdi.sku);
    mprintf("Capabilities:\n");
    mprintf(" - ECC: %s\n", no_yes[!!spdi.hasECC]);
    mprintf(" - XMP: %u\n", spdi.XMP);

    return 0;
}
