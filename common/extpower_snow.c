/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* External power detection for snow */

#include "common.h"
#include "extpower.h"
#include "gpio.h"
#include "pmu_tpschrome.h"
#include "task.h"

int extpower_is_present(void)
{
	/*
	 * Detect AC state using combined gpio pins
	 *
	 * On snow, there's no single gpio signal to detect AC.
	 *   GPIO_AC_PWRBTN_L provides AC on and PWRBTN release.
	 *   GPIO_KB_PWR_ON_L provides PWRBTN release.
	 *
	 * When AC plugged, both GPIOs will be high.
	 *
	 * One drawback of this detection is, when press-and-hold power
	 * button. AC state will be unknown. This function will fallback
	 * to PMU VACG.
	 */

	int ac_good = 1, battery_good;

	if (gpio_get_level(GPIO_KB_PWR_ON_L))
		return gpio_get_level(GPIO_AC_PWRBTN_L);

	/* Check PMU VACG */
	if (!in_interrupt_context())
		pmu_get_power_source(&ac_good, &battery_good);

	/*
	 * Charging task only interacts with AP in discharging state. So
	 * return 1 when AC status can not be detected by GPIO or VACG.
	 */
	return ac_good;
}

/* TODO(crosbug.com/p/23810): host events and hook notifications */
