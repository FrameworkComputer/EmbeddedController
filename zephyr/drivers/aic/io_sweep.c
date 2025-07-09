/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_aic_pins

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(io_sweep, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "only one 'cros,aic-pins' compatible node may be present");

static const struct gpio_dt_spec ec_io_gpio[] = { DT_INST_FOREACH_PROP_ELEM_SEP(
	0, io_gpios, GPIO_DT_SPEC_GET_BY_IDX, (, )) };

static int cmd_io_sweep(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	for (uint8_t i = 0; i < ARRAY_SIZE(ec_io_gpio); i++) {
		const struct gpio_dt_spec *gpio = &ec_io_gpio[i];

		ret = gpio_pin_configure_dt(gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("gpio configuration failed: %d", ret);
			return ret;
		}
	}

	for (uint8_t i = 0; i < ARRAY_SIZE(ec_io_gpio); i++) {
		LOG_INF("test pin %d", i);
		for (uint8_t j = 0; j < ARRAY_SIZE(ec_io_gpio); j++) {
			const struct gpio_dt_spec *gpio = &ec_io_gpio[j];

			gpio_pin_set_dt(gpio, i == j ? 1 : 0);
			LOG_INF("pin %d: %d", j, i == j ? 1 : 0);
		}

		k_sleep(K_MSEC(200));
	}

	for (uint8_t i = 0; i < ARRAY_SIZE(ec_io_gpio); i++) {
		const struct gpio_dt_spec *gpio = &ec_io_gpio[i];

		ret = gpio_pin_configure_dt(gpio, GPIO_INPUT);
		if (ret < 0) {
			LOG_ERR("gpio configuration failed: %d", ret);
			return ret;
		}
	}

	return 0;
};

SHELL_CMD_REGISTER(io_sweep, NULL, "AIC io sweep", cmd_io_sweep);
