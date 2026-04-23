/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adsp_comms.h"
#include "common.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(adsp_comms, LOG_LEVEL_INF);

static void adsp_power_state_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	LOG_INF("ADSP: Power State: 0x%04x", data);
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_DEFAULT, ADSP_POWER_STATE_REG_VAL,
		       adsp_power_state_cb);

static void adsp_oem_magic_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	if (data == ADSP_OEM_CUSTOM_MAGIC_VAL) {
		LOG_INF("ADSP: Comms established");
	} else {
		LOG_INF("ADSP: Incorrect magic packet received (0x%04x)", data);
	}
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_MAGIC,
		       adsp_oem_magic_cb);
