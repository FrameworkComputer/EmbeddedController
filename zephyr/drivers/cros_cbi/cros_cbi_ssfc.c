/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/cros_cbi.h>
#include <logging/log.h>

#include "cros_board_info.h"
#include "cros_cbi_ssfc.h"
#include "cros_cbi_common.h"

#define DT_DRV_COMPAT named_cbi_ssfc_value

LOG_MODULE_REGISTER(cros_cbi_ssfc, LOG_LEVEL_ERR);

void cros_cbi_ssfc_init(const struct device *dev)
{
	struct cros_cbi_data *data = (struct cros_cbi_data *)(dev->data);

	if (cbi_get_ssfc(&data->cached_ssfc.raw_value) != EC_SUCCESS) {
		DT_INST_FOREACH_STATUS_OKAY_VARGS(CBI_SSFC_INIT_DEFAULT, data)
	}

	LOG_INF("Read CBI SSFC : 0x%08X\n", data->cached_ssfc.raw_value);
}

static int cros_cbi_ssfc_get_parent_field_value(union cbi_ssfc cached_ssfc,
						enum cbi_ssfc_value_id value_id,
						uint32_t *value)
{
	switch (value_id) {
		DT_INST_FOREACH_STATUS_OKAY_VARGS(CBI_SSFC_PARENT_VALUE_CASE,
						  cached_ssfc, value)
	default:
		LOG_ERR("CBI SSFC parent field value not found: %d\n",
		        value_id);
		return -EINVAL;
	}
	return 0;
}

bool cros_cbi_ec_ssfc_check_match(const struct device *dev,
					 enum cbi_ssfc_value_id value_id)
{
	struct cros_cbi_data *data = (struct cros_cbi_data *)(dev->data);
	struct cros_cbi_config *cfg = (struct cros_cbi_config *)(dev->config);
	int rc;
	uint32_t value;

	rc = cros_cbi_ssfc_get_parent_field_value(data->cached_ssfc,
						  value_id, &value);
	if (rc) {
		return false;
	}
	return value == cfg->ssfc_values[value_id];
}
