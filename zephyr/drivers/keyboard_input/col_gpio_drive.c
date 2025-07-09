/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(col_gpio_drive, CONFIG_INPUT_LOG_LEVEL);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(cros_ec_col_gpio) == 1,
	     "only one cros-ec,col-gpio compatible node can be supported");

#define COL_GPIO_NODE DT_INST(0, cros_ec_col_gpio)

#if CONFIG_DT_HAS_ITE_IT8XXX2_KBD_ENABLED
BUILD_ASSERT(DT_PROP(DT_PARENT(COL_GPIO_NODE), kso_ignore_mask) != 0,
	     "kso-ignore-mask must be specified on ITE devices for "
	     "ec-col-gpio to work correctly");
#endif

struct col_gpio_config {
	const struct device *kbd_dev;
	struct gpio_dt_spec gpio;
	int col;
	uint32_t settle_time_us;
};

struct col_gpio_data {
	bool state;
};

static const struct col_gpio_config col_gpio_cfg_0 = {
	.kbd_dev = DEVICE_DT_GET(DT_PARENT(COL_GPIO_NODE)),
	.gpio = GPIO_DT_SPEC_GET(COL_GPIO_NODE, col_gpios),
	.col = DT_PROP(COL_GPIO_NODE, col_num),
	.settle_time_us = DT_PROP(COL_GPIO_NODE, settle_time_us),
};

static struct col_gpio_data col_gpio_data_0;

void input_kbd_matrix_drive_column_hook(const struct device *dev, int col)
{
	const struct col_gpio_config *cfg = &col_gpio_cfg_0;
	struct col_gpio_data *data = &col_gpio_data_0;
	bool state;

	if (dev != cfg->kbd_dev) {
		return;
	}

	if (col == INPUT_KBD_MATRIX_COLUMN_DRIVE_ALL || col == cfg->col) {
		gpio_pin_set_dt(&cfg->gpio, 1);
		state = true;
	} else {
		gpio_pin_set_dt(&cfg->gpio, 0);
		state = false;
	}

	if (state != data->state) {
		data->state = state;
		k_busy_wait(cfg->settle_time_us);
	}
}

static int col_gpio_init(const struct device *dev)
{
	const struct col_gpio_config *cfg = dev->config;
	struct col_gpio_data *data = &col_gpio_data_0;
	int ret;

	if (!gpio_is_ready_dt(&cfg->gpio)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->gpio, GPIO_OUTPUT_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Pin configuration failed: %d", ret);
		return ret;
	}

	data->state = true;

	return 0;
}
DEVICE_DT_DEFINE(COL_GPIO_NODE, col_gpio_init, NULL, &col_gpio_data_0,
		 &col_gpio_cfg_0, POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY,
		 NULL);
