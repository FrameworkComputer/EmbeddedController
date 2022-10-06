/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _RAMMUS_CBI_SSFC__H_
#define _RAMMUS_CBI_SSFC__H_

#include "stdint.h"

/****************************************************************************
 * Rammus CBI Second Source Factory Cache
 */

/*
 * Lid Sensor (Bits 2-0)
 */
enum ec_ssfc_lid_sensor {
	SSFC_SENSOR_LID_DEFAULT = 0,
	SSFC_SENSOR_LID_BMA255 = 1,
	SSFC_SENSOR_LID_KX022 = 2
};

/*
 * Base Sensor (Bits 5-3)
 */
enum ec_ssfc_base_sensor {
	SSFC_SENSOR_BASE_DEFAULT = 0,
	SSFC_SENSOR_BASE_BMI160 = 1,
	SSFC_SENSOR_BASE_ICM426XX = 2,
};

union rammus_cbi_ssfc {
	struct {
		enum ec_ssfc_lid_sensor lid_sensor : 3;
		enum ec_ssfc_base_sensor base_sensor : 3;
		uint32_t reserved_2 : 26;
	};
	uint32_t raw_value;
};

/**
 * Get the Lid sensor type from SSFC_CONFIG.
 *
 * @return the Lid sensor board type.
 */
enum ec_ssfc_lid_sensor get_cbi_ssfc_lid_sensor(void);

/**
 * Get the base sensor type form SSFC_CONFIG.
 *
 * @return the base sensor board type.
 */
enum ec_ssfc_base_sensor get_cbi_ssfc_base_sensor(void);

#endif /* _RAMMUS_CBI_SSFC__H_ */
