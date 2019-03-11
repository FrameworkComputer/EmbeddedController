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

#define TIMEUS_CLK_FREQ 24 /* units: MHz */
#define RX_BUF_SIZE	257

/* Flag indicating whether bit banging is enabled or not. */
static uint8_t bitbang_enabled;
/* Flag indicating bit banging is desired.  Allows async enable/disable. */
static uint8_t bitbang_wanted;

/* Current bitbang context */
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

enum parity_type {
	PARITY_TYPE_NONE = 0,
	PARITY_TYPE_ODD = 1,
	PARITY_TYPE_EVEN = 2,
	PARITY_TYPE_MAX,
};

char *parity_type_name[PARITY_TYPE_MAX] = {
	"none",
	"odd",
	"even",
};

static char *feature_name = "Bit bang";

int uart_bitbang_is_enabled(void)
{
	return bitbang_enabled;
}

int uart_bitbang_is_wanted(void)
{
	return bitbang_wanted;
}

static int uart_bitbang_config(int baud_rate, uint8_t parity)
{
	/* Can't configure when enabled */
	if (bitbang_enabled)
		return EC_ERROR_BUSY;

	/* Check desired properties. */
	if (!IS_BAUD_RATE_SUPPORTED(baud_rate)) {
		CPRINTF("Err: invalid baud rate (%d)\n", baud_rate);
		return EC_ERROR_INVAL;
	}
	bitbang_config.baud_rate = baud_rate;

	if (parity >= PARITY_TYPE_MAX) {
		CPRINTF("Err: invalid parity '%d'. (0:N, 1:O, 2:E)\n", parity);
		return EC_ERROR_INVAL;
	}
	bitbang_config.parity = parity;

	return EC_SUCCESS;
}

int uart_bitbang_enable(void)
{
	if (bitbang_enabled)
		return EC_SUCCESS;

	/* UART TX must be disconnected first */
	if (uart_tx_is_connected(bitbang_config.uart))
		return EC_ERROR_BUSY;

	/* Set this early to avoid interfering with CCD state machine. */
	bitbang_enabled = 1;

	/*
	 * Disable aggregate interrupts from GPIOs, otherwise
	 * _gpio0_interrupt() gets invoked along with the pin specific
	 * interrupts.
	 */
	task_disable_irq(GC_IRQNUM_GPIO0_GPIOCOMBINT);
	task_disable_irq(GC_IRQNUM_GPIO1_GPIOCOMBINT);

	/* Select the GPIOs instead of the UART block */
	REG32(bitbang_config.tx_pinmux_reg) = bitbang_config.tx_pinmux_regval;
	gpio_set_flags(bitbang_config.tx_gpio, GPIO_OUT_HIGH);
	REG32(bitbang_config.rx_pinmux_reg) = bitbang_config.rx_pinmux_regval;
	gpio_set_flags(bitbang_config.rx_gpio, GPIO_INPUT);

	/*
	 * Ungate the microsecond timer so that we can use it.  This is needed
	 * for accurate framing if using faster baud rates.
	 */
	pmu_clock_en(PERIPH_TIMEUS);
	GR_TIMEUS_EN(0) = 0;
	GR_TIMEUS_MAXVAL(0) = 0xFFFFFFFF;
	GR_TIMEUS_CUR_MAJOR(0) = 0; /* Prevent timer counter overflows. */
	GR_TIMEUS_EN(0) = 1;

	/* Save context information. */
	bit_period_ticks = ((uint64_t)TIMEUS_CLK_FREQ * SECOND) /
		bitbang_config.baud_rate;
	set_parity = bitbang_config.parity;

	uartn_disable_interrupt(bitbang_config.uart);
	task_enable_irq(bitbang_config.rx_irq);
	gpio_enable_interrupt(bitbang_config.rx_gpio);

	CPRINTS("%s enabled", feature_name);

	return EC_SUCCESS;
}

int uart_bitbang_disable(void)
{
	if (!uart_bitbang_is_enabled())
		return EC_SUCCESS;

	gpio_reset(bitbang_config.tx_gpio);
	gpio_reset(bitbang_config.rx_gpio);

	/* Gate the microsecond timer since we're done with it. */
	pmu_clock_dis(PERIPH_TIMEUS);

	/* Don't need to watch RX */
	gpio_disable_interrupt(bitbang_config.rx_gpio);
	task_disable_irq(bitbang_config.rx_irq);
	uartn_enable_interrupt(bitbang_config.uart);

	/* Restore aggregate GPIO interrupts. */
	task_enable_irq(GC_IRQNUM_GPIO0_GPIOCOMBINT);
	task_enable_irq(GC_IRQNUM_GPIO1_GPIOCOMBINT);

	bitbang_enabled = 0;

	CPRINTS("%s disabled", feature_name);
	return EC_SUCCESS;
}

/*
 * Function waiting for completion of the current tick should be re-entrant -
 * it is not likely to happen, but is possible that the RX interrupt gets
 * asserted while the last period of the TX is still counting, because the
 * last TX period is counting with interrupts enabled.
 */
static void wait_ticks(uint32_t *next_tick)
{
	uint32_t nt = *next_tick;

	while (GR_TIMEUS_CUR_MAJOR(0) < nt)
		;

	*next_tick += bit_period_ticks;
}

static uint32_t get_next_tick(uint32_t delta)
{
	return GR_TIMEUS_CUR_MAJOR(0) + delta;
}

static void uart_bitbang_write_char(char c)
{
	int val;
	int ones;
	int i;
	uint32_t next_tick;

	interrupt_disable();

	next_tick = get_next_tick(bit_period_ticks);

	/* Start bit. */
	gpio_set_level(bitbang_config.tx_gpio, 0);
	wait_ticks(&next_tick);

	/* 8 data bits. */
	ones = 0;
	for (i = 0; i < 8; i++) {
		val = !!(c & BIT(i));
		gpio_set_level(bitbang_config.tx_gpio, val);

		/* Count 1's in order to handle parity bit. */
		ones += val;
		wait_ticks(&next_tick);
	}

	/* Optional parity. */
	if (set_parity != PARITY_TYPE_NONE) {
		gpio_set_level(bitbang_config.tx_gpio,
			       (set_parity == PARITY_TYPE_ODD) ^ (ones & 1));
		wait_ticks(&next_tick);
	}

	/* 1 stop bit. */
	gpio_set_level(bitbang_config.tx_gpio, 1);

	/*
	 * Re-enable interrupts early: this could be the last byte and the
	 * response could come very soon, we don't want to waste time enabling
	 * interrupts AFTER stop bit is completed.
	 */
	interrupt_enable();
	wait_ticks(&next_tick);
}

void uart_bitbang_drain_tx_queue(struct queue const *q)
{
	uint8_t c;

	while (queue_count(q)) {
		QUEUE_REMOVE_UNITS(q, &c, 1);
		uart_bitbang_write_char(c);
	}
}

static int uart_bitbang_receive_char(uint8_t *rxed_char, uint32_t *next_tick)
{
	uint8_t rx_char;
	int i;
	int ones;
	int parity_bit;
	int stop_bit;

#if BITBANG_DEBUG
	rx_buff_rx_char_cnt++;
#endif /* BITBANG_DEBUG */

	rx_char = 0;
	ones = 0;

	/* Wait 1 bit period for the start bit. */
	wait_ticks(next_tick);

	for (i = 0; i < 8; i++) {
		if (gpio_get_level(bitbang_config.rx_gpio)) {
			ones++;
			rx_char |= BIT(i);
		}
		wait_ticks(next_tick);
	}

	/* optional parity or stop bit. */
	parity_bit = gpio_get_level(bitbang_config.rx_gpio);

	if (set_parity) {
		wait_ticks(next_tick);
		stop_bit = gpio_get_level(bitbang_config.rx_gpio);

		if ((set_parity == PARITY_TYPE_ODD) !=
			((ones + parity_bit) & 1)) {
#if BITBANG_DEBUG
			parity_err_cnt++;
			parity_err_discard[parity_discard_idx] = rx_char;
			parity_discard_idx = (parity_discard_idx + 1) %
				DISCARD_LOG;
#endif /* BITBANG_DEBUG */
			return EC_ERROR_CRC;
		}
	} else {
		/* If there's no parity, that _was_ the stop bit. */
		stop_bit = parity_bit;
	}


	/* Check that the stop bit is valid. */
	if (stop_bit != 1) {
#if BITBANG_DEBUG
		stop_bit_err_cnt++;
		stop_bit_discard[stop_bit_discard_idx] = rx_char;
		stop_bit_discard_idx = (stop_bit_discard_idx + 1) % DISCARD_LOG;
#endif /* BITBANG_DEBUG */
		return EC_ERROR_CRC;
	}

	/* Place the received char in the RX buffer. */
#if BITBANG_DEBUG
	rx_buff_inserted_cnt++;
#endif /* BITBANG_DEBUG */

	*rxed_char = rx_char;

	return EC_SUCCESS;
}

void __attribute__((used)) uart_bitbang_irq(void)
{
	uint8_t rx_buffer[RX_BUF_SIZE];
	size_t i = 0;
	uint32_t next_tick;

	/* Empirically chosen IRQ latency compensation. */
	next_tick = get_next_tick(bit_period_ticks  - 40);
	do {
		uint32_t max_time;
		int rv;
new_char:
		rv = uart_bitbang_receive_char(rx_buffer + i, &next_tick);
		gpio_clear_pending_interrupt(bitbang_config.rx_gpio);

		if (rv != EC_SUCCESS)
			break;

		if (++i == RX_BUF_SIZE)
			break;
		/*
		 * For the duration of one byte wait for another byte from the
		 * EC.
		 */
		max_time = GR_TIMEUS_CUR_MAJOR(0) + bit_period_ticks * 10;
		while (GR_TIMEUS_CUR_MAJOR(0) < max_time) {
			if (!gpio_get_level(bitbang_config.rx_gpio)) {
				next_tick = get_next_tick(bit_period_ticks);
				goto new_char;
			}
		}

	} while (0);

	QUEUE_ADD_UNITS(bitbang_config.uart_in, rx_buffer, i);
}

#if BITBANG_DEBUG
static int write_test_pattern(int pattern_idx)
{
	switch (pattern_idx) {
	case 0:
		uart_bitbang_write_char(uart, 'a');
		uart_bitbang_write_char(uart, 'b');
		uart_bitbang_write_char(uart, 'c');
		uart_bitbang_write_char(uart, '\n');
		ccprintf("wrote: 'abc\\n'\n");
		break;

	case 1:
		uart_bitbang_write_char(uart, 0xAA);
		uart_bitbang_write_char(uart, 0xCC);
		uart_bitbang_write_char(uart, 0x55);
		ccprintf("wrote: '0xAA 0xCC 0x55'\n");
		break;

	default:
		ccprintf("unknown test pattern\n");
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}
#endif /* BITBANG_DEBUG */

static int command_bitbang(int argc, char **argv)
{
	int baud_rate;
	uint32_t parity;
	int rv;

	if (argc > 1) {
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
				return write_test_pattern(atoi(argv[3]));
#endif /* BITBANG_DEBUG */

			baud_rate = atoi(argv[2]);
			for (parity = PARITY_TYPE_NONE;
			     parity < PARITY_TYPE_MAX;
			     ++parity)
				if (!strcasecmp(parity_type_name[parity],
						argv[3]))
					break;
			if (parity >= PARITY_TYPE_MAX)
				return EC_ERROR_PARAM3;

			rv = uart_bitbang_config(baud_rate, parity);
			if (rv)
				return rv;

			if (servo_is_connected())
				ccprintf("%sing superseded by servo\n",
					feature_name);

			bitbang_wanted = 1;
			ccd_update_state();
			return EC_SUCCESS;
		}

		return EC_ERROR_PARAM_COUNT;
	}

	if (!uart_bitbang_is_enabled()) {
		ccprintf("%s mode disabled.\n", feature_name);
	} else {
		ccprintf("baud rate - parity\n");
		ccprintf("  %6d    ", bitbang_config.baud_rate);
		ccprintf("%s\n", parity_type_name[
			(bitbang_config.parity < PARITY_TYPE_MAX) ?
			bitbang_config.parity : PARITY_TYPE_NONE]);
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
