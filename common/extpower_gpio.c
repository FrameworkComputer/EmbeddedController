/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Pure GPIO-based external power detection */

#include "common.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"

int extpower_is_present(void)
{
	return gpio_get_level(GPIO_AC_PRESENT);
}

/**
 * Deferred function to handle external power change
 */
static void extpower_deferred(void)
{
	hook_notify(HOOK_AC_CHANGE);

	/* Forward notification to host */
	if (extpower_is_present())
		host_set_single_event(EC_HOST_EVENT_AC_CONNECTED);
	else
		host_set_single_event(EC_HOST_EVENT_AC_DISCONNECTED);
}
DECLARE_DEFERRED(extpower_deferred);

void extpower_interrupt(enum gpio_signal signal)
{
	/* Trigger deferred notification of external power change */
	hook_call_deferred(extpower_deferred, 0);
}
