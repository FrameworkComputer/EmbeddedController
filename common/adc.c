/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ADC module for Chrome EC */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "util.h"

/* 'adc' console command is not supported in continuous mode */
#ifndef CONFIG_ADC_PROFILE_FAST_CONTINUOUS
static enum adc_channel find_adc_channel_by_name(const char *name)
{
	const struct adc_t *ch = adc_channels;
	int i;

	if (!name || !*name)
		return ADC_CH_COUNT;

	for (i = 0; i < ADC_CH_COUNT; i++, ch++) {
		if (!strcasecmp(name, ch->name))
			return i;
	}

	return ADC_CH_COUNT;
}

static int print_one_adc(int channel)
{
	int v;

	v = adc_read_channel(channel);
	if (v == ADC_READ_ERROR)
		return EC_ERROR_UNKNOWN;
	ccprintf("  %s = %d\n", adc_channels[channel].name, v);
	return EC_SUCCESS;
}

static int command_adc(int argc, char **argv)
{
	int i, ret;

	/* If a channel is specified, read only that one */
	if (argc == 2) {
		i = find_adc_channel_by_name(argv[1]);
		if (i == ADC_CH_COUNT)
			return EC_ERROR_PARAM1;
		return print_one_adc(i);
	} else {
		/* Otherwise print them all */
		for (i = 0; i < ADC_CH_COUNT; ++i) {
			ret = print_one_adc(i);
			if (ret)
				return ret;
		}
		return EC_SUCCESS;
	}
}
DECLARE_CONSOLE_COMMAND(adc, command_adc,
			"[name]",
			"Print ADC channel(s)");
#endif
