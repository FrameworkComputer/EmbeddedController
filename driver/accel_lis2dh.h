/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LIS2DH/LIS2DE/LNG2DM accelerometer module for Chrome EC */

#ifndef __CROS_EC_ACCEL_LIS2DH_H
#define __CROS_EC_ACCEL_LIS2DH_H

#include "driver/stm_mems_common.h"

/*
 * LIS2DH/LIS2DE:
 *
 * 7-bit address is 0011 00X b. Where 'X' is determined
 * by the voltage on the ADDR pin
 */
#define LIS2DH_ADDR0_FLAGS	0x18
#define LIS2DH_ADDR1_FLAGS	0x19

/*
 * LNG2DM:
 *
 * 8-bit address is 0101 00XW b. Where 'X' is determined
 * by the voltage on the ADDR pin, and 'W' is read write bit
 */
#define LNG2DM_ADDR0_FLAGS	0x28
#define LNG2DM_ADDR1_FLAGS	0x29

/* Who Am I  */
#define LIS2DH_WHO_AM_I_REG	0x0f
#define LIS2DH_WHO_AM_I		0x33

/* COMMON DEFINE FOR ACCEL SENSOR */
#define LIS2DH_EN_BIT		0x01
#define LIS2DH_DIS_BIT		0x00

#define LIS2DH_INT2_ON_INT1_ADDR	0x13
#define LIS2DH_INT2_ON_INT1_MASK	0x20

#define LIS2DH_OUT_X_L_ADDR	0x28

#define LIS2DH_CTRL1_ADDR	0x20
#define LIS2DH_INT2_ON_INT1_MASK	0x20
#define LIS2DH_ENABLE_ALL_AXES	0x07

#define LIS2DH_CTRL2_ADDR	0x21
#define LIS2DH_CTRL2_RESET_VAL	0x00

#define LIS2DH_CTRL3_ADDR	0x22
#define LIS2DH_CTRL3_RESET_VAL	0x00

#define LIS2DH_CTRL4_ADDR	0x23
#define LIS2DH_BDU_MASK		0x80

#define LIS2DH_CTRL5_ADDR	0x24
#define LIS2DH_CTRL5_RESET_VAL	0x00

#define LIS2DH_CTRL6_ADDR	0x25
#define LIS2DH_CTRL6_RESET_VAL	0x00

#define LIS2DH_STATUS_REG	0x27
#define LIS2DH_STS_XLDA_UP	0x80

#define LIS2DH_FS_2G_VAL         0x00
#define LIS2DH_FS_4G_VAL         0x01
#define LIS2DH_FS_8G_VAL         0x02
#define LIS2DH_FS_16G_VAL        0x03

/* Interrupt source status register */
#define LIS2DH_INT1_SRC_REG	0x31

/* Output data rate Mask register */
#define LIS2DH_ACC_ODR_MASK	0xf0

/* Acc data rate */
enum lis2dh_odr {
	LIS2DH_ODR_0HZ_VAL = 0,
	LIS2DH_ODR_1HZ_VAL,
	LIS2DH_ODR_10HZ_VAL,
	LIS2DH_ODR_25HZ_VAL,
	LIS2DH_ODR_50HZ_VAL,
	LIS2DH_ODR_100HZ_VAL,
	LIS2DH_ODR_200HZ_VAL,
	LIS2DH_ODR_400HZ_VAL,
	LIS2DH_ODR_LIST_NUM
};

/* Absolute maximum rate for sensor */
#define LIS2DH_ODR_MIN_VAL		1000
#define LIS2DH_ODR_MAX_VAL \
	MOTION_MAX_SENSOR_FREQUENCY(400000, 25000)

/* Return ODR reg value based on data rate set */
#define LIS2DH_ODR_TO_REG(_odr) \
	(_odr <= 1000) ? LIS2DH_ODR_1HZ_VAL : \
	(_odr <= 10000) ? LIS2DH_ODR_10HZ_VAL : \
	((31 - __builtin_clz(_odr / 25000))) + 3

/* Return ODR real value normalized to sensor capabilities */
#define LIS2DH_ODR_TO_NORMALIZE(_odr) \
	(_odr <= 1000) ? 1000 : (_odr <= 10000) ? 10000 : \
	(25000 * (1 << (31 - __builtin_clz(_odr / 25000))))

/* Return ODR real value normalized to sensor capabilities from reg value */
#define LIS2DH_REG_TO_NORMALIZE(_reg) \
	(_reg == LIS2DH_ODR_1HZ_VAL) ? 1000 : \
	(_reg == LIS2DH_ODR_10HZ_VAL) ? 10000 : (25000 * (1 << (_reg - 3)))

/* Full scale range Mask register */
#define LIS2DH_FS_MASK		0x30

/* FS reg value from Full Scale */
#define LIS2DH_FS_TO_REG(_fs) (__fls(_fs) - 1)

/*
 * Sensor resolution in number of bits
 *
 * lis2dh has variable precision (8/10/12 bits) depending Power Mode
 * selected, here Only Normal Power mode supported (10 bits).
 *
 * lis2de/lng2dm only support 8bit resolution.
 */
#if defined(CONFIG_ACCEL_LIS2DE) || defined(CONFIG_ACCEL_LNG2DM)
#define LIS2DH_RESOLUTION       8
#elif defined(CONFIG_ACCEL_LIS2DH)
#define LIS2DH_RESOLUTION      	10
#endif

extern const struct accelgyro_drv lis2dh_drv;

#endif /* __CROS_EC_ACCEL_LIS2DH_H */
