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

struct col_gpio_config {
	const struct device *kbd_dev;
	struct gpio_dt_spec gpio;
	int col;
};

static const struct col_gpio_config col_gpio_cfg_0 = {
	.kbd_dev = DEVICE_DT_GET(DT_PARENT(COL_GPIO_NODE)),
	.gpio = GPIO_DT_SPEC_GET(COL_GPIO_NODE, col_gpios),
	.col = DT_PROP(COL_GPIO_NODE, col_num),
};

void input_kbd_matrix_drive_column_hook(const struct device *dev, int col)
{
	const struct col_gpio_config *cfg = &col_gpio_cfg_0;

	if (dev != cfg->kbd_dev) {
		return;
	}

	if (col == INPUT_KBD_MATRIX_COLUMN_DRIVE_ALL || col == cfg->col) {
		gpio_pin_set_dt(&cfg->gpio, 1);
	} else {
		gpio_pin_set_dt(&cfg->gpio, 0);
	}
}

static int col_gpio_init(const struct device *dev)
{
	const struct col_gpio_config *cfg = dev->config;
	int ret;

	if (!gpio_is_ready_dt(&cfg->gpio)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->gpio, GPIO_OUTPUT_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Pin configuration failed: %d", ret);
		return ret;
	}

	return 0;
}
DEVICE_DT_DEFINE(COL_GPIO_NODE, col_gpio_init, NULL, NULL, &col_gpio_cfg_0,
		 POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);
