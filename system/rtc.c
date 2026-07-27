// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Sam Demeulemeester.

#include <stdbool.h>
#include <stdint.h>

#include "io.h"

#include "rtc.h"

#include "build_version.h"

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

// RTC (CMOS) register reading. Ports 0x70/0x71 BCD format.
// ISA-only; LoongArch has no equivalent fixed-port RTC.

#if defined(__i386__) || defined(__x86_64__)
static uint8_t rtc_read(uint8_t reg)
{
    outb(reg, 0x70);
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}
#endif

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

bool rtc_get_time(rtc_time_t *dt)
{
#if defined(__i386__) || defined(__x86_64__)
    // Wait for any update in progress to complete (UIP bit, status register A).
    for (int i = 0; i < 20000 && (rtc_read(0x0A) & 0x80); i++) {}

    uint8_t rtc_stb  = rtc_read(0x0B);
    uint8_t rtc_sec  = rtc_read(0x00);
    uint8_t rtc_min  = rtc_read(0x02);
    uint8_t rtc_hour = rtc_read(0x04);
    uint8_t rtc_day  = rtc_read(0x07);
    uint8_t rtc_mon  = rtc_read(0x08);
    uint8_t rtc_year = rtc_read(0x09);
    uint8_t rtc_cent = rtc_read(0x32);

    bool rtc_pm = rtc_hour & 0x80;
    rtc_hour &= 0x7F;

    // Registers are BCD unless the RTC is in binary mode (status register B, DM bit).
    if (!(rtc_stb & 0x04)) {
        rtc_sec  = bcd_to_bin(rtc_sec);
        rtc_min  = bcd_to_bin(rtc_min);
        rtc_hour = bcd_to_bin(rtc_hour);
        rtc_day  = bcd_to_bin(rtc_day);
        rtc_mon  = bcd_to_bin(rtc_mon);
        rtc_year = bcd_to_bin(rtc_year);
        rtc_cent = bcd_to_bin(rtc_cent);
    }

    // Convert 12-hour mode (hour bit 7 = PM) to 24-hour.
    if (!(rtc_stb & 0x02)) {
        rtc_hour = rtc_hour % 12 + (rtc_pm ? 12 : 0);
    }

    dt->year  = (rtc_cent ? rtc_cent * 100 : 2000) + rtc_year;
    dt->month = rtc_mon;
    dt->day   = rtc_day;
    dt->hour  = rtc_hour;
    dt->min   = rtc_min;
    dt->sec   = rtc_sec;

    // Reject out-of-range values (ie: dead CMOS battery) and use the build date instead.
    if (dt->month >= 1 && dt->month <= 12 && dt->day >= 1 && dt->day <= 31
    &&  dt->year >= 1980 && dt->year <= 2107
    &&  dt->hour <= 23 && dt->min <= 59 && dt->sec <= 59) {
        return true;
    }
#endif

    // BUILD_DATETIME ("YYYY-MM-DD hh:mm:ss") is the git commit date, not __DATE__,
    // so builds stay reproducible as required for shim-review.
    static const char bd[] = BUILD_DATETIME;

    dt->year  = (bd[0] - '0') * 1000 + (bd[1] - '0') * 100 + (bd[2] - '0') * 10 + bd[3] - '0';
    dt->month = (bd[5] - '0') * 10 + bd[6] - '0';
    dt->day   = (bd[8] - '0') * 10 + bd[9] - '0';
    dt->hour  = (bd[11] - '0') * 10 + bd[12] - '0';
    dt->min   = (bd[14] - '0') * 10 + bd[15] - '0';
    dt->sec   = (bd[17] - '0') * 10 + bd[18] - '0';

    return false;
}
