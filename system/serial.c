// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2022 Sam Demeulemeester

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "string.h"
#include "serial.h"
#include "unistd.h"

#include "config.h"
#include "display.h"

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
    } else {
        return __inb(reg_walker.addr);
    }
}

static void serial_wait_for_xmit(struct serial_port *port)
{
    uint8_t lsr;

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
    /* Now, do each character */
    while (*p) {
        serial_wait_for_xmit(port);

        /* Send the character out. */
        serial_write_reg(port, UART_TX, *p);
        if (*p==10) {
            serial_wait_for_xmit(port);
            serial_write_reg(port, UART_TX, 13);
        }
        p++;
    }
}

void tty_print(int y, int x, const char *p)
{
    static char sx[3], sy[3];

    itoa(++x,sx);
    itoa(++y,sy);

    serial_echo_print("\x1b[");
    serial_echo_print(sy);
    serial_echo_print(";");
    serial_echo_print(sx);
    serial_echo_print("H");
    serial_echo_print(p);
}

//------------------------------------------------------------------------------
// Public Functions
//------------------------------------------------------------------------------

void tty_init(void)
{
    if (!enable_tty) {
        return;
    }

    int uart_status, serial_div;
    unsigned char lcr;

    console_serial.enable       = true;
    console_serial.is_mmio      = false;
    console_serial.parity       = SERIAL_DEFAULT_PARITY;
    console_serial.bits         = SERIAL_DEFAULT_BITS;
    console_serial.baudrate     = tty_params_baud;
    console_serial.reg_width    = 1;
    console_serial.refclk       = UART_REF_CLK;
    console_serial.base_addr    = serial_base_ports[tty_params_port];

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
    uart_status = serial_read_reg(&console_serial, UART_LSR);           /* COM? LSR */
    uart_status = serial_read_reg(&console_serial, UART_RX);	        /* COM? RBR */
    serial_write_reg(&console_serial, UART_IER, 0x00);              /* Disable all interrupts */

    tty_clear_screen();
    tty_disable_cursor();
}

void tty_send_region(int start_row, int start_col, int end_row, int end_col)
{
    char p[SCREEN_WIDTH+1];
    int col_len = end_col - start_col;

    if (start_col > (SCREEN_WIDTH - 1) || end_col > (SCREEN_WIDTH - 1)) {
        return;
    }

    if (start_row > (SCREEN_HEIGHT - 1) || end_row > (SCREEN_HEIGHT - 1)) {
        return;
    }

    for (int row = start_row; row <= end_row; row++) {

        // Last line is inverted (Black on white)
        if (row == SCREEN_HEIGHT-1) {
            tty_inverse();
        }

        // Copy Shadow buffer to TTY buffer
        for (int col = start_col; col <= end_col; col++) {
            p[col] = shadow_buffer[row][col].value & 0x7F;
        }

        // Add string terminator
        p[end_col+1] = '\0';

        // For first line, title on top-left must be inverted
        // Do the switch, send to TTY then continue to next line.
        if (row == 0 && start_col == 0 && col_len > 28) {
            tty_inverse();
            p[28] = '\0';
            tty_print(row,0,p);
            tty_normal();
            p[28] = '|';
            tty_print(row, 28, p + 28);
            continue;
        }

        // Replace degree symbol with '*' for tty to avoid a C437/VT100 translation table.
        if (row == 7 && col_len > 77 && (shadow_buffer[7][73].value & 0xFF) == 0xF8) {
            p[73] = 0x2A;
        }

        // Send row to TTY
        tty_print(row, start_col, p + start_col);

        // Revert to normal if last line.
        if (row == SCREEN_HEIGHT-1) {
            tty_normal();
        }
    }
}

char tty_get_key(void)
{
    int uart_status = serial_read_reg(&console_serial, UART_LSR);

    if (uart_status & UART_LSR_DR) {
        return serial_read_reg(&console_serial, UART_RX);
    } else {
        return 0xFF;
    }
}
