/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_board_info.h"

test_mockable int cbi_get_ssfc(uint32_t *ssfc)
{
	uint8_t size = sizeof(*ssfc);

	return cbi_get_board_info(CBI_TAG_SSFC, (uint8_t *)ssfc, &size);
}

test_mockable int cbi_get_fw_config(uint32_t *fw_config)
{
	uint8_t size = sizeof(*fw_config);

	return cbi_get_board_info(CBI_TAG_FW_CONFIG, (uint8_t *)fw_config,
				  &size);
}
