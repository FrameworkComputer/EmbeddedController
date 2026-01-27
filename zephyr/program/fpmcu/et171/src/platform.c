/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor.h"
#include "write_protect.h"

#define MT_DATA_OFF DT_REG_ADDR(DT_NODELABEL(mt_data))
struct mt_data {
	uint32_t size;
	uint8_t data[];
};

int fp_vendor_command(uint32_t param, uint8_t *buf, size_t buf_size)
{
	const struct mt_data *mt_data =
		(struct mt_data *)(CONFIG_FLASH_BASE_ADDRESS + MT_DATA_OFF);
	if (mt_data->size > buf_size) {
		return -EC_RES_RESPONSE_TOO_BIG;
	}
	memcpy(buf, (uint8_t *)mt_data->data, mt_data->size);

	return mt_data->size;
}

/* TODO(b/432682921): Change based on the final solution of WP state. */
bool write_protect_is_asserted_custom(void)
{
	return false;
}
