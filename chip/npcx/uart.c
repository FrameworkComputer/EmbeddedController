/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hwtimer.h"
#include "hwtimer_chip.h"
#include "lpc.h"
#include "registers.h"
#include "clock_chip.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

#ifdef NPCX_UART_FIFO_SUPPORT
/* Enable UART Tx FIFO empty interrupt */
#define NPCX_UART_TX_EMPTY_INT_EN()      \
			(SET_BIT(NPCX_UFTCTL, NPCX_UFTCTL_TEMPTY_EN))
/* True if UART Tx FIFO empty interrupt is enabled */
#define NPCX_UART_TX_EMPTY_INT_IS_EN()   \
			(IS_BIT_SET(NPCX_UFTCTL, NPCX_UFTCTL_TEMPTY_EN))
/* Disable UART Tx FIFO empty interrupt */
#define NPCX_UART_TX_EMPTY_INT_DIS()     \
			(CLEAR_BIT(NPCX_UFTCTL, NPCX_UFTCTL_TEMPTY_EN))
/* True if the Tx FIFO is not completely full */
#define NPCX_UART_TX_IS_READY()          \
			(!(GET_FIELD(NPCX_UFTSTS, NPCX_UFTSTS_TEMPTY_LVL) == 0))
/*
 * True if Tx is in progress
 * (i.e. FIFO is not empty or last byte in TSFT (Transmit Shift register)
 * is not sent)
 */
#define NPCX_UART_TX_IN_XMIT()           \
			(!IS_BIT_SET(NPCX_UFTSTS, NPCX_UFTSTS_NXMIP))

/*
 * Enable to generate interrupt when there is at least one byte
 * in the receive FIFO
 */
#define NPCX_UART_RX_INT_EN()            \
			(SET_BIT(NPCX_UFRCTL, NPCX_UFRCTL_RNEMPTY_EN))
/* True if at least one byte is in the receive FIFO */
#define NPCX_UART_RX_IS_AVAILABLE()      \
			(IS_BIT_SET(NPCX_UFRSTS, NPCX_UFRSTS_RFIFO_NEMPTY_STS))
#else
/* Enable UART Tx buffer empty interrupt */
#define NPCX_UART_TX_EMPTY_INT_EN()      (NPCX_UICTRL |= 0x20)
/* True if UART Tx buffer empty interrupt is enabled */
#define NPCX_UART_TX_EMPTY_INT_IS_EN()   (NPCX_UICTRL & 0x20)
/* Disable UART Tx buffer empty interrupt */
#define NPCX_UART_TX_EMPTY_INT_DIS()     (NPCX_UICTRL &= ~0x20)
/* True if 1-byte Tx buffer is empty */
#define NPCX_UART_TX_IS_READY()          (NPCX_UICTRL & 0x01)
/*
 * True if Tx is in progress
 * (i.e. Tx buffer is not empty or last byte in TSFT (Transmit Shift register)
 * is not sent)
 */
#define NPCX_UART_TX_IN_XMIT()           (NPCX_USTAT & 0x40)
 /* Enable to generate interrupt when there is data in the receive buffer */
#define NPCX_UART_RX_INT_EN()            (NPCX_UICTRL = 0x40)
/* True if there is data in the 1-byte Receive buffer */
#define NPCX_UART_RX_IS_AVAILABLE()      (NPCX_UICTRL & 0x02)
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

static const uint32_t block_alt_timeout_us = 500*MSEC;

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
		SET_BIT(NPCX_UART_ALT_DEVALT, NPCX_UART_ALT_DEVALT_SL);
		CLEAR_BIT(NPCX_UART_DEVALT, NPCX_UART_DEVALT_SL);
		return;
	}
#endif

	SET_BIT(NPCX_UART_DEVALT, NPCX_UART_DEVALT_SL);
	CLEAR_BIT(NPCX_UART_ALT_DEVALT, NPCX_UART_ALT_DEVALT_SL);

#if !NPCX_UART_MODULE2 && defined(CHIP_FAMILY_NPCX7)
	/* UART module 1 belongs to KSO since wake-up functionality in npcx7. */
	CLEAR_BIT(NPCX_DEVALT(0x09), NPCX_DEVALT9_NO_KSO09_SL);
#endif
}

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_start(void)
{
	/* We needn't to switch uart from gpio again in npcx7. */
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

	/* If interrupt is already enabled, nothing to do */
	if (NPCX_UART_TX_EMPTY_INT_IS_EN())
		return;

	/* Do not allow deep sleep while transmit in progress */
	disable_sleep(SLEEP_MASK_UART);

	/*
	 * Re-enable the transmit interrupt, then forcibly trigger the
	 * interrupt.  This works around a hardware problem with the
	 * UART where the FIFO only triggers the interrupt when its
	 * threshold is _crossed_, not just met.
	 */
	NPCX_UART_TX_EMPTY_INT_EN();

	task_trigger_irq(NPCX_IRQ_UART);
}

void uart_tx_stop(void)
{
	/* Disable TX interrupt */
	NPCX_UART_TX_EMPTY_INT_DIS();

	/*
	 * Re-allow deep sleep when transmiting on the default pad (deep sleep
	 * is always disabled when alternate pad is selected).
	 */
	if (pad == UART_DEFAULT_PAD)
		enable_sleep(SLEEP_MASK_UART);
}

void uart_tx_flush(void)
{
	/* Wait for transmit FIFO empty and last byte is sent */
	while (NPCX_UART_TX_IN_XMIT())
		;
}

int uart_tx_ready(void)
{
	return NPCX_UART_TX_IS_READY();
}

int uart_tx_in_progress(void)
{
	return NPCX_UART_TX_IN_XMIT();
}

int uart_rx_available(void)
{
	int rx_available = NPCX_UART_RX_IS_AVAILABLE();

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
	/* Wait for space in transmit FIFO. */
	while (!uart_tx_ready())
		;

	NPCX_UTBUF = c;
}

int uart_read_char(void)
{
	return NPCX_URBUF;
}

void uart_clear_rx_fifo(int channel)
{
	int scratch __attribute__ ((unused));
	if (channel == 0) { /* suppose '0' is EC UART*/
		/*if '1' that mean have a RX data on the FIFO register*/
		while (NPCX_UART_RX_IS_AVAILABLE())
			scratch = NPCX_URBUF;
	}
}

/**
 * Interrupt handler for UART0
 */
void uart_ec_interrupt(void)
{
#ifdef CONFIG_UART_PAD_SWITCH
	if (pad == UART_ALTERNATE_PAD) {
		if (uart_rx_available()) {
			uint8_t c = uart_read_char();

			if (altpad_rx_pos < altpad_rx_len)
				altpad_rx_buf[altpad_rx_pos++] = c;
		}
		if (uart_tx_ready()) {
			if (altpad_tx_pos < altpad_tx_len)
				uart_write_char(altpad_tx_buf[altpad_tx_pos++]);
			else
				uart_tx_stop();
		}
		return;
	}
#endif

	/* Default pad. */
	/* Read input FIFO until empty, then fill output FIFO */
	uart_process_input();
	uart_process_output();
}
#ifdef NPCX_UART_FIFO_SUPPORT
DECLARE_IRQ(NPCX_IRQ_UART, uart_ec_interrupt, 4);
#else
DECLARE_IRQ(NPCX_IRQ_UART, uart_ec_interrupt, 1);
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
	NPCX_UFTCTL &= ~0xE0;
	NPCX_UFRCTL &= ~0xE0;
#else
	NPCX_UICTRL = 0x00;
#endif
	task_disable_irq(NPCX_IRQ_UART);

	/* Flush the last byte */
	uart_tx_flush();
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
	NPCX_UART_RX_INT_EN();

	/*
	 * If pad is switched while a byte is being received, the last byte may
	 * be corrupted, let's wait for ~1 byte (9/115200 = 78 us + margin),
	 * then flush the FIFO. See b/65526215.
	 */
	udelay(100);
	uart_clear_rx_fifo(0);

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

	if ((get_time().val - last_default_pad_rx_time.val)
			< block_alt_timeout_us)
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
	uart_tx_start();

	do {
		usleep(100);

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

#ifdef NPCX_UART_FIFO_SUPPORT
static void uart_set_fifo_mode(void)
{
	/* Enable the UART FIFO mode */
	SET_BIT(NPCX_UMDSL, NPCX_UMDSL_FIFO_MD);
	/* Disable all Tx interrupts */
	NPCX_UFTCTL &= ~((1 << NPCX_UFTCTL_TEMPTY_LVL_EN) |
					(1 << NPCX_UFTCTL_TEMPTY_EN) |
					(1 << NPCX_UFTCTL_NXIMPEN));
}

#endif

static void uart_config(void)
{
	/* Configure pins from GPIOs to CR_UART */
	gpio_config_module(MODULE_UART, 1);

	/* Enable MIWU IRQ of UART */
	task_enable_irq(NPCX_UART_MIWU_IRQ);

#ifdef CONFIG_LOW_POWER_IDLE
	/*
	 * Configure the UART wake-up event triggered from a falling edge
	 * on CR_SIN pin.
	 */
	SET_BIT(NPCX_WKEDG(1, NPCX_UART_WK_GROUP), NPCX_UART_WK_BIT);
#endif

	/*
	 * If apb2's clock is not 15MHz, we need to find the other optimized
	 * values of UPSR and UBAUD for baud rate 115200.
	 */
#if (NPCX_APB_CLOCK(2) != 15000000)
#error "Unsupported apb2 clock for UART!"
#endif

	/*
	 * Fix baud rate to 115200. If this value is modified, please also
	 * modify the delay in uart_set_pad and uart_reset_default_pad_panic.
	 */
	NPCX_UPSR = 0x38;
	NPCX_UBAUD = 0x01;

	/*
	 * 8-N-1, FIFO enabled.  Must be done after setting
	 * the divisor for the new divisor to take effect.
	 */
	NPCX_UFRS = 0x00;
#ifdef NPCX_UART_FIFO_SUPPORT
	uart_set_fifo_mode();
#endif
	NPCX_UART_RX_INT_EN();
}

void uart_init(void)
{
	uint32_t mask = 0;

	/*
	 * Enable UART0 in run, sleep, and deep sleep modes. Enable the Host
	 * UART in run and sleep modes.
	 */
	mask = 0x10; /* bit 4 */
	clock_enable_peripheral(CGC_OFFSET_UART, mask, CGC_MODE_ALL);

	/* Set pin-mask for UART */
	npcx_gpio2uart();

	/* Configure UARTs (identically) */
	uart_config();

	/*
	 * Enable interrupts for UART0 only. Host UART will have to wait
	 * until the LPC bus is initialized.
	 */
	uart_clear_rx_fifo(0);
	task_enable_irq(NPCX_IRQ_UART);

	init_done = 1;
}
