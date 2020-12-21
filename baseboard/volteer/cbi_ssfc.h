/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _VOLTEER_CBI_SSFC__H_
#define _VOLTEER_CBI_SSFC__H_

#include "stdint.h"

/****************************************************************************
 * Volteer CBI Second Source Factory Cache
 */

/*
 * Base Sensor (Bits 0-2)
 */
enum ec_ssfc_base_sensor {
	SSFC_SENSOR_BASE_DEFAULT = 0,
	SSFC_SENSOR_BMI160 = 1,
	SSFC_SENSOR_ICM426XX = 2
};

/*
 * Lid Sensor (Bits 3-5)
 */
enum ec_ssfc_lid_sensor {
	SSFC_SENSOR_LID_DEFAULT = 0,
	SSFC_SENSOR_BMA255 = 1,
	SSFC_SENSOR_KX022 = 2
};

union volteer_cbi_ssfc {
	struct {
		enum ec_ssfc_base_sensor	base_sensor : 3;
		enum ec_ssfc_lid_sensor		lid_sensor : 3;
		uint32_t					reserved_2 : 26;
	};
	uint32_t raw_value;
};

/**
 * Get the Base sensor type from SSFC_CONFIG.
 *
 * @return the Base sensor board type.
 */
enum ec_ssfc_base_sensor get_cbi_ssfc_base_sensor(void);

/**
 * Get the Lid sensor type from SSFC_CONFIG.
 *
 * @return the Lid sensor board type.
 */
enum ec_ssfc_lid_sensor get_cbi_ssfc_lid_sensor(void);



#endif /* _Volteer_CBI_SSFC__H_ */
