/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Wireless power management */

#include "common.h"
#include "gpio.h"
#include "host_command.h"

void wireless_enable(int flags)
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

static int wireless_enable_cmd(struct host_cmd_handler_args *args)
{
	const struct ec_params_switch_enable_wireless *p = args->params;

	wireless_enable(p->enabled);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SWITCH_ENABLE_WIRELESS,
		     wireless_enable_cmd,
		     EC_VER_MASK(0));
