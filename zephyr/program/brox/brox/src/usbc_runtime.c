/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB-C board functions for the Brox reference board only
 */

#include "cros_board_info.h"
#include "usbc/pdc_runtime_port_config.h"

#include <stdint.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(brox_usbc, LOG_LEVEL_INF);

/** Supply pdc_power_mgmt with dynamic USB-C port configuration data */
int board_get_pdc_for_port(int port, const struct device **dev)
{
	int rv;
	uint32_t sku_id;

	if (dev == NULL) {
		LOG_ERR("%s: Bad pointer", __func__);
		return -EINVAL;
	}

	rv = cbi_get_sku_id(&sku_id);
	if (rv) {
		LOG_ERR("%s: Cannot read CBI SKU ID: %d. PDC config unknown!",
			__func__, rv);

		*dev = NULL;
		return -EIO;
	}

	switch (sku_id) {
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x21:
	case 0x22:
	case 0x23:
		/* RTK PDC */
		switch (port) {
		case 0:
			*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0));
			return 0;
		case 1:
			*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1));
			return 0;
		}
		break;
	case 0x24:
		/* TI PDC */
		switch (port) {
		case 0:
			*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0_ti));
			return 0;
		case 1:
			*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1_ti));
			return 0;
		}
		break;
	}

	LOG_ERR("%s: No entry for SKU (0x%x) or port %d", __func__, sku_id,
		port);

	*dev = NULL;
	return -ENOENT;
}
