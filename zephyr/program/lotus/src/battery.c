/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio.h>

#include "battery.h"
#include "battery_smart.h"
#include "battery_fuel_gauge.h"
#include "console.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)

enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres = BP_NO;
	char text[32];
	static int retry;

	/*
	 * EC does not connect to the battery present pin,
	 * add the workaround to read the battery device name (register 0x21).
	 */

	if (battery_device_name(text, sizeof(text))) {
		if (retry++ > 3) {
			batt_pres = BP_NO;
			retry = 0;
		}
	} else {
		batt_pres = BP_YES;
		retry = 0;
	}

	return batt_pres;
}
