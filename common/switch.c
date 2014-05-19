/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Switch module for Chrome EC */

#include "common.h"
#include "console.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "power_button.h"
#include "switch.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SWITCH, outstr)
#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ## args)

static uint8_t *memmap_switches;

/**
 * Update status of non-debounced switches.
 *
 * Note that deferred functions are called in the same context as lid and
 * power button changes, so we don't need a mutex.
 */
static void switch_update(void)
{
	static uint8_t prev;

	/* Make sure this is safe to call before power_button_init() */
	if (!memmap_switches)
		return;

	prev = *memmap_switches;

	if (power_button_is_pressed())
		*memmap_switches |= EC_SWITCH_POWER_BUTTON_PRESSED;
	else
		*memmap_switches &= ~EC_SWITCH_POWER_BUTTON_PRESSED;

	if (lid_is_open())
		*memmap_switches |= EC_SWITCH_LID_OPEN;
	else
		*memmap_switches &= ~EC_SWITCH_LID_OPEN;

	if ((flash_get_protect() & EC_FLASH_PROTECT_GPIO_ASSERTED) == 0)
		*memmap_switches |= EC_SWITCH_WRITE_PROTECT_DISABLED;
	else
		*memmap_switches &= ~EC_SWITCH_WRITE_PROTECT_DISABLED;

#ifdef CONFIG_SWITCH_DEDICATED_RECOVERY
	if (gpio_get_level(GPIO_RECOVERY_L) == 0)
		*memmap_switches |= EC_SWITCH_DEDICATED_RECOVERY;
	else
		*memmap_switches &= ~EC_SWITCH_DEDICATED_RECOVERY;
#endif

	if (prev != *memmap_switches)
		CPRINTS("SW 0x%02x", *memmap_switches);
}
DECLARE_DEFERRED(switch_update);
DECLARE_HOOK(HOOK_LID_CHANGE, switch_update, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, switch_update, HOOK_PRIO_DEFAULT);

static void switch_init(void)
{
	/* Set up memory-mapped switch positions */
	memmap_switches = host_get_memmap(EC_MEMMAP_SWITCHES);
	*memmap_switches = 0;

	switch_update();

	/* Switch data is now present */
	*host_get_memmap(EC_MEMMAP_SWITCHES_VERSION) = 1;

#ifdef CONFIG_SWITCH_DEDICATED_RECOVERY
	/* Enable interrupts, now that we've initialized */
	gpio_enable_interrupt(GPIO_RECOVERY_L);
#endif

	/*
	 * TODO(crosbug.com/p/23793): It's weird that flash_common.c owns
	 * reading the write protect signal, but we enable the interrupt for it
	 * here.  Take ownership of WP back, or refactor it to its own module.
	 */
#ifdef CONFIG_WP_ACTIVE_HIGH
	gpio_enable_interrupt(GPIO_WP);
#else
	gpio_enable_interrupt(GPIO_WP_L);
#endif
}
DECLARE_HOOK(HOOK_INIT, switch_init, HOOK_PRIO_DEFAULT);

void switch_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(switch_update, 0);
}

static int command_mmapinfo(int argc, char **argv)
{
	uint8_t *memmap_switches = host_get_memmap(EC_MEMMAP_SWITCHES);
	uint8_t val = *memmap_switches;
	int i;
	const char *explanation[] = {
		"lid_open",
		"powerbtn",
		"wp_off",
		"kbd_rec",
		"gpio_rec",
		"fake_dev",
	};
	ccprintf("memmap switches = 0x%x\n", val);
	for (i = 0; i < ARRAY_SIZE(explanation); i++)
		if (val & (1 << i))
			ccprintf(" %s\n", explanation[i]);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(mmapinfo, command_mmapinfo,
			NULL,
			"Print memmap switch state",
			NULL);

