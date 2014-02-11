/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Wireless power management */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "host_command.h"
#include "util.h"
#include "wireless.h"

/* Unless told otherwise, disable wireless in suspend */
#ifndef CONFIG_WIRELESS_SUSPEND
#define CONFIG_WIRELESS_SUSPEND 0
#endif

/*
 * Flags which will be left on when suspending.  Other flags will be disabled
 * when suspending.
 */
static int suspend_flags = CONFIG_WIRELESS_SUSPEND;

/**
 * Set wireless switch state.
 *
 * @param flags		Enable flags from ec_commands.h (EC_WIRELESS_SWITCH_*),
 *			0 to turn all wireless off, or -1 to turn all wireless
 *			on.
 * @param mask		Which of those flags to set
 */
static void wireless_enable(int flags)
{
#ifdef WIRELESS_GPIO_WLAN
	gpio_set_level(WIRELESS_GPIO_WLAN,
		       flags & EC_WIRELESS_SWITCH_WLAN);
#endif

#ifdef WIRELESS_GPIO_WWAN
	gpio_set_level(WIRELESS_GPIO_WWAN,
		       flags & EC_WIRELESS_SWITCH_WWAN);
#endif

#ifdef WIRELESS_GPIO_BLUETOOTH
	gpio_set_level(WIRELESS_GPIO_BLUETOOTH,
		       flags & EC_WIRELESS_SWITCH_BLUETOOTH);
#endif

#ifdef WIRELESS_GPIO_WLAN_POWER
	gpio_set_level(WIRELESS_GPIO_WLAN_POWER,
		       flags & EC_WIRELESS_SWITCH_WLAN_POWER);
#endif

}

static int wireless_get(void)
{
	int flags = 0;

#ifdef WIRELESS_GPIO_WLAN
	if (gpio_get_level(WIRELESS_GPIO_WLAN))
		flags |= EC_WIRELESS_SWITCH_WLAN;
#endif

#ifdef WIRELESS_GPIO_WWAN
	if (gpio_get_level(WIRELESS_GPIO_WWAN))
		flags |= EC_WIRELESS_SWITCH_WWAN;
#endif

#ifdef WIRELESS_GPIO_BLUETOOTH
	if (gpio_get_level(WIRELESS_GPIO_BLUETOOTH))
		flags |= EC_WIRELESS_SWITCH_BLUETOOTH;
#endif

#ifdef WIRELESS_GPIO_WLAN_POWER
	if (gpio_get_level(WIRELESS_GPIO_WLAN_POWER))
		flags |= EC_WIRELESS_SWITCH_WLAN_POWER;
#endif

	return flags;
}

void wireless_set_state(enum wireless_power_state state)
{
	switch (state) {
	case WIRELESS_OFF:
		wireless_enable(0);
		break;
	case WIRELESS_SUSPEND:
		/*
		 * When suspending, only turn things off.  If the AP has
		 * disabled WiFi power, going into S3 should not re-enable it.
		 */
		wireless_enable(wireless_get() & suspend_flags);
		break;
	case WIRELESS_ON:
		wireless_enable(EC_WIRELESS_SWITCH_ALL);
		break;
	}
}

static int wireless_enable_cmd(struct host_cmd_handler_args *args)
{
	const struct ec_params_switch_enable_wireless_v1 *p = args->params;
	struct ec_response_switch_enable_wireless_v1 *r = args->response;

	if (args->version == 0) {
		/* Ver.0 command just set all current flags */
		wireless_enable(p->now_flags);
		return EC_RES_SUCCESS;
	}

	/* Ver.1 can set flags based on mask */
	wireless_enable((wireless_get() & ~p->now_mask) |
			(p->now_flags & p->now_mask));

	suspend_flags = (suspend_flags & ~p->suspend_mask) |
		(p->suspend_flags & p->suspend_mask);

	/* And return the current flags */
	r->now_flags = wireless_get();
	r->suspend_flags = suspend_flags;
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SWITCH_ENABLE_WIRELESS,
		     wireless_enable_cmd,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static int command_wireless(int argc, char **argv)
{
	char *e;
	int i;

	if (argc >= 2) {
		i = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		wireless_enable(i);
	}

	if (argc >= 3) {
		i = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		suspend_flags = i;
	}

	ccprintf("Wireless flags: now=0x%x, suspend=0x%x\n", wireless_get(),
		 suspend_flags);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(wireless, command_wireless,
			"[now [suspend]]",
			"Get/set wireless flags",
			NULL);
