/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "pmu.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "uart_bitbang.h"
#include "uartn.h"

#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

#define BITBANG_DEBUG 0 /* Set to 1 to enable debug counters and logs. */

/* Support the "standard" baud rates. */
#define IS_BAUD_RATE_SUPPORTED(rate) \
	((rate == 1200) || (rate == 2400) || (rate == 4800) || (rate == 9600) \
	|| (rate == 19200) || (rate == 38400) || (rate == 57600) || \
	 (rate == 115200))

#define RX_BUF_SIZE 8
#define BUF_NEXT(idx) ((idx+1) % RX_BUF_SIZE)

#define TIMEUS_CLK_FREQ 24 /* units: MHz */

/* Flag indicating whether bit banging is enabled or not. */
static uint8_t bitbang_enabled;
/* Flag indicating bit banging is desired.  Allows async enable/disable. */
static uint8_t bitbang_wanted;

static int rx_buf[RX_BUF_SIZE];

/* Current bitbang context */
static int tx_pin;
static int rx_pin;
static uint32_t bit_period_ticks;
static uint8_t set_parity;

#if BITBANG_DEBUG
/* debug counters and log */
#define DISCARD_LOG 8
static int read_char_cnt;
static int rx_buff_inserted_cnt;
static int rx_buff_rx_char_cnt;
static int stop_bit_err_cnt;
static int parity_err_cnt;
static int parity_err_discard[DISCARD_LOG];
static int parity_discard_idx;
static int stop_bit_discard[DISCARD_LOG];
static int stop_bit_discard_idx;
#endif /* BITBANG_DEBUG */

static int is_uart_allowed(int uart)
{
	return uart == bitbang_config.uart;
}

int uart_bitbang_is_enabled(int uart)
{
	return (is_uart_allowed(uart) && bitbang_enabled);
}

int uart_bitbang_is_wanted(int uart)
{
	return (is_uart_allowed(uart) && bitbang_wanted);
}

int uart_bitbang_config(int uart, int baud_rate, int parity)
{
	/* Can't configure when enabled */
	if (bitbang_enabled)
		return EC_ERROR_BUSY;

	if (!is_uart_allowed(uart)) {
		CPRINTF("bit bang config not found for UART%d\n", uart);
		return EC_ERROR_INVAL;
	}

	/* Check desired properties. */
	if (!IS_BAUD_RATE_SUPPORTED(baud_rate)) {
		CPRINTF("Err: invalid baud rate (%d)\n", baud_rate);
		return EC_ERROR_INVAL;
	}
	bitbang_config.baud_rate = baud_rate;

	switch (parity) {
	case 0:
	case 1:
	case 2:
		break;

	default:
		CPRINTF("Err: invalid parity '%d'. (0:N, 1:O, 2:E)\n", parity);
		return EC_ERROR_INVAL;
	};
	bitbang_config.htp.parity = parity;

	return EC_SUCCESS;
}

int uart_bitbang_enable(int uart)
{
	/* We only want to bit bang 1 UART at a time */
	if (bitbang_enabled)
		return EC_ERROR_BUSY;

	/* UART TX must be disconnected first */
	if (uart_tx_is_connected(uart))
		return EC_ERROR_BUSY;

	if (!is_uart_allowed(uart)) {
		CPRINTS("bit bang config not found for UART%d", uart);
		return EC_ERROR_INVAL;
	}

	/* Select the GPIOs instead of the UART block */
	REG32(bitbang_config.tx_pinmux_reg) =
		bitbang_config.tx_pinmux_regval;
	gpio_set_flags(bitbang_config.tx_gpio, GPIO_OUT_HIGH);
	REG32(bitbang_config.rx_pinmux_reg) =
		bitbang_config.rx_pinmux_regval;
	gpio_set_flags(bitbang_config.rx_gpio, GPIO_INPUT);

	/*
	 * Ungate the microsecond timer so that we can use it.  This is needed
	 * for accurate framing if using faster baud rates.
	 */
	pmu_clock_en(PERIPH_TIMEUS);
	GR_TIMEUS_EN(0) = 0;
	GR_TIMEUS_MAXVAL(0) = 0xFFFFFFFF;
	GR_TIMEUS_EN(0) = 1;

	/* Save context information. */
	tx_pin = bitbang_config.tx_gpio;
	rx_pin = bitbang_config.rx_gpio;
	bit_period_ticks = TIMEUS_CLK_FREQ *
		((1 * SECOND) / bitbang_config.baud_rate);
	set_parity = bitbang_config.htp.parity;

	/* Register the function pointers. */
	uartn_funcs[uart]._rx_available = _uart_bitbang_rx_available;
	uartn_funcs[uart]._write_char = _uart_bitbang_write_char;
	uartn_funcs[uart]._read_char = _uart_bitbang_read_char;

	bitbang_enabled = 1;
	gpio_enable_interrupt(bitbang_config.rx_gpio);
	CPRINTS("Bit bang enabled");
	return EC_SUCCESS;
}

int uart_bitbang_disable(int uart)
{
	if (!uart_bitbang_is_enabled(uart))
		return EC_SUCCESS;

	/*
	 * This is safe because if the UART was not specified in the config, we
	 * would have already returned.
	 */
	bitbang_enabled = 0;
	gpio_reset(bitbang_config.tx_gpio);
	gpio_reset(bitbang_config.rx_gpio);

	/* Unregister the function pointers. */
	uartn_funcs[uart]._rx_available = _uartn_rx_available;
	uartn_funcs[uart]._write_char = _uartn_write_char;
	uartn_funcs[uart]._read_char = _uartn_read_char;

	/* Gate the microsecond timer since we're done with it. */
	pmu_clock_dis(PERIPH_TIMEUS);

	/* Don't need to watch RX */
	gpio_disable_interrupt(bitbang_config.rx_gpio);
	CPRINTS("Bit bang disabled");
	return EC_SUCCESS;
}

static void wait_ticks(uint32_t ticks)
{
	uint32_t t0 = GR_TIMEUS_CUR_MAJOR(0);

	while ((GR_TIMEUS_CUR_MAJOR(0) - t0) < ticks)
		;
}

void uart_bitbang_write_char(int uart, char c)
{
	int val;
	int ones;
	int i;

	if (!uart_bitbang_is_enabled(uart))
		return;

	interrupt_disable();

	/* Start bit. */
	gpio_set_level(tx_pin, 0);
	wait_ticks(bit_period_ticks);

	/* 8 data bits. */
	ones = 0;
	for (i = 0; i < 8; i++) {
		val = (c & (1 << i));
		/* Count 1's in order to handle parity bit. */
		if (val)
			ones++;
		gpio_set_level(tx_pin, val);
		wait_ticks(bit_period_ticks);
	}

	/* Optional parity. */
	switch (set_parity) {
	case 1: /* odd parity */
		if (ones & 0x1)
			gpio_set_level(tx_pin, 0);
		else
			gpio_set_level(tx_pin, 1);
		wait_ticks(bit_period_ticks);
		break;

	case 2: /* even parity */
		if (ones & 0x1)
			gpio_set_level(tx_pin, 1);
		else
			gpio_set_level(tx_pin, 0);
		wait_ticks(bit_period_ticks);
		break;

	case 0: /* no parity */
	default:
		break;
	};

	/* 1 stop bit. */
	gpio_set_level(tx_pin, 1);
	wait_ticks(bit_period_ticks);
	interrupt_enable();
}

int uart_bitbang_receive_char(int uart)
{
	uint8_t rx_char;
	int i;
	int rv;
	int ones;
	int parity_bit;
	int stop_bit;
	uint8_t head;
	uint8_t tail;

	/* Disable interrupts so that we aren't interrupted. */
	interrupt_disable();
#if BITBANG_DEBUG
	rx_buff_rx_char_cnt++;
#endif /* BITBANG_DEBUG */
	rv = EC_SUCCESS;

	rx_char = 0;

	/* Wait 1 bit period for the start bit. */
	wait_ticks(bit_period_ticks);

	/* 8 data bits. */
	ones = 0;
	for (i = 0; i < 8; i++) {
		if (gpio_get_level(rx_pin)) {
			ones++;
			rx_char |= (1 << i);
		}
		wait_ticks(bit_period_ticks);
	}

	/* optional parity or stop bit. */
	parity_bit = gpio_get_level(rx_pin);
	if (set_parity) {
		wait_ticks(bit_period_ticks);
		stop_bit = gpio_get_level(rx_pin);
	} else {
		/* If there's no parity, that _was_ the stop bit. */
		stop_bit = parity_bit;
	}

	/* Check the parity if necessary. */
	switch (set_parity) {
	case 2: /* even parity */
		if (ones & 0x1)
			rv = parity_bit ? EC_SUCCESS : EC_ERROR_CRC;
		else
			rv = parity_bit ? EC_ERROR_CRC : EC_SUCCESS;
		break;

	case 1: /* odd parity */
		if (ones & 0x1)
			rv = parity_bit ? EC_ERROR_CRC : EC_SUCCESS;
		else
			rv = parity_bit ? EC_SUCCESS : EC_ERROR_CRC;
		break;

	case 0:
	default:
		break;
	}

#if BITBANG_DEBUG
	if (rv != EC_SUCCESS) {
		parity_err_cnt++;
		parity_err_discard[parity_discard_idx] = rx_char;
		parity_discard_idx = (parity_discard_idx + 1) % DISCARD_LOG;
	}
#endif /* BITBANG_DEBUG */

	/* Check that the stop bit is valid. */
	if (stop_bit != 1) {
		rv = EC_ERROR_CRC;
#if BITBANG_DEBUG
		stop_bit_err_cnt++;
		stop_bit_discard[stop_bit_discard_idx] = rx_char;
		stop_bit_discard_idx = (stop_bit_discard_idx + 1) % DISCARD_LOG;
#endif /* BITBANG_DEBUG */
	}

	if (rv != EC_SUCCESS) {
		interrupt_enable();
		return rv;
	}

	/* Place the received char in the RX buffer. */
	head = bitbang_config.htp.head;
	tail = bitbang_config.htp.tail;
	if (BUF_NEXT(tail) != head) {
		rx_buf[tail] = rx_char;
		bitbang_config.htp.tail = BUF_NEXT(tail);
#if BITBANG_DEBUG
		rx_buff_inserted_cnt++;
#endif /* BITBANG_DEBUG */
	}

	interrupt_enable();
	return EC_SUCCESS;
}

int uart_bitbang_read_char(int uart)
{
	int c;
	uint8_t head;

	if (!is_uart_allowed(uart))
		return 0;

	head = bitbang_config.htp.head;
	c = rx_buf[head];

	if (head != bitbang_config.htp.tail)
		bitbang_config.htp.head = BUF_NEXT(head);

#if BITBANG_DEBUG
	read_char_cnt++;
#endif /* BITBANG_DEBUG */
	return c;
}

int uart_bitbang_is_char_available(int uart)
{
	if (!is_uart_allowed(uart))
		return 0;

	return bitbang_config.htp.head != bitbang_config.htp.tail;
}

#if BITBANG_DEBUG
static int write_test_pattern(int uart, int pattern_idx)
{
	if (!uart_bitbang_is_enabled(uart)) {
		ccprintf("bit banging mode not enabled for UART%d\n", uart);
		return EC_ERROR_INVAL;
	}

	switch (pattern_idx) {
	case 0:
		uartn_write_char(uart, 'a');
		uartn_write_char(uart, 'b');
		uartn_write_char(uart, 'c');
		uartn_write_char(uart, '\n');
		ccprintf("wrote: 'abc\\n'\n");
		break;

	case 1:
		uartn_write_char(uart, 0xAA);
		uartn_write_char(uart, 0xCC);
		uartn_write_char(uart, 0x55);
		ccprintf("wrote: '0xAA 0xCC 0x55'\n");
		break;

	default:
		ccprintf("unknown test pattern\n");
		return EC_ERROR_INVAL;
	};

	return EC_SUCCESS;
}
#endif /* BITBANG_DEBUG */

static int command_bitbang(int argc, char **argv)
{
	int uart;
	int baud_rate;
	int parity;
	int rv;

	if (argc > 1) {
		uart = atoi(argv[1]);
		if (argc == 3) {
			if (!strcasecmp("disable", argv[2])) {
				bitbang_wanted = 0;
				ccd_update_state();
				return EC_SUCCESS;
			}
			return EC_ERROR_PARAM2;
		}

		if (argc == 4) {
#if BITBANG_DEBUG
			if (!strncasecmp("test", argv[2], 4))
				return write_test_pattern(uart, atoi(argv[3]));
#endif /* BITBANG_DEBUG */

			baud_rate = atoi(argv[2]);
			if (!strcasecmp("odd", argv[3]))
				parity = 1;
			else if (!strcasecmp("even", argv[3]))
				parity = 2;
			else if (!strcasecmp("none", argv[3]))
				parity = 0;
			else
				return EC_ERROR_PARAM3;

			rv = uart_bitbang_config(uart, baud_rate, parity);
			if (rv)
				return rv;

			if (servo_is_connected())
				ccprintf("Bit banging superseded by servo\n");

			bitbang_wanted = 1;
			ccd_update_state();
			return EC_SUCCESS;
		}

		return EC_ERROR_PARAM_COUNT;
	}

	if (!uart_bitbang_is_enabled(bitbang_config.uart)) {
		ccprintf("bit banging mode disabled.\n");
	} else {
		ccprintf("baud rate - parity\n");
		ccprintf("  %6d    ", bitbang_config.baud_rate);
		switch (bitbang_config.htp.parity) {
		case 1:
			ccprintf("odd\n");
			break;

		case 2:
			ccprintf("even\n");
			break;

		case 0:
		default:
			ccprintf("none\n");
		break;
		};
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(bitbang, command_bitbang,
			"<uart> <baud_rate> <odd,even,none> | <uart> disable "
#if BITBANG_DEBUG
			"| <uart> test <0, 1>"
#endif /* BITBANG_DEBUG */
			, "set bit bang mode");

#if BITBANG_DEBUG
static int command_bitbang_dump_stats(int argc, char **argv)
{
	int i;

	if (argc == 2) {
		/* Clear the counters. */
		if (!strncasecmp(argv[1], "clear", 5)) {
			parity_err_cnt = 0;
			stop_bit_err_cnt = 0;
			rx_buff_rx_char_cnt = 0;
			read_char_cnt = 0;
			rx_buff_inserted_cnt = 0;
			return EC_SUCCESS;
		}
		return EC_ERROR_PARAM1;
	}

	ccprintf("Errors:\n");
	ccprintf("%d Parity Errors\n", parity_err_cnt);
	ccprintf("%d Stop Bit Errors\n", stop_bit_err_cnt);
	ccprintf("Buffer info\n");
	ccprintf("%d received\n", rx_buff_rx_char_cnt);
	ccprintf("%d chars inserted\n", rx_buff_inserted_cnt);
	ccprintf("%d chars read\n", read_char_cnt);
	ccprintf("Contents\n");
	ccprintf("[");
	for (i = 0; i < RX_BUF_SIZE; i++)
		ccprintf(" %02x ", rx_buf[i] & 0xFF);
	ccprintf("]\n");
	ccprintf("head: %d\ntail: %d\n",
		 bitbang_config.htp.head,
		 bitbang_config.htp.tail);
	ccprintf("Discards\nparity: ");
	ccprintf("[");
	for (i = 0; i < DISCARD_LOG; i++)
		ccprintf(" %02x ", parity_err_discard[i] & 0xFF);
	ccprintf("]\n");
	ccprintf("idx: %d\n", parity_discard_idx);
	ccprintf("stop bit: ");
	ccprintf("[");
	for (i = 0; i < DISCARD_LOG; i++)
		ccprintf(" %02x ", stop_bit_discard[i] & 0xFF);
	ccprintf("]\n");
	ccprintf("idx: %d\n", stop_bit_discard_idx);
	cflush();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(bbstats, command_bitbang_dump_stats,
			"",
			"dumps bitbang stats");
#endif /* BITBANG_DEBUG */
