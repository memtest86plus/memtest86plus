// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2023 Sam Demeulemeester

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "string.h"
#include "serial.h"
#include "unistd.h"

#include "config.h"
#include "display.h"

#include "usbhcd.h"

static struct serial_port console_serial;

//------------------------------------------------------------------------------
// Private Functions
//------------------------------------------------------------------------------

static void serial_write_reg(struct serial_port *port, uint16_t reg, uint8_t val)
{
    union {
        uintptr_t addr;
        uint8_t *ptr;
    } reg_walker;

    reg_walker.addr = port->base_addr + reg * port->reg_width;

    if (port->is_mmio) {
        *reg_walker.ptr = val;
    } else if (port->is_usb) {
        // ignore
    } else {
        __outb(val, reg_walker.addr);
    }
}

static uint8_t serial_read_reg(struct serial_port *port, uint16_t reg)
{
    union {
        uintptr_t addr;
        uint8_t *ptr;
    } reg_walker;

    reg_walker.addr = port->base_addr + reg * port->reg_width;

    if (port->is_mmio) {
        return *reg_walker.ptr;
    } else if (port->is_usb) {
        return 0; // ignore
    } else {
        return __inb(reg_walker.addr);
    }
}

static void serial_wait_for_xmit(struct serial_port *port)
{
    uint8_t lsr;

    if (port->is_usb) {
        return; // TODO
    }
    do {
        lsr = serial_read_reg(port, UART_LSR);
    } while ((lsr & BOTH_EMPTY) != BOTH_EMPTY);
}

void serial_echo_print(const char *p)
{
    struct serial_port *port = &console_serial;

    if (!port->enable) {
        return;
    }

    if (port->is_usb) {
        usb_serial_print(p);
	return;
    }

    /* Now, do each character */
    while (*p) {
        /* Send the character out. */
        serial_wait_for_xmit(port);
        serial_write_reg(port, UART_TX, *p++);
    }
}

void tty_goto(int y, int x)
{
    static char s[3];

    serial_echo_print("\x1b[");
    serial_echo_print(itoa(y + 1, s));
    serial_echo_print(";");
    serial_echo_print(itoa(x + 1, s));
    serial_echo_print("H");
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void tty_init(void)
{
    if (!enable_tty) {
        return;
    }

    if (tty_usb) {
        console_serial.is_usb = true;
    }

    int uart_status, serial_div;
    unsigned char lcr;

    console_serial.enable       = true;
    console_serial.base_addr    = tty_address;
    console_serial.baudrate     = tty_baud_rate;
    console_serial.parity       = SERIAL_DEFAULT_PARITY;
    console_serial.bits         = SERIAL_DEFAULT_BITS;

    // UART MMIO Address is usually above TOLUD and never < 1MB
    if (console_serial.base_addr > 0xFFFF) {
        console_serial.is_mmio      = true;
        console_serial.reg_width    = tty_mmio_stride;
        console_serial.refclk       = tty_mmio_ref_clk;
    } else {
        console_serial.is_mmio      = false;
        console_serial.reg_width    = 1;
        console_serial.refclk       = UART_REF_CLK_IO;
    }

    /* read the Divisor Latch */
    uart_status = serial_read_reg(&console_serial, UART_LCR);
    serial_write_reg(&console_serial, UART_LCR, uart_status | UART_LCR_DLAB);
    serial_read_reg(&console_serial, UART_DLM);
    serial_read_reg(&console_serial, UART_DLL);
    serial_write_reg(&console_serial, UART_LCR, uart_status);

    /* now do hardwired init */
    lcr = console_serial.parity | (console_serial.bits - 5);
    serial_write_reg(&console_serial, UART_LCR, lcr);               /* No parity, 8 data bits, 1 stop */
    serial_div = (console_serial.refclk / console_serial.baudrate) / 16;
    serial_write_reg(&console_serial, UART_LCR, 0x80|lcr);          /* Access divisor latch */
    serial_write_reg(&console_serial, UART_DLL, serial_div & 0xff); /* baud rate divisor */
    serial_write_reg(&console_serial, UART_DLM, (serial_div >> 8) & 0xff);
    serial_write_reg(&console_serial, UART_LCR, lcr);               /* Done with divisor */

    /* Prior to disabling interrupts, read the LSR and RBR registers */
    uart_status = serial_read_reg(&console_serial, UART_LSR);       /* COM? LSR */
    uart_status = serial_read_reg(&console_serial, UART_RX);        /* COM? RBR */
    serial_write_reg(&console_serial, UART_IER, 0x00);              /* Disable all interrupts */

    /* In case of MMIO UART, set up FIFO Reg */
    if (console_serial.is_mmio) {
        serial_write_reg(&console_serial, UART_FCR, 0x00);
        serial_write_reg(&console_serial, UART_FCR, (0xFF) & (UART_FCR_ENA | UART_FCR_THR));
    }

    tty_clear_screen();
    tty_disable_cursor();
}

void tty_send_region(int start_row, int start_col, int end_row, int end_col)
{
    char p[SCREEN_WIDTH+1];
    uint8_t ch;
    int pos = 0;
    int cur_inverse = -1, inverse = false;

    if (start_col > (SCREEN_WIDTH - 1) || end_col > (SCREEN_WIDTH - 1)) {
        return;
    }

    if (start_row > (SCREEN_HEIGHT - 1) || end_row > (SCREEN_HEIGHT - 1)) {
        return;
    }

    for (int row = start_row; row <= end_row; row++) {

        // Always use absolute positioning instead of relying on CR-LF to avoid issues
        // when a CR-LF is lost (especially with Industrial RS232/Ethernet converters).
        tty_goto(row, start_col);

        // Copy Shadow buffer to TTY buffer
        pos = 0;
        for (int col = start_col; col <= end_col; col++) {

            inverse = ((shadow_buffer[row][col].attr & 0x70) >> 4 != BLUE);

            if (cur_inverse != inverse) {

                if (pos) {
                  p[pos] = '\0';
                  serial_echo_print(p);
                  pos = 0;
                }

                if (inverse) {
                    tty_inverse();
                } else {
                    tty_normal();
                }

                cur_inverse = inverse;
            }

            /* Make sure only VT100 characters are sent. */
            ch = shadow_buffer[row][col].ch;

            switch (ch) {
                case 32 ... 127:
                    break;

                case 0xB3:
                    ch = '|';
                    break;

                case 0xC1:
                case 0xC2:
                case 0xC4:
                    ch = '-';
                    break;

                case 0xF8:
                    ch = '*';
                    break;

                default:
                    ch = '?';
            }

            p[pos++] = ch;
        }

        if (pos) {
            p[pos] = '\0';
            serial_echo_print(p);
        }
    }
}

char tty_get_key(void)
{
    int uart_status;

    if (console_serial.is_usb) {
        return 0xFF; // not supported
    }

    uart_status = serial_read_reg(&console_serial, UART_LSR);

    if (uart_status & UART_LSR_DR) {
        return serial_read_reg(&console_serial, UART_RX);
    } else {
        return 0xFF;
    }
}
