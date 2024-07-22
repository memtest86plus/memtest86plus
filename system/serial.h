#ifndef _SERIAL_REG_H
#define _SERIAL_REG_H
/**
 * \file
 *
 * Provides the TTY interface. It provides an 80x25 VT100 compatible
 * display via Serial/UART.
 *
 *//*
 * Copyright (C) 2004-2023 Sam Demeulemeester.
 */

#define SERIAL_DEFAULT_BITS     8
#define SERIAL_DEFAULT_PARITY   0

#define SERIAL_PORT_0x3F8   0
#define SERIAL_PORT_0x2F8   1
#define SERIAL_PORT_0x3E8   2
#define SERIAL_PORT_0x2E8   3

static const uint16_t serial_io_ports[] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };

struct serial_port {
    bool enable;
    bool is_mmio;
    bool is_usb;
    int parity;
    int bits;
    int baudrate;
    int reg_width;
    int refclk;
    uintptr_t base_addr;
};

/*
 * Definitions for VT100 commands
 */

#define TTY_CLEAR_SCREEN    "\x1b[2J"

#define TTY_DISABLE_CURSOR  "\x1b[?25l"

#define TTY_NORMAL          "\x1b[0m"
#define TTY_BOLD            "\x1b[1m"
#define TTY_UNDERLINE       "\x1b[4m"
#define TTY_INVERSE         "\x1b[7m"

/*
 * Definitions for the Base UART Registers
 */

#define UART_REF_CLK_IO     1843200
#define UART_REF_CLK_MMIO   48000000

#define UART_RX     0   /* In:  Receive buffer (DLAB=0) */
#define UART_TX     0   /* Out: Transmit buffer (DLAB=0) */
#define UART_DLL    0   /* Out: Divisor Latch Low (DLAB=1) */
#define UART_DLM    1   /* Out: Divisor Latch High (DLAB=1) */
#define UART_IER    1   /* Out: Interrupt Enable Register */
#define UART_IIR    2   /* In:  Interrupt ID Register */
#define UART_FCR    2   /* Out: FIFO Control Register */
#define UART_EFR    2   /* I/O: Extended Features Register */
    /* (DLAB=1, 16C660 only) */
#define UART_LCR    3   /* Out: Line Control Register */
#define UART_MCR    4   /* Out: Modem Control Register */
#define UART_LSR    5   /* In:  Line Status Register */
#define UART_MSR    6   /* In:  Modem Status Register */
#define UART_SCR    7   /* I/O: Scratch Register */

/*
 * Definitions for the Line Control Register
 */

#define UART_LCR_DLAB   0x80    /* Divisor latch access bit */
#define UART_LCR_SBC    0x40    /* Set break control */
#define UART_LCR_SPAR   0x20    /* Stick parity (?) */
#define UART_LCR_EPAR   0x10    /* Even parity select */
#define UART_LCR_PARITY 0x08    /* Parity Enable */
#define UART_LCR_STOP   0x04    /* Stop bits: 0=1 stop bit, 1= 2 stop bits */
#define UART_LCR_WLEN5  0x00    /* Wordlength: 5 bits */
#define UART_LCR_WLEN6  0x01    /* Wordlength: 6 bits */
#define UART_LCR_WLEN7  0x02    /* Wordlength: 7 bits */
#define UART_LCR_WLEN8  0x03    /* Wordlength: 8 bits */

/*
 * Definitions for the Line Status Register
 */
#define UART_LSR_TEMT   0x40    /* Transmitter empty */
#define UART_LSR_THRE   0x20    /* Transmit-hold-register empty */
#define UART_LSR_BI     0x10    /* Break interrupt indicator */
#define UART_LSR_FE     0x08    /* Frame error indicator */
#define UART_LSR_PE     0x04    /* Parity error indicator */
#define UART_LSR_OE     0x02    /* Overrun error indicator */
#define UART_LSR_DR     0x01    /* Receiver data ready */

/*
 * Definitions for the Interrupt Identification Register
 */
#define UART_IIR_NO_INT 0x01    /* No interrupts pending */
#define UART_IIR_ID     0x06    /* Mask for the interrupt ID */

#define UART_IIR_MSI    0x00    /* Modem status interrupt */
#define UART_IIR_THRI   0x02    /* Transmitter holding register empty */
#define UART_IIR_RDI    0x04    /* Receiver data interrupt */
#define UART_IIR_RLSI   0x06    /* Receiver line status interrupt */

/*
 * Definitions for the FIFO Control Register
 */
#define UART_FCR_ENA   0x01     /* FIFO Enable */
#define UART_FCR_THR   0x20     /* FIFO Threshold */

/*
 * Definitions for the Interrupt Enable Register
 */
#define UART_IER_MS     0x08    /* Enable Modem status interrupt */
#define UART_IER_RLSI   0x04    /* Enable receiver line status interrupt */
#define UART_IER_THRI   0x02    /* Enable Transmitter holding register int. */
#define UART_IER_RDI    0x01    /* Enable receiver data interrupt */

/*
 * Definitions for the Modem Control Register
 */
#define UART_MCR_LOOP   0x10    /* Enable loopback test mode */
#define UART_MCR_OUT2   0x08    /* Out2 complement */
#define UART_MCR_OUT1   0x04    /* Out1 complement */
#define UART_MCR_RTS    0x02    /* RTS complement */
#define UART_MCR_DTR    0x01    /* DTR complement */

/*
 * Definitions for the Modem Status Register
 */
#define UART_MSR_DCD    0x80    /* Data Carrier Detect */
#define UART_MSR_RI     0x40    /* Ring Indicator */
#define UART_MSR_DSR    0x20    /* Data Set Ready */
#define UART_MSR_CTS    0x10    /* Clear to Send */
#define UART_MSR_DDCD   0x08    /* Delta DCD */
#define UART_MSR_TERI   0x04    /* Trailing edge ring indicator */
#define UART_MSR_DDSR   0x02    /* Delta DSR */
#define UART_MSR_DCTS   0x01    /* Delta CTS */
#define UART_MSR_ANY_DELTA 0x0F /* Any of the delta bits! */

/*
 * Definitions for the Extended Features Register
 * (StarTech 16C660 only, when DLAB=1)
 */
#define UART_EFR_CTS    0x80    /* CTS flow control */
#define UART_EFR_RTS    0x40    /* RTS flow control */
#define UART_EFR_SCD    0x20    /* Special character detect */
#define UART_EFR_ENI    0x10    /* Enhanced Interrupt */

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

#define tty_full_redraw() \
    tty_send_region(0, 0, 24, 79);

#define tty_partial_redraw() \
    tty_send_region(1, 34, 5, 79); \
    tty_send_region(7, 0, 8, 79); \
    if(enable_temperature) tty_send_region(1, 16, 1, 26);

#define tty_error_redraw() \
    tty_send_region(10, 0, 23, 79);

#define tty_normal() \
    serial_echo_print(TTY_NORMAL);

#define tty_inverse() \
    serial_echo_print(TTY_INVERSE);

#define tty_disable_cursor() \
    serial_echo_print(TTY_DISABLE_CURSOR);

#define tty_clear_screen() \
    serial_echo_print(TTY_CLEAR_SCREEN);

void tty_init(void);

void tty_print(int y, int x, const char *p);

void tty_send_region(int start_row, int start_col, int end_row, int end_col);

char tty_get_key(void);

#endif /* _SERIAL_REG_H */
