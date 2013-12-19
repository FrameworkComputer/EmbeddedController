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

static int command_adc(int argc, char **argv)
{
	int i, v;
	int data[ADC_CH_COUNT];

	/* If a channel is specified, read only that one */
	if (argc == 2) {
		i = find_adc_channel_by_name(argv[1]);
		if (i == ADC_CH_COUNT)
			return EC_ERROR_PARAM1;
		v = adc_read_channel(i);
		if (v == ADC_READ_ERROR)
			return EC_ERROR_UNKNOWN;
		ccprintf("  %s = %d\n", adc_channels[i].name, v);
		return EC_SUCCESS;
	} else {
		/* Otherwise print them all */
		if (adc_read_all_channels(data))
			return EC_ERROR_UNKNOWN;
		for (i = 0; i < ADC_CH_COUNT; ++i)
			ccprintf("  %s = %d\n", adc_channels[i].name, data[i]);
		return EC_SUCCESS;
	}
}
DECLARE_CONSOLE_COMMAND(adc, command_adc,
			"[name]",
			"Print ADC channel(s)",
			NULL);
