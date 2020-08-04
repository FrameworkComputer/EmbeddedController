/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PECI interface for Chrome EC */

#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "peci.h"
#include "registers.h"
#include "temp_sensor.h"
#include "util.h"

/* Initial PECI baud rate */
#define PECI_BAUD_RATE 100000

/* Polling interval for PECI, in ms */
#define PECI_POLL_INTERVAL_MS 250

/*
 * Internal and external path delays, in ns.  The external delay is a
 * best-guess measurement, but we're fairly tolerant of a bad guess because
 * PECI_BAUD_RATE is slow compared to PECI's actual maximum baud rate.
 */
#define PECI_TD_FET_NS 60
#define PECI_TD_INT_NS 80

/* Number of controller retries. Should be between 0 and 7. */
#define PECI_RETRY_COUNT 4

/* Timing negotiation error bypass. 1 = on. 0 = off. */
#define PECI_ERROR_BYPASS 1

#define TEMP_AVG_LENGTH 4  /* Should be power of 2 */
static int temp_vals[TEMP_AVG_LENGTH];
static int temp_idx;

int peci_get_cpu_temp(void)
{
	int v = LM4_PECI_M0D0 & 0xffff;

	if (v >= 0x8000 && v <= 0x8fff)
		return -1;

	return v >> 6;
}

int peci_temp_sensor_get_val(int idx, int *temp_ptr)
{
	int sum = 0;
	int success_cnt = 0;
	int i;

	if (!chipset_in_state(CHIPSET_STATE_ON))
		return EC_ERROR_NOT_POWERED;

	for (i = 0; i < TEMP_AVG_LENGTH; ++i) {
		if (temp_vals[i] >= 0) {
			success_cnt++;
			sum += temp_vals[i];
		}
	}

	/*
	 * Require at least two valid samples. When the AP transitions into S0,
	 * it is possible, depending on the timing of the PECI sample, to read
	 * an invalid temperature. This is very rare, but when it does happen
	 * the temperature returned is CONFIG_PECI_TJMAX. Requiring two valid
	 * samples here assures us that one bad maximum temperature reading
	 * when entering S0 won't cause us to trigger an over temperature.
	 */
	if (success_cnt < 2)
		return EC_ERROR_UNKNOWN;

	*temp_ptr = sum / success_cnt;
	return EC_SUCCESS;
}

static void peci_temp_sensor_poll(void)
{
	temp_vals[temp_idx] = peci_get_cpu_temp();
	temp_idx = (temp_idx + 1) & (TEMP_AVG_LENGTH - 1);
}
DECLARE_HOOK(HOOK_TICK, peci_temp_sensor_poll, HOOK_PRIO_TEMP_SENSOR);

static void peci_freq_changed(void)
{
	int freq = clock_get_freq();
	int baud;

	/* Disable polling while reconfiguring */
	LM4_PECI_CTL = 0;

	/*
	 * Calculate baud setting from desired rate, compensating for internal
	 * and external delays.
	 */
	baud = freq / (4 * PECI_BAUD_RATE) - 2;
	baud -= (freq / 1000000) * (PECI_TD_FET_NS + PECI_TD_INT_NS) / 1000;

	/* Set baud rate and polling rate */
	LM4_PECI_DIV = (baud << 16) |
		(PECI_POLL_INTERVAL_MS * (freq / 1000 / 4096));

	/* Set up temperature monitoring to report in degrees K */
	LM4_PECI_CTL = ((CONFIG_PECI_TJMAX + 273) << 22) | 0x0001 |
		       (PECI_RETRY_COUNT << 12) |
		       (PECI_ERROR_BYPASS << 11);
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, peci_freq_changed, HOOK_PRIO_DEFAULT);

static void peci_init(void)
{
	int i;

	/* Enable the PECI module in run and sleep modes. */
	clock_enable_peripheral(CGC_OFFSET_PECI, 0x1,
			CGC_MODE_RUN | CGC_MODE_SLEEP);

	/* Configure GPIOs */
	gpio_config_module(MODULE_PECI, 1);

	/* Set initial clock frequency */
	peci_freq_changed();

	/* Initialize temperature reading buffer to a valid value. */
	for (i = 0; i < TEMP_AVG_LENGTH; ++i)
		temp_vals[i] = 300; /* 27 C */
}
DECLARE_HOOK(HOOK_INIT, peci_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Console commands */

static int command_peci_temp(int argc, char **argv)
{
	int t = peci_get_cpu_temp();
	if (t == -1) {
		ccprintf("PECI error 0x%04x\n", LM4_PECI_M0D0 & 0xffff);
		return EC_ERROR_UNKNOWN;
	}
	ccprintf("CPU temp = %d K = %d C\n", t, K_TO_C(t));
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pecitemp, command_peci_temp,
			NULL,
			"Print CPU temperature");
