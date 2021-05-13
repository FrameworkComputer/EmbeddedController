/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "gpio.h"
#include "power.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PMIC_EC_PWRGD, POWER_SIGNAL_ACTIVE_HIGH, "PMIC_PWR_GOOD"},
	{GPIO_AP_IN_SLEEP_L, POWER_SIGNAL_ACTIVE_LOW, "AP_IN_S3_L"},
	{GPIO_AP_EC_WATCHDOG_L, POWER_SIGNAL_ACTIVE_LOW, "AP_WDT_ASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);
