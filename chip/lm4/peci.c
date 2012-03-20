/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PECI interface for Chrome EC */

#include "board.h"
#include "console.h"
#include "gpio.h"
#include "peci.h"
#include "registers.h"
#include "temp_sensor.h"
#include "uart.h"
#include "util.h"

/* Max junction temperature for processor in degrees C */
/* TODO: read TjMax from processor via PECI */
#define PECI_TJMAX 105

/* Initial PECI baud rate */
#define PECI_BAUD_RATE 150000

/* Polling interval for PECI, in ms */
#define PECI_POLL_INTERVAL_MS 200

/* Internal and external path delays, in ns */
#define PECI_TD_FET_NS 25  /* Guess; TODO: what is real delay */
#define PECI_TD_INT_NS 80

static int last_temp_val;

/* Configures the GPIOs for the PECI module. */
static void configure_gpios(void)
{
	/* PJ6 alternate function 1 = PECI Tx */
	gpio_set_alternate_function(LM4_GPIO_J, 0x40, 1);

	/* PJ7 analog input = PECI Rx (comparator) */
	LM4_GPIO_DEN(LM4_GPIO_J) &= ~0x80;
}


int peci_get_cpu_temp(void)
{
	int v = LM4_PECI_M0D0 & 0xffff;

	if (v >= 0x8000 && v <= 0x8fff)
		return -1;

	return v >> 6;
}

int peci_temp_sensor_poll(void)
{
	last_temp_val = peci_get_cpu_temp();

	if (last_temp_val > 0)
		return EC_SUCCESS;
	else
		return EC_ERROR_UNKNOWN;
}

int peci_temp_sensor_get_val(int idx)
{
	return last_temp_val;
}

/*****************************************************************************/
/* Console commands */

static int command_peci_temp(int argc, char **argv)
{
	int t = peci_get_cpu_temp();
	if (t == -1) {
		uart_puts("Error reading CPU temperature via PECI\n");
		uart_printf("Error code = 0x%04x\n", LM4_PECI_M0D0 & 0xffff);
		return EC_ERROR_UNKNOWN;
	}
	uart_printf("Current CPU temperature = %d K = %d C\n", t, t - 273);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pecitemp, command_peci_temp);

/*****************************************************************************/
/* Initialization */

int peci_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));
	int baud;

	/* Enable the PECI module and delay a few clocks */
	LM4_SYSTEM_RCGCPECI = 1;
	scratch = LM4_SYSTEM_RCGCPECI;

	/* Configure GPIOs */
	configure_gpios();

	/* Disable polling while reconfiguring */
	LM4_PECI_CTL = 0;

	/* Calculate baud setting from desired rate, compensating for internal
	 * and external delays. */
	baud = CPU_CLOCK / (4 * PECI_BAUD_RATE) - 2;
	baud -= (CPU_CLOCK / 1000000) * (PECI_TD_FET_NS + PECI_TD_INT_NS)
		/ 1000;

	/* Set baud rate and polling rate */
	LM4_PECI_DIV = (baud << 16) |
		(PECI_POLL_INTERVAL_MS * (CPU_CLOCK / 1000 / 4096));

	/* Set up temperature monitoring to report in degrees K */
	LM4_PECI_CTL = ((PECI_TJMAX + 273) << 22) | 0x2001;

	return EC_SUCCESS;
}
