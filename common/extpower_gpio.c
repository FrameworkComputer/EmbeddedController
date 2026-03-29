/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Pure GPIO-based external power detection */

#include "common.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "timer.h"

__overridable int board_extpower_is_present(void)
{
	return 0;
}

__overridable void board_extpower_enable_interrupt(void)
{
}

__overridable void board_extpower_disable_interrupt(void)
{
}

static int debounced_extpower_presence;

static int get_extpower_presence(void)
{
#ifdef CONFIG_EXTPOWER_GPIO_CUSTOM
	return board_extpower_is_present();
#else
	return gpio_get_level(GPIO_AC_PRESENT);
#endif
}

void extpower_enable_interrupt(void)
{
#ifdef CONFIG_EXTPOWER_GPIO_CUSTOM
	board_extpower_enable_interrupt();
#else
	gpio_enable_interrupt(GPIO_AC_PRESENT);
#endif
}

void extpower_disable_interrupt(void)
{
#ifdef CONFIG_EXTPOWER_GPIO_CUSTOM
	board_extpower_disable_interrupt();
#else
	gpio_disable_interrupt(GPIO_AC_PRESENT);
#endif
}

test_mockable int extpower_is_present(void)
{
	return debounced_extpower_presence;
}

/**
 * Deferred function to handle external power change
 */
static void extpower_deferred(void)
{
	int extpower_presence = get_extpower_presence();

	if (extpower_presence == debounced_extpower_presence)
		return;

	debounced_extpower_presence = extpower_presence;
	extpower_handle_update(extpower_presence);
}
DECLARE_DEFERRED(extpower_deferred);

void extpower_interrupt(enum gpio_signal signal)
{
	/* Trigger deferred notification of external power change */
	hook_call_deferred(&extpower_deferred_data,
			   CONFIG_EXTPOWER_DEBOUNCE_MS * MSEC);
}

static void extpower_init(void)
{
	debounced_extpower_presence = get_extpower_presence();

	/* Enable interrupts, now that we've initialized */
	extpower_enable_interrupt();
}
DECLARE_HOOK(HOOK_INIT, extpower_init, HOOK_PRIO_INIT_EXTPOWER);
