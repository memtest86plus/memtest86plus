// SPDX-License-Identifier: GPL-2.0
#ifndef CONFIG_H
#define CONFIG_H
/**
 * \file
 *
 * Provides the configuration settings and pop-up menu.
 *
 *//*
 * Copyright (C) 2020-2022 Martin Whitaker.
 */

#include <stdbool.h>
#include <stdint.h>

#include "smp.h"

// Origin and size of the pop-up window.

#define POP_R       3
#define POP_C       21

#define POP_W       38
#define POP_H       18

#define POP_LAST_R  (POP_R + POP_H - 1)
#define POP_LAST_C  (POP_C + POP_W - 1)

#define POP_REGION  POP_R, POP_C, POP_LAST_R, POP_LAST_C

#define POP_LM      (POP_C + 3)     // Left margin
#define POP_LI      (POP_C + 5)     // List indent

#define SEL_W       32
#define SEL_H       2

#define SEL_AREA    (SEL_W * SEL_H)

// -------------

typedef enum {
    PAR,
    SEQ,
    ONE
} cpu_mode_t;

typedef enum {
    ERROR_MODE_NONE,
    ERROR_MODE_SUMMARY,
    ERROR_MODE_ADDRESS,
    ERROR_MODE_BADRAM
} error_mode_t;

typedef enum {
    POWER_SAVE_OFF,
    POWER_SAVE_LOW,
    POWER_SAVE_HIGH
} power_save_t;

extern uintptr_t    pm_limit_lower;
extern uintptr_t    pm_limit_upper;

extern uintptr_t    num_pages_to_test;

extern cpu_mode_t   cpu_mode;

extern error_mode_t error_mode;

extern cpu_state_t  cpu_state[MAX_CPUS];

extern bool         enable_temperature;
extern bool         enable_trace;

extern bool         enable_sm;
extern bool         enable_tty;

extern bool         pause_at_start;

extern power_save_t power_save;

extern int tty_params_port;
extern int tty_params_baud;
extern int tty_update_period;

void config_init(void);

void config_menu(bool initial);

void initial_config(void);

#endif // CONFIG_H
