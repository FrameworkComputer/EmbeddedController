/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"

#include <drivers/microchip_wp.h>

#define DT_DRV_COMPAT microchip_write_protect

BUILD_ASSERT(
	DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	"Exactly one instance of microchip,write-protect should be defined.");

#define FOREACH_MCHP_GPIO(inst, prop) \
	DT_INST_FOREACH_PROP_ELEM_SEP(inst, prop, GPIO_DT_SPEC_GET_BY_IDX, (, ))

const struct mchp_wp_config *wp_config;

struct mchp_wp_config {
	const struct gpio_dt_spec wp_gpios;
	const struct gpio_dt_spec ex_wp_gpios;
};

void sync_wp_assert_status(void)
{
	/* For MEC1727, Sync up mchp_wp state with gpio_wp state */
	gpio_pin_set_dt(&wp_config->wp_gpios,
			gpio_pin_get_dt(&wp_config->ex_wp_gpios));
}

static int mchp_wp_init(const struct device *dev)
{
	wp_config = dev->config;

	if (!gpio_is_ready_dt(&wp_config->wp_gpios)) {
		return -ENODEV;
	}
	gpio_pin_configure_dt(&wp_config->wp_gpios, GPIO_OUTPUT_HIGH);

	if (!gpio_is_ready_dt(&wp_config->ex_wp_gpios)) {
		return -ENODEV;
	}
	gpio_pin_configure_dt(&wp_config->ex_wp_gpios, GPIO_INPUT);

	return 0;
}

static const struct mchp_wp_config mchp_wp_cfg = {
	.wp_gpios = GPIO_DT_SPEC_GET(DT_DRV_INST(0), wp_gpios),
	.ex_wp_gpios = GPIO_DT_SPEC_GET(DT_DRV_INST(0), wp_ex_gpios),
};

DEVICE_DT_INST_DEFINE(0, mchp_wp_init, NULL, NULL, &mchp_wp_cfg, POST_KERNEL,
		      CONFIG_APPLICATION_INIT_PRIORITY, NULL);
