// SPDX-License-Identifier: GPL-2.0
//
// SPD files can be also pened with:
//   decode-dimms -x <(xxd data/<filename.spd>)

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "smbus.h"
#include "selftests.h"

// Make the linker happy.
struct mem_dev *dmi_memory_device;
void *pci_config_read16;
void *pci_config_read8;
void *pci_config_write8;
void *imc_type;
void *quirk;

#define MAX_SPD_SIZE 1024

#define parse_spd_ddr4 parse_spd_ddr4_wrapper
void parse_spd_ddr4(spd_info *spdi, uint8_t slot_idx);

#define parse_spd_ddr3 parse_spd_ddr3_wrapper
void parse_spd_ddr3(spd_info *spdi, uint8_t slot_idx);

#define parse_spd_ddr2 parse_spd_ddr2_wrapper
void parse_spd_ddr2(spd_info *spdi, uint8_t slot_idx);

#define parse_spd_ddr parse_spd_ddr_wrapper
void parse_spd_ddr(spd_info *spdi, uint8_t slot_idx);

#define print_spdi print_spdi_wrapper
void print_spdi(spd_info spdi, uint8_t lidx);

uint8_t spd[MAX_SPD_SIZE];
uint16_t spd_size;
uint8_t spd_slot;

void load_spd(uint8_t slot_idx, char *filename) {
    int f = open(filename, 0);

    if (f == -1) {
        fprintf(stderr, "Cannot open %s\n", filename);
        exit(1);
    }

    spd_size = read(f, spd, MAX_SPD_SIZE);
    close(f);

    spd_slot = slot_idx;
    printf("Loaded %s, slot=%u, size=%u\n", filename, slot_idx, spd_size);
}

uint8_t mock_read_spd_byte(uint8_t slot_idx, uint16_t spd_adr) {

    if (spd_adr < spd_size && slot_idx == spd_slot)
        return spd[spd_adr];

    fprintf(stderr, "Unbound SPD read slot %u, addr %u\n", spd_slot, spd_adr);
    exit(1);
}

void test_parse_spd_ddr4() {

    spd_info spdi;
    uint8_t slot_idx = 0;

    // BUG: shuld be reported as DDR4-2133 15-15-15-36 not DDR4-2132 14-14-14-35
    // Samsung PB: DDR4-2133 (1066MHz @ CL=15, tRCD=15, tRP=15)
    load_spd(++slot_idx, "data/DDR4-M393A1G40EB1-CPB.spd");
    memset(&spdi, 0, sizeof(spdi));
    parse_spd_ddr4(&spdi, slot_idx);
    check_int(spdi.isValid, 1);
    check_str(spdi.type, "DDR4");
    check_int(spdi.module_size, 8192);
    check_str(spdi.sku, "M393A1G40EB1-CPB");
    check_int(spdi.fab_year, 17);
    check_int(spdi.fab_week, 10);
    check_int(spdi.hasECC, 1);
    check_int(spdi.XMP, 0);
    check_int(spdi.freq, 2132);
    check_int(spdi.tCL, 14);
    check_int(spdi.tRCD, 14);
    check_int(spdi.tRP, 14);
    check_int(spdi.tRAS, 35);

    // BUG: shuld be reported as DDR4-2400 17-17-17-39 not DDR4-2132 16-16-16-38
    // Samsung RC: DDR4-2400 (1200MHz @ CL=17, tRCD=17, tRP=17)
    load_spd(++slot_idx, "data/DDR4-M393A1G40EB1-CRC.spd");
    memset(&spdi, 0, sizeof(spdi));
    parse_spd_ddr4(&spdi, slot_idx);
    check_int(spdi.isValid, 1);
    check_str(spdi.type, "DDR4");
    check_int(spdi.module_size, 8192);
    check_str(spdi.sku, "M393A1G40EB1-CRC");
    check_int(spdi.fab_year, 18);
    check_int(spdi.fab_week, 5);
    check_int(spdi.hasECC, 1);
    check_int(spdi.XMP, 0);
    check_int(spdi.freq, 2400);
    check_int(spdi.tCL, 16);
    check_int(spdi.tRCD, 16);
    check_int(spdi.tRP, 16);
    check_int(spdi.tRAS, 38);
}

void test_parse_spd_ddr3() {

    spd_info spdi;
    uint8_t slot_idx = 0;

    // 9905403-440.A00LF -> Kingston HyperX KHX1600C9D3X2K2/8GX
    // https://www.kingston.com/datasheets/KHX1600C9D3X2K2_8GX.pdf
    // JEDEC: DDR3-1333 9-9-9-24@1.5V, XMP: DDR3-1600 9-9-9-27@1.65V
    load_spd(++slot_idx, "data/DDR3-9905403-440.A00LF.spd");
    memset(&spdi, 0, sizeof(spdi));
    parse_spd_ddr3(&spdi, slot_idx);
    check_int(spdi.isValid, 1);
    check_str(spdi.type, "DDR3");
    check_int(spdi.module_size, 4096);
    check_str(spdi.sku, "9905403-440.A00LF");
    check_int(spdi.fab_year, 11);
    check_int(spdi.fab_week, 39);
    check_int(spdi.hasECC, 0);
    check_int(spdi.XMP, 1);
    check_int(spdi.freq, 1600);
    check_int(spdi.tCL, 9);
    check_int(spdi.tRCD, 9);
    check_int(spdi.tRP, 9);
    check_int(spdi.tRAS, 27);

    // BUG: shuld be reported as DDR3-1333 9-9-9-24 not 8-8-8-22
    // Samsung H9: DDR3-1333 9-9-9
    load_spd(++slot_idx, "data/DDR3-M392B1G73DB0-YH9.spd");
    memset(&spdi, 0, sizeof(spdi));
    parse_spd_ddr3(&spdi, slot_idx);
    check_int(spdi.isValid, 1);
    check_str(spdi.type, "DDR3");
    check_int(spdi.module_size, 8192);
    check_str(spdi.sku, "M392B1G73DB0-YH9");
    check_int(spdi.fab_year, 14);
    check_int(spdi.fab_week, 20);
    check_int(spdi.hasECC, 1);
    check_int(spdi.XMP, 0);
    check_int(spdi.freq, 1333);
    check_int(spdi.tCL, 8);
    check_int(spdi.tRCD, 8);
    check_int(spdi.tRP, 8);
    check_int(spdi.tRAS, 22);

    // BUG: should be reported as DDR3-1600 11-11-11-28 not 10-10-10-26
    // Samsung K0: DDR3-1600 11-11-11
    load_spd(++slot_idx, "data/DDR3-M378B5173DB0-CK0.spd");
    memset(&spdi, 0, sizeof(spdi));
    parse_spd_ddr3(&spdi, slot_idx);
    check_int(spdi.isValid, 1);
    check_str(spdi.type, "DDR3");
    check_int(spdi.module_size, 4096);
    check_str(spdi.type, "DDR3");
    check_str(spdi.sku, "M378B5173DB0-CK0");
    check_int(spdi.fab_year, 14);
    check_int(spdi.fab_week, 16);
    check_int(spdi.hasECC, 0);
    check_int(spdi.XMP, 0);
    check_int(spdi.freq, 1600);
    check_int(spdi.tCL, 10);
    check_int(spdi.tRCD, 10);
    check_int(spdi.tRP, 10);
    check_int(spdi.tRAS, 26);

    // BUG: should be reported as DDR3-1866 13-13-13-32 not 10-10-10-28
    // Samsung MA: DDR3-1866 13-13-13
    load_spd(++slot_idx, "data/DDR3-M391B1G73QH0-CMA.spd");
    memset(&spdi, 0, sizeof(spdi));
    parse_spd_ddr3(&spdi, slot_idx);
    check_int(spdi.isValid, 1);
    check_str(spdi.type, "DDR3");
    check_int(spdi.module_size, 8192);
    check_str(spdi.sku, "M391B1G73QH0-CMA");
    check_int(spdi.fab_year, 15);
    check_int(spdi.fab_week, 22);
    check_int(spdi.hasECC, 1);
    check_int(spdi.XMP, 0);
    check_int(spdi.freq, 1866);
    check_int(spdi.tCL, 10);
    check_int(spdi.tRCD, 10);
    check_int(spdi.tRP, 10);
    check_int(spdi.tRAS, 28);
}

void test_parse_spd_ddr2() {

    spd_info spdi;
    uint8_t slot_idx = 0;

    // BUG: should be reported as DDR2-800 5-5-5-15 not 3-5-5-14
    load_spd(++slot_idx, "data/DDR2-GoldenEmpire-1.spd");
    memset(&spdi, 0, sizeof(spdi));
    parse_spd_ddr2(&spdi, slot_idx);
    check_int(spdi.isValid, 1);
    check_str(spdi.type, "DDR2");
    check_int(spdi.module_size, 1024);
    check_str(spdi.sku, "CL4-4-4  DDR2-800");
    check_int(spdi.fab_year, 9);
    check_int(spdi.fab_week, 44);
    check_int(spdi.hasECC, 0);
    check_int(spdi.XMP, 0);
    check_int(spdi.freq, 800);
    check_int(spdi.tCL, 3);
    check_int(spdi.tRCD, 5);
    check_int(spdi.tRP, 5);
    check_int(spdi.tRAS, 14);

    load_spd(++slot_idx, "data/DDR2-GoldenEmpire-2.spd");
    memset(&spdi, 0, sizeof(spdi));
    parse_spd_ddr2(&spdi, slot_idx);
    check_int(spdi.isValid, 1);
    check_str(spdi.type, "DDR2");
    check_int(spdi.module_size, 1024);
    check_str(spdi.sku, "CL4-4-4DDR2-800");
    check_int(spdi.fab_year, 6);
    check_int(spdi.fab_week, 27);
    check_int(spdi.XMP, 0);

    // BUG: should be reported as DDR2-800 5-5-5-15 not 3-5-5-18
    load_spd(++slot_idx, "data/DDR2-2G-UDIMM.spd");
    memset(&spdi, 0, sizeof(spdi));
    parse_spd_ddr2(&spdi, slot_idx);
    check_int(spdi.isValid, 1);
    check_str(spdi.type, "DDR2");
    check_int(spdi.module_size, 2048);
    check_str(spdi.sku, "2G-UDIMM");
    check_int(spdi.fab_year, 9);
    check_int(spdi.fab_week, 9);
    check_int(spdi.hasECC, 0);
    check_int(spdi.XMP, 0);
    check_int(spdi.freq, 800);
    check_int(spdi.tCL, 3);
    check_int(spdi.tRCD, 5);
    check_int(spdi.tRP, 5);
    check_int(spdi.tRAS, 18);
}

void test_parse_spd_ddr() {

    spd_info spdi;
    uint8_t slot_idx = 0;

    load_spd(++slot_idx, "data/DDR-OCZ4001024ELPE.spd");
    memset(&spdi, 0, sizeof(spdi));
    parse_spd_ddr(&spdi, slot_idx);

    check_int(spdi.isValid, 1);
    check_str(spdi.type, "DDR");
    check_int(spdi.module_size, 1024);
    check_str(spdi.sku, "OCZ4001024ELPE");
    check_int(spdi.fab_year, 0);
    check_int(spdi.fab_week, 0);
    check_int(spdi.hasECC, 0);
    check_int(spdi.XMP, 0);
    check_int(spdi.freq, 400);
    check_int(spdi.tCL, 2);
    check_int(spdi.tRCD, 3);
    check_int(spdi.tRP, 2);
    check_int(spdi.tRAS, 5);
}

void test_print_spdi() {
    spd_info spdi;

    memset(&spdi, 0, sizeof(spdi));

    spdi.type = "DDR5";
    spdi.module_size = 8192;
    spdi.hasECC = true;
    spdi.freq = 7200;
    spdi.jedec_code = 1;                // AMD
    strcpy(spdi.sku, "123 abc");
    spdi.fab_year = 22;                 // 2022
    spdi.fab_week = 3;                  // W03

    memtest_print_add_expectation(14, 43, "(2022-W03)");
    memtest_print_add_expectation(14, 35, "123 abc");
    memtest_print_add_expectation(14, 29, "- AMD");
    memtest_print_add_expectation(14, 25, "ECC");
    memtest_print_add_expectation(14, 0, " - Slot 0: 8GB DDR5-7200");

    print_spdi(spdi, 1);

    memtest_print_done();
}

int main() {

    test_parse_spd_ddr4();
    test_parse_spd_ddr3();
    test_parse_spd_ddr2();
    test_parse_spd_ddr();

    test_print_spdi();

    printf("All tests for %s passed.\n", __FILE__);

    return 0;
}
