// SPDX-License-Identifier: GPL-2.0
#ifndef RTC_H
#define RTC_H
/**
 * \file
 *
 * Provides the current date & time, from the CMOS RTC where available,
 * falling back to the build date & time of the binary.
 *
 *//*
 * Copyright (C) 2026 Sam Demeulemeester.
 */

#include <stdbool.h>

/**
 * A calendar date & time.
 */
typedef struct {
    int         year;               // 4-digit year
    int         month;              // 1 - 12
    int         day;                // 1 - 31
    int         hour;               // 0 - 23
    int         min;                // 0 - 59
    int         sec;                // 0 - 59
} rtc_time_t;

/**
 * Fills dt with the current RTC date & time on x86, or with the binary's
 * build date & time elsewhere (and when the RTC returns out-of-range
 * values, e.g. dead CMOS battery). Returns true only for live RTC data.
 */
bool rtc_get_time(rtc_time_t *dt);

#endif // RTC_H
