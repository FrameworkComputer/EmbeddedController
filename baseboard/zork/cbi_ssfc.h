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
	SSFC_BASE_GYRO_ICM42607 = 4,
};
#define SSFC_BASE_GYRO_OFFSET 0
#define SSFC_BASE_GYRO_MASK GENMASK(2, 0)

enum ec_ssfc_spkr_auto_mode {
	SSFC_SPKR_AUTO_MODE_OFF = 0,
	SSFC_SPKR_AUTO_MODE_ON = 1,
};
#define SSFC_SPKR_AUTO_MODE_OFFSET 3
#define SSFC_SPKR_AUTO_MODE_MASK GENMASK(3, 3)

/*
 * eDP PHY Alternate Tuning (Bits 4-5)
 */
enum ec_ssfc_edp_phy_alt_tuning {
	SSFC_EDP_PHY_ALT_TUNING_0 = 0,
	SSFC_EDP_PHY_ALT_TUNING_1 = 1,
	SSFC_EDP_PHY_ALT_TUNING_2 = 2,
	SSFC_EDP_PHY_ALT_TUNING_3 = 3,
};
#define SSFC_EDP_PHY_ALT_TUNING_OFFSET 4
#define SSFC_EDP_PHY_ALT_TUNING_MASK GENMASK(5, 4)

/*
 * TypeC port 1 secondary MUX (Bits 6-7)
 */
enum ec_ssfc_c1_mux {
	SSFC_C1_MUX_NONE = 0,
	SSFC_C1_MUX_TUSB544 = 1,
	SSFC_C1_MUX_PS8818 = 2,
};
#define SSFC_C1_MUX_OFFSET 6
#define SSFC_C1_MUX_MASK GENMASK(7, 6)

/**
 * Get the Base sensor type from SSFC_CONFIG.
 *
 * @return the Base sensor board type.
 */
enum ec_ssfc_base_gyro_sensor get_cbi_ssfc_base_sensor(void);

/**
 * Get whether speaker amp auto mode is enabled from SSFC.
 */
enum ec_ssfc_spkr_auto_mode get_cbi_ssfc_spkr_auto_mode(void);

/**
 * Get the eDP PHY alternate tuning from SSFC.
 */
enum ec_ssfc_edp_phy_alt_tuning get_cbi_ssfc_edp_phy_alt_tuning(void);

/**
 * Get the C1 usb mux from SSFC.
 */
enum ec_ssfc_c1_mux get_cbi_ssfc_c1_mux(void);

#endif /* _ZORK_CBI_SSFC__H_ */
