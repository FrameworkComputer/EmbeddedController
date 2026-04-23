/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ADSP_COMMS_H
#define __CROS_EC_ADSP_COMMS_H

#include "charge_manager.h"

#include <stdint.h>

#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus

extern "C" {
#endif

enum adsp_feature_id {
	ADSP_FEATURE_DEFAULT = 0x03,
	ADSP_FEATURE_OEM_CUSTOM = 0x07,
};

enum adsp_power_state_reg {
	ADSP_POWER_STATE_REG_VAL = 0x11,
	ADSP_POWER_STATE_REG_RESTART = 0x12,
};

enum adsp_oem_custom_reg {
	ADSP_OEM_CUSTOM_REG_MAGIC = 0x01,
	ADSP_OEM_CUSTOM_REG_VERSION = 0x02,
	ADSP_OEM_CUSTOM_REG_CHARGE_PORT = 0x03,
	ADSP_OEM_CUSTOM_REG_CHARGE_STATE = 0x04,
	ADSP_OEM_CUSTOM_REG_BATTERY_STATE = 0x05,
	ADSP_OEM_CUSTOM_REG_BATTERY_LEVEL = 0x06,
};

enum adsp_oem_custom_charge_state {
	ADSP_OEM_CUSTOM_CHARGE_STATE_CHARGE = 0x00,
	ADSP_OEM_CUSTOM_CHARGE_STATE_DISCHARGE = 0x01,
	ADSP_OEM_CUSTOM_CHARGE_STATE_ERROR = 0x02,
	ADSP_OEM_CUSTOM_CHARGE_STATE_IDLE = 0x03,
	ADSP_OEM_CUSTOM_CHARGE_STATE_FORCED_IDLE = 0x04,
	ADSP_OEM_CUSTOM_CHARGE_STATE_NEAR_FULL = 0x05,
};

#define ADSP_OEM_CUSTOM_MAGIC_VAL 0xec
#define ADSP_OEM_CUSTOM_VERSION_1 0x01
#define ADSP_OEM_CUSTOM_CHARGE_PORT_DISABLED 0x00
#define ADSP_OEM_CUSTOM_CHARGE_PORT_START 0x01
#define ADSP_OEM_CUSTOM_CHARGE_PORT_COUNT CHARGE_PORT_COUNT

typedef void (*adsp_comms_callback_t)(uint8_t fid, uint8_t addr, uint16_t data);

struct adsp_comms_callback {
	uint8_t fid;
	uint8_t addr;
	adsp_comms_callback_t cb;
};

/**
 * Register a callback for a specific ADSP feature ID and register address.
 *
 * @param _fid   The feature ID.
 * @param _addr  The register address.
 * @param _cb    The callback function.
 */
#define ADSP_COMMS_REGISTER_CB(_fid, _addr, _cb)                        \
	static const STRUCT_SECTION_ITERABLE(                           \
		adsp_comms_callback, UTIL_CAT(adsp_comms_cb_, _cb)) = { \
		.fid = (_fid),                                          \
		.addr = (_addr),                                        \
		.cb = (_cb),                                            \
	}

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ADSP_COMMS_H */
