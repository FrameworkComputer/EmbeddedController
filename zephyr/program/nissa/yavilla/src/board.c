/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* yavilla hardware configuration */
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "tablet_mode.h"
#include "task.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);
__override uint8_t board_get_usb_pd_port_count(void)
{
	return 2;
}
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
	 * Enable USB-C interrupts.
	 */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1));

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
		/* Lid accel is not stuffed, don't allow line to float */
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_acc_int_l),
				      GPIO_INPUT | GPIO_PULL_DOWN);
		LOG_INF("Clameshell: Disable motion sensors and gmr sensor!");
	} else
		LOG_INF("Convertible!!!");
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_POST_I2C);
