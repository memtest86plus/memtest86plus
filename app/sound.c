// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 Anton Ivanov (aka demiurg_spb+rigler).
// Copyright (C) 2024 Lionel Debroux.
#include <stdbool.h>
#include "pit.h"
#include "sound.h"  // self to check prototypes

static bool enabled;
static unsigned int duration;

#define DURATION (7)

static void beep_off()
{
    if (enabled) {
        enabled = 0;
        duration = 0;
        pit_off();
    }
}

static void beep_on(unsigned f_hz)
{
    if (!enabled) {
        enabled = 1;
        duration = DURATION;
        pit_init_square_wave_generator(2, f_hz);  // pit-ch2 is connected to buzzer
    }
}

void sound_beep(bool ok)
{
    #define beep_freq_ok (1100)
    #define beep_freq_er ( 380)

    beep_on(ok ? beep_freq_ok : beep_freq_er);
}

void sound_tick_task(void)
{
    if (duration) {
        duration--;
    }
    else {
        beep_off();
    }
}
