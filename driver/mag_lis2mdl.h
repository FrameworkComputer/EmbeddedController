/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LIS2MSL magnetometer module for Chrome EC */

#ifndef __CROS_EC_MAG_LIS2MDL_H
#define __CROS_EC_MAG_LIS2MDL_H

#include "accelgyro.h"
#include "mag_cal.h"
#include "stm_mems_common.h"

#define LIS2MDL_I2C_ADDR(__x)		(__x << 1)

/*
 * 7-bit address is 0011110Xb. Where 'X' is determined
 * by the voltage on the ADDR pin
 */
#define LIS2MDL_ADDR0			LIS2MDL_I2C_ADDR(0x1e)
#define LIS2MDL_ADDR1			LIS2MDL_I2C_ADDR(0x1f)

/* Registers */
#define LIS2MDL_WHO_AM_I_REG		0x4f
#define LIS2MDL_WHO_AM_I		0x40

#define LIS2MDL_CFG_REG_A_ADDR		0x60
#define LIS2MDL_SW_RESET		0x20
#define LIS2MDL_ODR_100HZ		0xc
#define LIS2MDL_CONT_MODE		0x0

#define LIS2MDL_STATUS_REG		0x67
#define LIS2MDL_OUT_REG			0x68

#define	LIS2DSL_RESOLUTION		16
/*
 * Maximum sensor data range (milligauss):
 * Spec is 1.5 mguass / LSB, so 0.15 uT / LSB.
 * Calibration code is set to 16LSB/ut, [0.0625 uT/LSB]
 * Apply a multiplier to change the unit
 */
#define LIS2MDL_RATIO(_in) (((_in) * 24) / 10)


struct lis2mdl_private_data {
	/* lsm6dsm_data union requires cal be first element */
	struct mag_cal_t cal;
#ifdef CONFIG_MAG_BMI160_LIS2MDL
	intv3_t          hn;   /* last sample for offset compensation */
	int              hn_valid;
#endif
};


#define LIS2MDL_ODR_MIN_VAL	10000
#define LIS2MDL_ODR_MAX_VAL	50000
#if (CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ <= LIS2MDL_ODR_MAX_VAL)
#error "EC too slow for magnetometer"
#endif

void lis2mdl_normalize(const struct motion_sensor_t *s,
		       intv3_t v,
		       uint8_t *data);

extern const struct accelgyro_drv lis2mdl_drv;

#endif /* __CROS_EC_MAG_LIS2MDL_H */

