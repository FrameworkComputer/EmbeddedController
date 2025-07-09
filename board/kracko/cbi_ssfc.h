/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _DEDEDE_CBI_SSFC__H_
#define _DEDEDE_CBI_SSFC__H_

#include "stdint.h"

/****************************************************************************
 * Dedede CBI Second Source Factory Cache
 */

/*
 * Base Sensor (Bits 0-2)
 */
enum ec_ssfc_base_sensor {
	SSFC_SENSOR_BASE_DEFAULT = 0,
	SSFC_SENSOR_BMI160 = 1,
	SSFC_SENSOR_ICM426XX = 2,
	SSFC_SENSOR_LSM6DSM = 3,
	SSFC_SENSOR_ICM42607 = 4
};

/*
 * Lid Sensor (Bits 3-5)
 */
enum ec_ssfc_lid_sensor {
	SSFC_SENSOR_LID_DEFAULT = 0,
	SSFC_SENSOR_BMA255 = 1,
	SSFC_SENSOR_KX022 = 2,
	SSFC_SENSOR_LIS2DWL = 3,
	SSFC_SENSOR_BMA422 = 4
};

/*
 * TCPC Port 1 (Bits 6-7)
 */
enum ec_ssfc_tcpc_p1 {
	SSFC_TCPC_P1_DEFAULT,
	SSFC_TCPC_P1_PS8705,
	SSFC_TCPC_P1_PS8805,
};

/*
 * Audio Codec Source(Bit 8-10)
 */
enum ec_ssfc_audio_codec_source {
	SSFC_AUDIO_CODEC_DEFAULT = 0,
	SSFC_AUDIO_CODEC_VD = 1,
	SSFC_ADUIO_CODEC_VS = 2,
};

union dedede_cbi_ssfc {
	struct {
		uint32_t base_sensor : 3;
		uint32_t lid_sensor : 3;
		uint32_t tcpc_type : 2;
		uint32_t audio_codec_source : 3;
		uint32_t reserved_2 : 21;
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

/**
 * Get the TCPC port 1 type from SSFC_CONFIG.
 *
 * @return the TCPC type.
 */
enum ec_ssfc_tcpc_p1 get_cbi_ssfc_tcpc_p1(void);

#endif /* _DEDEDE_CBI_SSFC__H_ */
