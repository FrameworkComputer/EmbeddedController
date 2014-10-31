/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Dummy power module for Sensor HUB.
 *
 * This implements the following features:
 * when AP_IN_SUSPEND is low, in S0, otherwise S3.
 *
 */

#include "chipset.h"  /* This module implements chipset functions too */
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "power.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

#define IN_SUSPEND POWER_SIGNAL_MASK(ECDRIVEN_SUSPEND_ASSERTED)

enum power_state power_chipset_init(void)
{
	return POWER_S3;
}

enum power_state power_handle_state(enum power_state state)
{
	switch (state) {
	case POWER_S3:
		if (!(power_get_signals() & IN_SUSPEND)) {
			hook_notify(HOOK_CHIPSET_RESUME);
			return POWER_S0;
		}
		return state;

	case POWER_S0:
		if (power_get_signals() & IN_SUSPEND) {
			hook_notify(HOOK_CHIPSET_SUSPEND);
			return POWER_S3;
		}
		return state;
	default:
		CPRINTS("Unexpected state: $d", state);
	}

	return state;
}
