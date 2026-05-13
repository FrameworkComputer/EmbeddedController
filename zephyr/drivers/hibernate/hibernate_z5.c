/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_hibernate_z5

#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>

LOG_MODULE_REGISTER(hibernate_z5, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "only one 'cros-ec,hibernate-z5' compatible node may be present");

struct hibernate_z5_config {
	struct gpio_dt_spec en_slp_z_gpio;
};

static const struct hibernate_z5_config hibernate_cfg = {
	.en_slp_z_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), en_slp_z_gpios),
};

__override void board_hibernate_late(void)
{
	gpio_pin_set_dt(&hibernate_cfg.en_slp_z_gpio, 1);

	/*
	 * Ensure the GPIO is asserted long enough to discharge the
	 * the PP3300_Z1 regulator.
	 */
	k_busy_wait(1 * USEC_PER_SEC);

	/* This function isn't expected to return as the platform hardware
	 * will remove power to the EC.
	 */
}

static int hibernate_z5_init(const struct device *dev)
{
	int ret;
	const struct hibernate_z5_config *cfg = dev->config;

	if (!gpio_is_ready_dt(&cfg->en_slp_z_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->en_slp_z_gpio.port);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->en_slp_z_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("Pin configuration failed: %d", ret);
		return ret;
	}

	return 0;
}

/* Hibernate Z5 depends on GPIO drivers being ready. */
BUILD_ASSERT(CONFIG_HIBERNATE_Z5_INIT_PRIORITY > CONFIG_GPIO_INIT_PRIORITY);

DEVICE_DT_INST_DEFINE(0, hibernate_z5_init, NULL, NULL, &hibernate_cfg,
		      POST_KERNEL, CONFIG_HIBERNATE_Z5_INIT_PRIORITY, NULL);
