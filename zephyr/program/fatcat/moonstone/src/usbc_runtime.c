/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "usbc/pdc_power_mgmt.h"
#include "usbc/pdc_runtime_port_config.h"

#include <stdint.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(moonstone_usbc, LOG_LEVEL_INF);

static bool probe_pdc_chip_on_daughter_board(const struct device *dev)
{
	struct pdc_hw_config_t config;
	int rv;

	if (dev == NULL) {
		LOG_ERR("%s: Invalid pointer", __func__);
		return false;
	}

	rv = pdc_get_hw_config(dev, &config);
	if (rv) {
		LOG_ERR("%s: Cannot get bus info for PDC %s: %d", __func__,
			dev->name ? dev->name : "unnamed", rv);
		return false;
	}

	struct i2c_msg msgs[1];
	uint8_t dst;

	msgs[0].buf = &dst;
	msgs[0].len = 0U;
	msgs[0].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	/* If the transfer succeeds, a chip is at this address */
	return i2c_transfer_dt(&config.i2c, &msgs[0], 1) == 0;
}

/* Supply pdc_power_mgmt with dynamic USB-C port configuration data */
int board_get_pdc_for_port(int port, const struct device **dev)
{
	if (dev == NULL) {
		LOG_ERR("%s: Bad pointer", __func__);
		return -EINVAL;
	}

	switch (port) {
	case 0:
		*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0));
		return 0;
	case 1:
		*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1));
		return 0;
	case 2:
		if (!probe_pdc_chip_on_daughter_board(
			    DEVICE_DT_GET(DT_NODELABEL(pdc_power_p2)))) {
			*dev = NULL;
		} else {
			*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p2));
		}
		return 0;
	}

	LOG_ERR("%s: No entry for port %d", __func__, port);

	*dev = NULL;
	return -ENOENT;
}
