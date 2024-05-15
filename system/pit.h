// SPDX-License-Identifier: GPL-2.0
#ifndef PIT_H
#define PIT_H
/**
 * \file
 *
 * Provides types and variables used to work with PIT
 *
 * This will work on real hardware and in QEMU if it started with -audiodev pa,id=speaker -machine pcspk-audiodev=speaker.
 * The code also changes the PIT timer 2 frequency, so you will have to reset that when you're done "beep"ing :)
 *
 * hints:
 *  https://wiki.osdev.org/PC_Speaker
 *  https://wiki.osdev.org/Programmable_Interval_Timer
 *  https://stackoverflow.com/questions/8960620/low-level-i-o-access-using-outb-and-inb
 *  https://stackoverflow.com/questions/14194798/is-there-a-specification-of-x86-i-o-port-assignment
 *  https://stackoverflow.com/questions/22355436/how-to-compile-32-bit-apps-on-64-bit-ubuntu
 *
 * outb() and friends are hardware-specific.  The value argument is
 * passed first and the port argument is passed second, which is the
 * opposite order from most DOS implementations.
 *
 * void outb(unsigned char value, unsigned short port);
 *
 * Copyright (C) 2024 Anton Ivanov (aka demiurg_spb+rigler).
 * Copyright (C) 2024 Lionel Debroux.
 */


#include <stdint.h>
#include <sys/io.h> // if file not found for 32bit-target in 64bit OS: "sudo apt install gcc-multilib g++-multilib"

#define PIT_CH0_PORT    (0x0040U) // r/w
#define PIT_CH1_PORT    (0x0041U) // r/w
#define PIT_CH2_PORT    (0x0042U) // r/w
#define PIT_CTL_PORT    (0x0043U) // write only
#define PIT_STATUS_PORT (0x0061U)

#define PIT_STATUS_MASK (0b00000011)

#define _PIT_CTL_CH(x)      (((x)&3U)<<6)  // 0-ch0, 1-ch1, 2-ch2, 3-Read-back command (8254 only)

#define PIT_CTL_CH0        _PIT_CTL_CH(0)
#define PIT_CTL_CH1        _PIT_CTL_CH(1)
#define PIT_CTL_CH2        _PIT_CTL_CH(2)
#define PIT_CTL_READ_BACK  _PIT_CTL_CH(3)


#define _PIT_CTL_ACCESS(x)  (((x)&3U)<<4)  // 0-Latch count value command, 1-lbyte, 2-hbyte, 3-lobyte/hibyte

#define PIT_CTL_ACCESS_LATCH_CNT_VAL   _PIT_CTL_ACCESS(0)
#define PIT_CTL_ACCESS_LBYTE           _PIT_CTL_ACCESS(1)
#define PIT_CTL_ACCESS_HBYTE           _PIT_CTL_ACCESS(2)
#define PIT_CTL_ACCESS_LBYTE_HBYTE     _PIT_CTL_ACCESS(3)


#define _PIT_CTL_OP_MODE(x)  (((x)&7U)<<1)

#define PIT_CTL_OP_MODE0   _PIT_CTL_OP_MODE(0) // (interrupt on terminal count)
#define PIT_CTL_OP_MODE1   _PIT_CTL_OP_MODE(1) // (hardware re-triggerable one-shot)
#define PIT_CTL_OP_MODE2   _PIT_CTL_OP_MODE(2) // (rate generator)
#define PIT_CTL_OP_MODE3   _PIT_CTL_OP_MODE(3) // (square wave generator)
#define PIT_CTL_OP_MODE4   _PIT_CTL_OP_MODE(4) // (software triggered strobe)
#define PIT_CTL_OP_MODE5   _PIT_CTL_OP_MODE(5) // (hardware triggered strobe)


#define _PIT_CTL_BCD_MODE(x)  ((x)&1U)

#define PIT_CTL_BIN16_MODE   _PIT_CTL_BCD_MODE(0) // 16-bit binary
#define PIT_CTL_BCD4_MODE    _PIT_CTL_BCD_MODE(1) // four-digit BCD


static inline uint32_t pit_freq2div(unsigned frequency)
{
    return (uint32_t)(0.5f + 1193180.0f/frequency);
}

static inline void pit_off(void)
{
    const unsigned char x = inb(PIT_STATUS_PORT) & ~PIT_STATUS_MASK;

    outb(x, PIT_STATUS_PORT);
}

static inline void pit_init_square_wave_generator(unsigned char ch, uint32_t frequency)
{
    const uint32_t clk_div = pit_freq2div(frequency);

    const unsigned char cfg = PIT_CTL_OP_MODE3 | PIT_CTL_BIN16_MODE | PIT_CTL_ACCESS_LBYTE_HBYTE | _PIT_CTL_CH(ch);

    const unsigned short PIT_DATA_PORT = PIT_CH0_PORT + ch;    // base + offset

    outb(cfg, PIT_CTL_PORT); // 0xb6 = 0b10110110

    outb((uint8_t)clk_div,      PIT_DATA_PORT); // low byte
    outb((uint8_t)(clk_div>>8), PIT_DATA_PORT); // hi byte

    unsigned char x = inb(PIT_STATUS_PORT);

    if (!(x&PIT_STATUS_MASK)) {
         outb(x|PIT_STATUS_MASK, PIT_STATUS_PORT);
    }
}

#endif // PIT_H
