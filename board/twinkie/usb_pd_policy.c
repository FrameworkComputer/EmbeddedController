/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PDO_FIXED_FLAGS (PDO_FIXED_UNCONSTRAINED | PDO_FIXED_DATA_SWAP)

const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,  3000, PDO_FIXED_FLAGS),
		PDO_FIXED(12000, 3000, PDO_FIXED_FLAGS),
		PDO_FIXED(20000, 3000, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
		PDO_BATT(4750, 21000, 15000),
		PDO_VAR(4750, 21000, 3000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	int red = supply_voltage == 20000;
	int green = supply_voltage == 5000;
	int blue = supply_voltage && !(red || green);
	gpio_set_level(GPIO_LED_R_L, !red);
	gpio_set_level(GPIO_LED_G_L, !green);
	gpio_set_level(GPIO_LED_B_L, !blue);
}

int pd_set_power_supply_ready(int port)
{
	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
}

int pd_snk_is_vbus_provided(int port)
{
	/* assume the alert was programmed to detect bus voltage above 4.5V */
	return (gpio_get_level(GPIO_VBUS_ALERT_L) == 0);
}

__override int pd_check_power_swap(int port)
{
	/* Always refuse power swap */
	return 0;
}

__override int pd_check_data_swap(int port,
				  enum pd_data_role data_role)
{
	/* Always allow data swap */
	return 1;
}

__override void pd_check_pr_role(int port,
				 enum pd_power_role pr_role,
				 int flags)
{
}

__override void pd_check_dr_role(int port,
				 enum pd_data_role dr_role,
				 int flags)
{
}

__override int pd_custom_vdm(int port, int cnt, uint32_t *payload,
		  uint32_t **rpayload)
{
	return 0;
}
