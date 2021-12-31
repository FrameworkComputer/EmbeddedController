/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_CBI_CROS_CBI_COMMON_H
#define __CROS_CBI_CROS_CBI_COMMON_H

/* Device config */
struct cros_cbi_config {
	/* SSFC values for specific configs */
	const uint8_t *ssfc_values;
};

/* Device data */
struct cros_cbi_data {
	/* Cached SSFC configs */
	union cbi_ssfc cached_ssfc;
	/* Cached FW_CONFIG bits */
	uint32_t cached_fw_config;
};

void cros_cbi_ssfc_init(const struct device *dev);
bool cros_cbi_ec_ssfc_check_match(const struct device *dev,
					 enum cbi_ssfc_value_id value_id);
void cros_cbi_fw_config_init(const struct device *dev);
int cros_cbi_ec_get_fw_config(const struct device *dev,
			      enum cbi_fw_config_field_id field_id,
			      uint32_t *value);


#endif /* __CROS_CBI_CROS_CBI_COMMON_H */
