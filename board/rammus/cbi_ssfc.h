/* Copyright 2021 The Chromium OS Authors. All rights reserved.
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

union rammus_cbi_ssfc {
	struct {
		enum ec_ssfc_lid_sensor lid_sensor : 3;
		uint32_t reserved_2 : 29;
	};
	uint32_t raw_value;
};

/**
 * Get the Lid sensor type from SSFC_CONFIG.
 *
 * @return the Lid sensor board type.
 */
enum ec_ssfc_lid_sensor get_cbi_ssfc_lid_sensor(void);

#endif /* _RAMMUS_CBI_SSFC__H_ */
