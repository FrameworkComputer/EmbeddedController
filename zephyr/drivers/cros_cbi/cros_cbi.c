/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/cros_cbi.h>

#include "cros_board_info.h"
#include "cros_cbi_ssfc.h"
#include "cros_cbi_common.h"

static int cros_cbi_ec_init(const struct device *dev)
{
	cros_cbi_ssfc_init(dev);
	cros_cbi_fw_config_init(dev);

	return 0;
}

/* cros ec cbi driver registration */
static const struct cros_cbi_driver_api cros_cbi_driver_api = {
	.init = cros_cbi_ec_init,
	.ssfc_check_match = cros_cbi_ec_ssfc_check_match,
	.get_fw_config = cros_cbi_ec_get_fw_config,
};

static int cbi_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

/*
 * These belong in the SSFC specific code, but the array
 * is referenced in the cfg data.
 */
#define DT_DRV_COMPAT named_cbi_ssfc_value
static const uint8_t ssfc_values[] = {
	DT_INST_FOREACH_STATUS_OKAY(CBI_SSFC_VALUE_ARRAY)
};
#undef DT_DRV_COMPAT

static const struct cros_cbi_config cros_cbi_cfg = {
	.ssfc_values = ssfc_values,
};

static struct cros_cbi_data cros_cbi_data;

DEVICE_DEFINE(cros_cbi, CROS_CBI_LABEL, cbi_init, NULL, &cros_cbi_data,
	      &cros_cbi_cfg, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
	      &cros_cbi_driver_api);
