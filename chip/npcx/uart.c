/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for Chrome EC */

#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hwtimer.h"
#include "hwtimer_chip.h"
#include "lpc.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "uartn.h"
#include "util.h"

#define CONSOLE_UART CONFIG_CONSOLE_UART

#if CONSOLE_UART
#define CONSOLE_UART_IRQ NPCX_IRQ_UART2
#else
#define CONSOLE_UART_IRQ NPCX_IRQ_UART
#endif

static int init_done;

#ifdef CONFIG_UART_PAD_SWITCH

/* Current pad: 0 for default pad, 1 for alternate. */
static volatile enum uart_pad pad;

/*
 * When switched to alternate pad, read/write data according to the parameters
 * below.
 */
static uint8_t *altpad_rx_buf;
static volatile int altpad_rx_pos;
static int altpad_rx_len;
static uint8_t *altpad_tx_buf;
static volatile int altpad_tx_pos;
static int altpad_tx_len;

/*
 * Time we last received a byte on default UART, we do not allow use of
 * alternate pad for block_alt_timeout_us after that, to make sure input
 * characters are not lost (either interactively, or though servod/FAFT).
 */
static timestamp_t last_default_pad_rx_time;

static const uint32_t block_alt_timeout_us = 500 * MSEC;

#else

/* Default pad is always selected. */
static const enum uart_pad pad = UART_DEFAULT_PAD;

#endif /* CONFIG_UART_PAD_SWITCH */

#if defined(CHIP_FAMILY_NPCX5)
/* This routine switches the functionality from UART rx to GPIO */
void npcx_uart2gpio(void)
{
	/* Switch both pads back to GPIO mode. */
	CLEAR_BIT(NPCX_DEVALT(0x0C), NPCX_DEVALTC_UART_SL2);
	CLEAR_BIT(NPCX_DEVALT(0x0A), NPCX_DEVALTA_UART_SL1);
}
#endif /* CHIP_FAMILY_NPCX5 */

/*
 * This routine switches the functionality from GPIO to UART rx, depending
 * on the global variable "pad". It also deactivates the previous pad.
 *
 * Note that, when switching pad, we first configure the new pad, then switch
 * off the old one, to avoid having no pad selected at a given time, see
 * b/65526215#c26.
 */
void npcx_gpio2uart(void)
{
#ifdef CONFIG_UART_PAD_SWITCH
	if (pad == UART_ALTERNATE_PAD) {
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
		SET_BIT(NPCX_UART_ALT_DEVALT, NPCX_UART_ALT_DEVALT_SIN__SL);
		SET_BIT(NPCX_UART_ALT_DEVALT, NPCX_UART_ALT_DEVALT_SOUT_SL);
		CLEAR_BIT(NPCX_UART_DEVALT, NPCX_UART_DEVALT_SIN_SL);
		CLEAR_BIT(NPCX_UART_DEVALT, NPCX_UART_DEVALT_SOUT_SL);
#else
		SET_BIT(NPCX_UART_ALT_DEVALT, NPCX_UART_ALT_DEVALT_SL);
		CLEAR_BIT(NPCX_UART_DEVALT, NPCX_UART_DEVALT_SL);
#endif
		return;
	}
#endif

#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
	SET_BIT(NPCX_UART_DEVALT, NPCX_UART_DEVALT_SIN_SL);
	SET_BIT(NPCX_UART_DEVALT, NPCX_UART_DEVALT_SOUT_SL);
	CLEAR_BIT(NPCX_UART_ALT_DEVALT, NPCX_UART_ALT_DEVALT_SIN_SL);
	CLEAR_BIT(NPCX_UART_ALT_DEVALT, NPCX_UART_ALT_DEVALT_SOUT_SL);
#else
	SET_BIT(NPCX_UART_DEVALT, NPCX_UART_DEVALT_SL);
	CLEAR_BIT(NPCX_UART_ALT_DEVALT, NPCX_UART_ALT_DEVALT_SL);
#endif

#if !NPCX_UART_MODULE2 && (NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7)
	/*
	 * UART module 1 belongs to KSO since wake-up functionality in npcx7
	 * and later chips.
	 */
	CLEAR_BIT(NPCX_DEVALT(0x09), NPCX_DEVALT9_NO_KSO09_SL);
#endif
}

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_start(void)
{
#if defined(CHIP_FAMILY_NPCX5)
	if (uart_is_enable_wakeup() && pad == UART_DEFAULT_PAD) {
		/* disable MIWU */
		uart_enable_wakeup(0);
		/* Set pin-mask for UART */
		npcx_gpio2uart();
		/* enable uart again from MIWU mode */
		task_enable_irq(NPCX_IRQ_UART);
	}
#endif

	uartn_tx_start(CONSOLE_UART);
}

void uart_tx_stop(void)
{
#ifdef NPCX_UART_FIFO_SUPPORT
	uartn_tx_stop(CONSOLE_UART, 0);
#else
	uint8_t sleep_ena;

	sleep_ena = (pad == UART_DEFAULT_PAD) ? 1 : 0;
	uartn_tx_stop(CONSOLE_UART, sleep_ena);
#endif
}

void uart_tx_flush(void)
{
	uartn_tx_flush(CONSOLE_UART);
}

int uart_tx_ready(void)
{
	return uartn_tx_ready(CONSOLE_UART);
}

int uart_tx_in_progress(void)
{
	return uartn_tx_in_progress(CONSOLE_UART);
}

int uart_rx_available(void)
{
	int rx_available = uartn_rx_available(CONSOLE_UART);

	if (rx_available && pad == UART_DEFAULT_PAD) {
#ifdef CONFIG_LOW_POWER_IDLE
		/*
		 * Activity seen on UART RX pin while UART was disabled for deep
		 * sleep. The console won't see that character because the UART
		 * is disabled, so we need to inform the clock module of UART
		 * activity ourselves.
		 */
		clock_refresh_console_in_use();
#endif
#ifdef CONFIG_UART_PAD_SWITCH
		last_default_pad_rx_time = get_time();
#endif
	}
	return rx_available; /* If RX FIFO is empty return '0'. */
}

void uart_write_char(char c)
{
	uartn_write_char(CONSOLE_UART, c);
}

int uart_read_char(void)
{
	return uartn_read_char(CONSOLE_UART);
}

/* Interrupt handler for Console UART */
static void uart_ec_interrupt(void)
{
#ifdef CONFIG_UART_PAD_SWITCH
	if (pad == UART_ALTERNATE_PAD) {
		if (uartn_rx_available(NPCX_UART_PORT0)) {
			uint8_t c = uartn_read_char(NPCX_UART_PORT0);

			if (altpad_rx_pos < altpad_rx_len)
				altpad_rx_buf[altpad_rx_pos++] = c;
		}
		if (uartn_tx_ready(NPCX_UART_PORT0)) {
			if (altpad_tx_pos < altpad_tx_len)
				uartn_write_char(
					NPCX_UART_PORT0,
					altpad_tx_buf[altpad_tx_pos++]);
			else
				uart_tx_stop();
		}
		return;
	}
#endif
#ifdef NPCX_UART_FIFO_SUPPORT
	if (!uartn_tx_in_progress(CONSOLE_UART)) {
		if (uart_buffer_empty()) {
			uartn_enable_tx_complete_int(CONSOLE_UART, 0);
			enable_sleep(SLEEP_MASK_UART);
		}
	}
#endif

	/* Default pad. */
	/* Read input FIFO until empty, then fill output FIFO */
	uart_process_input();
	uart_process_output();
}
#ifdef NPCX_UART_FIFO_SUPPORT
DECLARE_IRQ(CONSOLE_UART_IRQ, uart_ec_interrupt, 4);
#else
DECLARE_IRQ(CONSOLE_UART_IRQ, uart_ec_interrupt, 1);
#endif

#ifdef CONFIG_UART_PAD_SWITCH
/*
 * Switch back to default UART pad, without flushing RX/TX buffers: If we are
 * about to panic, we just want to switch immmediately, and we don't care if we
 * output a bit of garbage.
 */
void uart_reset_default_pad_panic(void)
{
	pad = UART_DEFAULT_PAD;

	/* Configure new pad. */
	npcx_gpio2uart();

	/* Wait for ~2 bytes, to help the receiver resync. */
	udelay(200);
}

static void uart_set_pad(enum uart_pad newpad)
{
#ifdef NPCX_UART_FIFO_SUPPORT
	NPCX_UFTCTL(NPCX_UART_PORT0) &= ~0xE0;
	NPCX_UFRCTL(NPCX_UART_PORT0) &= ~0xE0;
#else
	NPCX_UICTRL(NPCX_UART_PORT0) = 0x00;
#endif
	task_disable_irq(NPCX_IRQ_UART);

	/* Flush the last byte */
	uartn_tx_flush(NPCX_UART_PORT0);
	uart_tx_stop();

	/*
	 * Allow deep sleep when default pad is selected (sleep is inhibited
	 * during TX). Disallow deep sleep when alternate pad is selected.
	 */
	if (newpad == UART_DEFAULT_PAD)
		enable_sleep(SLEEP_MASK_UART);
	else
		disable_sleep(SLEEP_MASK_UART);

	pad = newpad;

	/* Configure new pad. */
	npcx_gpio2uart();

	/* Re-enable receive interrupt. */
	uartn_rx_int_en(NPCX_UART_PORT0);

	/*
	 * If pad is switched while a byte is being received, the last byte may
	 * be corrupted, let's wait for ~1 byte (9/115200 = 78 us + margin),
	 * then flush the FIFO. See b/65526215.
	 */
	udelay(100);
	uartn_clear_rx_fifo(NPCX_UART_PORT0);

	task_enable_irq(NPCX_IRQ_UART);
}

/* TODO(b:67026316): Remove this and replace with software flow control. */
void uart_default_pad_rx_interrupt(enum gpio_signal signal)
{
	/*
	 * We received an interrupt on the primary pad, give up on the
	 * transaction and switch back.
	 */
	gpio_disable_interrupt(GPIO_UART_MAIN_RX);

#ifdef CONFIG_LOW_POWER_IDLE
	clock_refresh_console_in_use();
#endif
	last_default_pad_rx_time = get_time();

	uart_set_pad(UART_DEFAULT_PAD);
}

int uart_alt_pad_write_read(uint8_t *tx, int tx_len, uint8_t *rx, int rx_len,
			    int timeout_us)
{
	uint32_t start = __hw_clock_source_read();
	int ret = 0;

	if ((get_time().val - last_default_pad_rx_time.val) <
	    block_alt_timeout_us)
		return -EC_ERROR_BUSY;

	cflush();

	altpad_rx_buf = rx;
	altpad_rx_pos = 0;
	altpad_rx_len = rx_len;
	altpad_tx_buf = tx;
	altpad_tx_pos = 0;
	altpad_tx_len = tx_len;

	/*
	 * Turn on additional pull-up during transaction: that prevents the line
	 * from going low in case the base gets disconnected during the
	 * transaction. See b/68954760.
	 */
	gpio_set_flags(GPIO_EC_COMM_PU, GPIO_OUTPUT | GPIO_HIGH);

	uart_set_pad(UART_ALTERNATE_PAD);
	gpio_clear_pending_interrupt(GPIO_UART_MAIN_RX);
	gpio_enable_interrupt(GPIO_UART_MAIN_RX);
	uartn_tx_start(NPCX_UART_PORT0);

	do {
		crec_usleep(100);

		/* Pad switched during transaction. */
		if (pad != UART_ALTERNATE_PAD) {
			ret = -EC_ERROR_BUSY;
			goto out;
		}

		if (altpad_rx_pos == altpad_rx_len &&
		    altpad_tx_pos == altpad_tx_len)
			break;
	} while ((__hw_clock_source_read() - start) < timeout_us);

	gpio_disable_interrupt(GPIO_UART_MAIN_RX);
	uart_set_pad(UART_DEFAULT_PAD);

	if (altpad_tx_pos == altpad_tx_len)
		ret = altpad_rx_pos;
	else
		ret = -EC_ERROR_TIMEOUT;

out:
	gpio_set_flags(GPIO_EC_COMM_PU, GPIO_INPUT);

	altpad_rx_len = 0;
	altpad_rx_pos = 0;
	altpad_rx_buf = NULL;
	altpad_tx_len = 0;
	altpad_tx_pos = 0;
	altpad_tx_buf = NULL;

	return ret;
}
#endif
void uart_init(void)
{
	uartn_init(CONSOLE_UART);
	init_done = 1;
}
