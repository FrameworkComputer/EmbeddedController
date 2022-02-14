/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _REDRIX_CBI_SSFC_H_
#define _REDRIX_CBI_SSFC_H_

#include "stdint.h"

/****************************************************************************
 * Redrix CBI Second Source Factory Cache
 */

/*
 * Lid Sensor (Bits 0-1)
 */
enum ec_ssfc_lid_sensor {
	SSFC_SENSOR_LID_DEFAULT = 0,
	SSFC_SENSOR_LID_BMA253 = 1,
	SSFC_SENSOR_LID_BMA422 = 2
};

union redrix_cbi_ssfc {
	struct {
		enum ec_ssfc_lid_sensor lid_sensor : 2;
		uint32_t reserved_1 : 30;
	};
	uint32_t raw_value;
};

/**
 * Get the Lid sensor type from SSFC_CONFIG.
 *
 * @return the Lid sensor board type.
 */
enum ec_ssfc_lid_sensor get_cbi_ssfc_lid_sensor(void);

#endif /* _REDRIX_CBI_SSFC_H_ */
