/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usbc/pdc_runtime_port_config.h"

#include <errno.h>

#include <zephyr/device.h>

/** Supply pdc_power_mgmt with dynamic USB-C port configuration data */
int board_get_pdc_for_port(int port, const struct device **dev)
{
	switch (port) {
	case 0:
		*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_emul1));
		return 0;
	case 1:
		*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_emul2));
		return 0;
	}

	/* LCOV_EXCL_START - test fixture code */
	*dev = NULL;
	return -ERANGE;
	/* LCOV_EXCL_STOP */
}
