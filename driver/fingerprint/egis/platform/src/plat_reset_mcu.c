/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "gpio.h"
#include "plat_reset.h"
#include "plat_time.h"

void egis_fp_reset_sensor(void)
{
	gpio_set_level(GPIO_FP_RST_ODL, 0);
	plat_sleep_time(20);
	gpio_set_level(GPIO_FP_RST_ODL, 1);
	plat_sleep_time(20);
	return;
}
