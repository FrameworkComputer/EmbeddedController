/* Copyright 2021 The Chromium OS Authors. All rights reserved.
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
	SSFC_SENSOR_LIS2DWL = 3
};

/*
 * USB SuperSpeed Mux (Bits 6-8)
 */
enum ec_ssfc_usb_ss_mux {
	SSFC_USB_SS_MUX_DEFAULT = 0,
	SSFC_USB_SS_MUX_PS8743 = 1,
	SSFC_USB_SS_MUX_PI3USBX532 = 2,
};

union dedede_cbi_ssfc {
	struct {
		uint32_t base_sensor : 3;
		uint32_t lid_sensor : 3;
		uint32_t usb_ss_mux : 3;
		uint32_t reserved_2 : 23;
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
 * Get the USB SuperSpeed Mux type from SSFC_CONFIG
 *
 * @return the USB SuperSpeed Mux type
 */
enum ec_ssfc_usb_ss_mux get_cbi_ssfc_usb_ss_mux(void);


#endif /* _DEDEDE_CBI_SSFC__H_ */
