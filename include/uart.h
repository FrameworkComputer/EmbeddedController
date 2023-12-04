/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* uart.h - UART module for Chrome EC */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#ifndef __CROS_EC_UART_H
#define __CROS_EC_UART_H

#include "common.h"
#include "gpio_signal.h"

#include <stdarg.h> /* For va_list */

/**
 * Initialize the UART module.
 */
void uart_init(void);

/**
 * Return non-zero if UART init has completed.
 */
int uart_init_done(void);

/*
 * Output functions
 *
 * Output is buffered.  If the buffer overflows, subsequent output is
 * discarded.
 *
 * Modules should use the output functions in console.h in preference to these
 * routines, so that output can be filtered on a module-by-module basis.
 */

/**
 * Put a single character to the UART, like putchar().
 *
 * @param c		Character to put
 * @return EC_SUCCESS, or non-zero if output was truncated.
 */
int uart_putc(int c);

/**
 * Put a null-terminated string to the UART, like fputs().
 *
 * @return EC_SUCCESS, or non-zero if output was truncated.
 */
int uart_puts(const char *outstr);

/**
 * Put byte stream to the UART while translating '\n' to '\r\n'
 *
 * @param out		Pointer to data to send
 * @param len		Length of transfer in bytes
 * @return number of characters successfully written.
 */
int uart_put(const char *out, int len);

/**
 * Put raw byte stream to the UART
 *
 * @param out		Pointer to data to send
 * @param len		Length of transfer in bytes
 * @return number of characters successfully written.
 */
int uart_put_raw(const char *out, int len);

/**
 * Print formatted output to the UART, like printf().
 *
 * See printf.h for valid formatting codes.
 *
 * @return EC_SUCCESS, or non-zero if output was truncated.
 */
__attribute__((__format__(__printf__, 1, 2))) int
uart_printf(const char *format, ...);

/**
 * Print formatted output to the UART, like vprintf().
 *
 * See printf.h for valid formatting codes.
 *
 * @return EC_SUCCESS, or non-zero if output was truncated.
 */
int uart_vprintf(const char *format, va_list args);

/**
 * Put a single character into the transmit buffer.
 *
 * Does not enable the transmit interrupt; assumes that happens elsewhere.
 *
 * @param context	Context; ignored.
 * @param c		Character to write.
 * @return 0 if the character was transmitted, 1 if it was dropped.
 *
 * Note: This is intended to be implemented by the UART buffering
 * module, and called only by the implementations of the uart_*
 * functions.  You should stick to the higher level functions, such as
 * uart_putc, outside of the UART implementation.
 */
int uart_tx_char_raw(void *context, int c);

/**
 * Flush output.  Blocks until UART has transmitted all output.
 */
void uart_flush_output(void);

/*
 * Input functions
 *
 * Input is buffered.  If the buffer overflows, the oldest input in
 * the buffer is discarded to make room for the new input.
 *
 * Input lines may be terminated by CR ('\r'), LF ('\n'), or CRLF; all
 * are translated to newline.
 */

/**
 * Read a single character of input, similar to fgetc().
 *
 * @return the character, or -1 if no input waiting.
 */
int uart_getc(void);

/*
 * Hardware UART driver functions
 */

/**
 * Flush the transmit FIFO.
 */
void uart_tx_flush(void);

/**
 * Return non-zero if there is room to transmit a character immediately.
 */
int uart_tx_ready(void);

/**
 * Return non-zero if a transmit is in progress.
 */
int uart_tx_in_progress(void);

/**
 * Return non-zero if UART is ready to start a DMA transfer.
 */
int uart_tx_dma_ready(void);

/**
 * Start a UART transmit DMA transfer
 *
 * @param src		Pointer to data to send
 * @param len		Length of transfer in bytes
 */
void uart_tx_dma_start(const char *src, int len);

/**
 * Return non-zero if the UART has a character available to read.
 */
int uart_rx_available(void);

/**
 * Start a UART receive DMA transfer.
 *
 * DMA will be configured in circular buffer mode, so received characters
 * will be stored into the buffer continuously.
 *
 * @param dest		Pointer to destination buffer
 * @param len		Length of buffer in bytes
 */
void uart_rx_dma_start(char *dest, int len);

/**
 * Return the head of the receive DMA transfer buffer
 *
 * This is the next offset in the buffer which will receive a character, and
 * will be from 0..(len-1) where len is the buffer length passed to
 * uart_rx_dma_start().
 */
int uart_rx_dma_head(void);

/**
 * Send a character to the UART data register.
 *
 * If the transmit FIFO is full, blocks until there is space.
 *
 * @param c		Character to send.
 */
void uart_write_char(char c);

/**
 * Read one char from the UART data register.
 *
 * @return		The character read.
 */
int uart_read_char(void);

/**
 * Re-enable the UART transmit interrupt.
 *
 * This also forces triggering a UART interrupt, if the transmit interrupt was
 * disabled.
 */
void uart_tx_start(void);

/**
 * Disable the UART transmit interrupt.
 */
void uart_tx_stop(void);

/**
 * Helper for processing UART input.
 *
 * Reads the input FIFO until empty.  Intended to be called from the driver
 * interrupt handler.
 */
void uart_process_input(void);

/**
 * Clear input buffer
 */
void uart_clear_input(void);

/**
 * Helper for processing UART output.
 *
 * Fills the output FIFO until the transmit buffer is empty or the FIFO full.
 * Intended to be called from the driver interrupt handler.
 */
void uart_process_output(void);

/**
 * Return boolean expressing whether UART buffer is empty or not.
 */
int uart_buffer_empty(void);

/**
 * Return boolean expressing whether UART buffer is full or not.
 */
int uart_buffer_full(void);

/**
 * Return the number of bytes in the tx buffer
 */
int uart_buffer_used(void);

/**
 * Disable the EC console UART and convert the UART RX pin to a generic GPIO
 * with an edge detect interrupt.
 */
void uart_enter_dsleep(void);

/**
 * Enable the EC console UART after a uart_enter_dsleep().
 */
void uart_exit_dsleep(void);

#ifdef CONFIG_LOW_POWER_IDLE
/**
 * Interrupt handler for UART RX pin transition in deep sleep.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void uart_deepsleep_interrupt(enum gpio_signal signal);
#else
static inline void uart_deepsleep_interrupt(enum gpio_signal signal)
{
}
#endif /* !CONFIG_LOW_POWER_IDLE */

#if defined(HAS_TASK_CONSOLE) && defined(CONFIG_FORCE_CONSOLE_RESUME)
/**
 * Enable/Disable the UART controller low-power mode wake-up capability.
 *
 * @param enable  1 to enable wake-up, 0 to disable it.
 */
void uart_enable_wakeup(int enable);
#elif !defined(CHIP_FAMILY_NPCX5)
static inline void uart_enable_wakeup(int enable)
{
}
#endif

#ifdef CONFIG_UART_INPUT_FILTER
/**
 * Application-specific input filter, which takes the next input character as
 * a parameter.
 *
 * Return 0 to allow the character to be handled by the console, non-zero if
 * the character was handled by the filter.
 */
int uart_input_filter(int c);
#endif

/*
 * COMx functions
 */

/**
 * Enable COMx interrupts
 */
void uart_comx_enable(void);

/**
 * Return non-zero if ok to put a character via uart_comx_putc().
 */
int uart_comx_putc_ok(void);

/**
 * Write a character to the COMx UART interface.
 */
void uart_comx_putc(int c);

/*
 * Functions for pad switching UART, only defined on some chips (npcx), and
 * if CONFIG_UART_PAD_SWITCH is enabled.
 */
enum uart_pad {
	UART_DEFAULT_PAD = 0,
	UART_ALTERNATE_PAD = 1,
};

/**
 * Reset UART pad to default pad, so that a panic information can be printed
 * on the EC console.
 */
void uart_reset_default_pad_panic(void);

/**
 * Specialized function to write then read data on UART alternate pad.
 * The transfer may be interrupted at any time if data is received on the main
 * pad.
 *
 * @param tx		Data to be sent
 * @param tx_len	Length of data to be sent
 * @param rx		Buffer to receive data
 * @param rx_len	Receive buffer length
 * @param timeout_us	Timeout in microseconds for the transaction to complete.
 *
 * @return The number of bytes read back (indicates a timeout if != rx_len).
 *         - -EC_ERROR_BUSY if the alternate pad cannot be used (e.g. default
 *           pad is currently being used), or if the transfer was interrupted.
 *         - -EC_ERROR_TIMEOUT in case tx_len bytes cannot be written in the
 *           time specified in timeout_us.
 */
int uart_alt_pad_write_read(uint8_t *tx, int tx_len, uint8_t *rx, int rx_len,
			    int timeout_us);

/**
 * Interrupt handler for default UART RX pin transition when UART is switched
 * to alternate pad.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void uart_default_pad_rx_interrupt(enum gpio_signal signal);

/**
 * Prepare for following `uart_console_read_buffer()` call.  It will create a
 * snapshot of current uart buffer.
 *
 * @return result status (EC_RES_*)
 */
enum ec_status uart_console_read_buffer_init(void);

/**
 * Read from uart buffer.
 *
 * `uart_console_read_buffer_init()` must be called first.
 *
 * If `type` is CONSOLE_READ_NEXT, this will return data starting from the
 * beginning of the last snapshot created by `uart_console_read_buffer_init()`.
 *
 * If `type` is CONSOLE_READ_RECENT, this will start from the end of the
 * previous snapshot (so if current snapshot and previous snapshot has overlaps,
 * only new content will be returned).
 *
 * @param type		an ec_console_read_subcmd value.
 * @param dest		output buffer, it will be a null-terminated string.
 * @param dest_size	size of output buffer.
 * @param write_count	number of bytes written (including '\0').
 *
 * @return result status (EC_RES_*)
 */
int uart_console_read_buffer(uint8_t type, char *dest, uint16_t dest_size,
			     uint16_t *write_count);

/**
 * Initialize tx buffer head and tail
 */
void uart_init_buffer(void);

#endif /* __CROS_EC_UART_H */
