/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Special AC Adapter logic for Falco */

#include "adc.h"
#include "common.h"
#include "console.h"
#include "util.h"

enum adapter_type {
	ADAPTER_UNKNOWN,
	ADAPTER_45W,
	ADAPTER_65W,
	ADAPTER_90W,
};

static const char * const adapter_str[] = {
	"unknown",
	"45W",
	"65W",
	"90W"
};

static enum adapter_type identify_adapter(void)
{
	int mv;
	mv = adc_read_channel(ADC_AC_ADAPTER_ID_VOLTAGE);
	if (mv >= 434 && mv <= 554)
		return ADAPTER_45W;
	if (mv >= 561 && mv <= 717)
		return ADAPTER_65W;
	if (mv >= 725 && mv <= 925)
		return ADAPTER_90W;

	return ADAPTER_UNKNOWN;
}

static int command_adapter(int argc, char **argv)
{
	ccprintf("%s\n", adapter_str[identify_adapter()]);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(adapter, command_adapter,
			NULL,
			"Identify AC adapter type",
			NULL);
