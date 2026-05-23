/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB-C board functions for the Trulo reference board only
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "usbc/pdc_runtime_port_config.h"

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tarkin_usbc, LOG_LEVEL_INF);

/** Supply pdc_power_mgmt with dynamic USB-C port configuration data */
int board_get_pdc_for_port(int port, const struct device **dev)
{
	if (dev == NULL) {
		LOG_ERR("%s: Bad pointer", __func__);
		return -EINVAL;
	}

	*dev = NULL;

	switch (port) {
	case 0:
		if (cros_cbi_ssfc_check_match(
			    CBI_SSFC_VALUE_ID(DT_NODELABEL(tps6699)))) {
			*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0_ti));
			return 0;
		}
		*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0));
		break;
	case 1:
		if (cros_cbi_ssfc_check_match(
			    CBI_SSFC_VALUE_ID(DT_NODELABEL(tps6699)))) {
			*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1_ti));
			return 0;
		}
		*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1));
		break;
	default:
		LOG_ERR("usb port %d not found", port);
		return -ENOENT;
	}

	return 0;
}
