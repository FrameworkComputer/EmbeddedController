/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IT83xx DAC module for Chrome EC */

#include "console.h"
#include "dac_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "util.h"

/* DAC module enable */
void dac_enable_channel(enum chip_dac_channel ch)
{
	IT83XX_DAC_DACPDREG &= ~IT83XX_DAC_POWDN(ch);
}

/* DAC module disable */
void dac_disable_channel(enum chip_dac_channel ch)
{
	IT83XX_DAC_DACPDREG |= IT83XX_DAC_POWDN(ch);
}

/* Set DAC output voltage */
void dac_set_output_voltage(enum chip_dac_channel ch, int mv)
{
	IT83XX_DAC_DACDAT(ch) = mv * DAC_RAW_DATA / DAC_AVCC;
}

/* Get DAC output voltage */
int dac_get_output_voltage(enum chip_dac_channel ch)
{
	return IT83XX_DAC_DACDAT(ch) * DAC_AVCC / DAC_RAW_DATA;
}

/* DAC module Initialization */
static void dac_init(void)
{
	/* Configure GPIOs */
	gpio_config_module(MODULE_DAC, 1);
}
DECLARE_HOOK(HOOK_INIT, dac_init, HOOK_PRIO_INIT_DAC);

static int command_dac(int argc, char **argv)
{
	char *e;
	int ch, mv, rv;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	ch = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;
	if (ch < 2 || ch > 5) {
		ccprintf("ch%d is not supported\n", ch);
		return EC_ERROR_PARAM1;
	}

	if (argc == 2) {
		if (!(IT83XX_DAC_DACPDREG & IT83XX_DAC_POWDN(ch))) {
			/* Get DAC output voltage */
			rv = dac_get_output_voltage(ch);
			ccprintf("DAC ch%d VOLT=%dmV\n", ch, rv);
		} else
			ccprintf("The DAC ch%d is powered down.\n", ch);
	} else {
		/*
		 * DAC data register raw data
		 * 0 ~ 0xFF(8-bit) = voltage 0 ~ 3300mV
		 */
		mv = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		if (mv) {
			/* Set DAC output voltage */
			dac_set_output_voltage(ch, mv);
			dac_enable_channel(ch);
		} else
			dac_disable_channel(ch);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dac, command_dac,
			"[ch2-5] [0-3300mV]",
			"Enable or disable(0mV) DAC output voltage.");
