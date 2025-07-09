/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "hooks.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(trulo, LOG_LEVEL_INF);

/* Trigger hibernate by enabling the Z-sleep circuit */
__override void board_hibernate_late(void)
{
#ifndef CONFIG_PLATFORM_EC_HIBERNATE_PSL
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_slp_z), 1);
	/*
	 * The system should hibernate, but there may be
	 * a small delay, so return.
	 */
#endif
}
