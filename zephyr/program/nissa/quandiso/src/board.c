/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Quandiso hardware configuration */
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "nissa_common.h"
#include "tablet_mode.h"
#include "task.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <ap_power/ap_power.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

/*
 * Enable interrupts
 */
static void board_init(void)
{
	int ret;
	uint32_t val;

	/*
	 * Retrieve the tablet config.
	 */
	ret = cros_cbi_get_fw_config(FW_TABLET, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_TABLET);
		return;
	}

	/*
	 * Disable tablet related interrupts for tablet absent DUT.
	 */
	if (val == FW_TABLET_ABSENT) {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
		/* Base accel is not stuffed, don't allow line to float */
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_imu_int_l),
				      GPIO_INPUT | GPIO_PULL_DOWN);
		LOG_INF("Clameshell: Disable motion sensors and gmr sensor!");
	} else
		LOG_INF("Convertible!!!");
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_POST_I2C);

void pen_detect_interrupt(enum gpio_signal s)
{
	int pen_detect =
		!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pen_detect_odl));

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen_x),
			pen_detect);
}

__override void board_power_change(struct ap_power_ev_callback *cb,
				   struct ap_power_ev_data data)
{
	const struct gpio_dt_spec *const pen_power_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen_x);
	const struct gpio_dt_spec *const pen_detect_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_pen_detect_odl);
	const struct gpio_int_config *const pen_detect_int =
		GPIO_INT_FROM_NODELABEL(int_pen_det_l);

	switch (data.event) {
	case AP_POWER_STARTUP:
		/* Enable Pen Detect interrupt */
		gpio_enable_dt_interrupt(pen_detect_int);
		/*
		 * Make sure pen detection is triggered or not when AP power on
		 */
		if (!gpio_pin_get_dt(pen_detect_gpio))
			gpio_pin_set_dt(pen_power_gpio, 1);
		break;
	case AP_POWER_SHUTDOWN:
		/*
		 * Disable pen detect INT and turn off pen power when AP
		 * shutdown
		 */
		gpio_disable_dt_interrupt(pen_detect_int);
		gpio_pin_set_dt(pen_power_gpio, 0);
		break;
	default:
		break;
	}
}
