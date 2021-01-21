/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _ZORK_CBI_SSFC__H_
#define _ZORK_CBI_SSFC__H_

#include "stdint.h"

/****************************************************************************
 * Zork CBI Second Source Factory Cache
 */

/*
 * Base Sensor (Bits 0-2)
 */
enum ec_ssfc_base_gyro_sensor {
	SSFC_BASE_GYRO_NONE = 0,
	SSFC_BASE_GYRO_BMI160 = 1,
	SSFC_BASE_GYRO_LSM6DSM = 2,
	SSFC_BASE_GYRO_ICM426XX = 3,
};

union zork_cbi_ssfc {
	struct {
		enum ec_ssfc_base_gyro_sensor	base_sensor : 3;
		uint32_t			reserved : 29;
	};
	uint32_t raw_value;
};

/**
 * Get the Base sensor type from SSFC_CONFIG.
 *
 * @return the Base sensor board type.
 */
enum ec_ssfc_base_gyro_sensor get_cbi_ssfc_base_sensor(void);

#endif /* _ZORK_CBI_SSFC__H_ */
