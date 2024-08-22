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
	SSFC_SENSOR_ICM42607 = 4,
	SSFC_SENSOR_BMI323 = 5
};

/*
 * Lid Sensor (Bits 3-5)
 */
enum ec_ssfc_lid_sensor {
	SSFC_SENSOR_LID_DEFAULT = 0,
	SSFC_SENSOR_BMA255 = 1,
	SSFC_SENSOR_KX022 = 2,
	SSFC_SENSOR_BMA422 = 3
};

/*
 * Usb Mux (Bits 20-21)
 */
enum ec_ssfc_usb_mux {
	SSFC_USBMUX_PS8762 = 0,
	SSFC_USBMUX_PS8743 = 1,
};

union dedede_cbi_ssfc {
	struct {
		uint32_t base_sensor : 3;
		uint32_t lid_sensor : 3;
		uint32_t cam_wfc : 3;
		uint32_t cam_ufc : 2;
		uint32_t cam_vcm : 2;
		uint32_t TS_Source : 4;
		uint32_t AUDIO_CODEC_SOURCE : 3;
		uint32_t usb_mux : 2;
		uint32_t reserved_2 : 10;
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
 * Get the Usb Mux type from SSFC_CONFIG.
 *
 * @return the Usb Mux board type.
 */
enum ec_ssfc_usb_mux get_cbi_ssfc_usb_mux(void);

#endif /* _DEDEDE_CBI_SSFC__H_ */
