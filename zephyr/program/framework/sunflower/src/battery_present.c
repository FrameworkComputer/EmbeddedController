/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio.h>

#include "battery.h"
#include "console.h"
#include "hooks.h"
#include "system.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)

static int power_on_check_batt;

/* check battery timer */
K_TIMER_DEFINE(check_battery_timer, NULL, NULL);

enum battery_present battery_is_present(void)
{
	static enum battery_present batt_pres = BP_NOT_SURE;
	char text[32];
	static int retry;

	/* timer expired, return no battery */
	if (k_timer_status_get(&check_battery_timer) > 0) {
		CPRINTS("check battery timeout, stop precharge!");
		power_on_check_batt = 0;
		return BP_NO;
	}

	/* check the battery present pin first */

	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_vcin1_batt_temp_l)) == 0) {
		k_timer_stop(&check_battery_timer);
		power_on_check_batt = 0;
		return BP_YES;
	}

	/* try to read the battery information */
	if (battery_device_name(text, sizeof(text))) {
		/* add the retry if read the bad respond */
		if (retry++ > 3 && !power_on_check_batt) {
			batt_pres = BP_NO;
			retry = 0;
		}
	} else {
		k_timer_stop(&check_battery_timer);
		power_on_check_batt = 0;
		batt_pres = BP_YES;
		retry = 0;
	}

	return batt_pres;
}

static void enable_check_battery_timer(void)
{
	/* start the timer */
	power_on_check_batt = 1;
	k_timer_start(&check_battery_timer, K_SECONDS(30), K_NO_WAIT);
}
DECLARE_HOOK(HOOK_INIT, enable_check_battery_timer, HOOK_PRIO_DEFAULT);
