/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "dma.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

int adc_enable_watchdog(int ain_id, int high, int low)
{
	return EC_ERROR_UNKNOWN;
}

int adc_disable_watchdog(void)
{
	return EC_ERROR_UNKNOWN;
}

int adc_read_channel(enum adc_channel ch)
{
	return EC_ERROR_UNKNOWN;
}

int adc_read_all_channels(int *data)
{
	return EC_ERROR_UNKNOWN;
}

static void adc_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, adc_init, HOOK_PRIO_DEFAULT);
