// SPDX-License-Identifier: GPL-2.0

#include <stdint.h>
#include <stdbool.h>

extern uint8_t mock_read_spd_byte(uint8_t slot_idx, uint16_t spd_adr);

static uint8_t get_spd(uint8_t slot_idx, uint16_t spd_adr);

static uint8_t get_spd_mock(uint8_t slot_idx, uint16_t spd_adr) {
    return mock_read_spd_byte(slot_idx, spd_adr);

    get_spd(slot_idx, spd_adr);         // Silence "defined but not used"
}

#define get_spd(slot_idx, spd_adr) get_spd_mock(slot_idx, spd_adr)
#include "smbus.c"

// Wrappers allowing to export and then call static functions.
void parse_spd_ddr4_wrapper(spd_info *spdi, uint8_t slot_idx) {
    parse_spd_ddr4(spdi, slot_idx);
}

void parse_spd_ddr3_wrapper(spd_info *spdi, uint8_t slot_idx) {
    parse_spd_ddr3(spdi, slot_idx);
}

void parse_spd_ddr2_wrapper(spd_info *spdi, uint8_t slot_idx) {
    parse_spd_ddr2(spdi, slot_idx);
}

void parse_spd_ddr_wrapper(spd_info *spdi, uint8_t slot_idx) {
    parse_spd_ddr(spdi, slot_idx);
}

void print_spdi_wrapper(spd_info spdi, uint8_t lidx) {
    print_spdi(spdi, lidx);
}
