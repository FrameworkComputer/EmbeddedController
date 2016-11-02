/* Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LSM6DSM Accel and Gyro driver for Chrome EC */

#ifndef __CROS_EC_ACCELGYRO_LSM6DSM_H
#define __CROS_EC_ACCELGYRO_LSM6DSM_H

#include "accelgyro.h"

#define LSM6DSM_I2C_ADDR(__x)		(__x << 1)

/*
 * 7-bit address is 110101Xb. Where 'X' is determined
 * by the voltage on the ADDR pin.
 */
#define LSM6DSM_ADDR0             	LSM6DSM_I2C_ADDR(0x6a)
#define LSM6DSM_ADDR1             	LSM6DSM_I2C_ADDR(0x6b)

/* Who Am I */
#define LSM6DSM_WHO_AM_I_REG		0x0f
#define LSM6DSM_WHO_AM_I          	0x6a

#define LSM6DSM_OUT_XYZ_SIZE 		6

/* Sensor Software Reset Bit */
#define LSM6DSM_RESET_ADDR		0x12
#define LSM6DSM_RESET_MASK		0x01

/* COMMON DEFINE FOR ACCEL-GYRO SENSORS */
#define LSM6DSM_EN_BIT			0x01
#define LSM6DSM_DIS_BIT			0x00

#define LSM6DSM_LIR_ADDR		0x58
#define LSM6DSM_LIR_MASK		0x01

#define LSM6DSM_BDU_ADDR		0x12
#define LSM6DSM_BDU_MASK		0x40

#define LSM6DSM_INT2_ON_INT1_ADDR	0x13
#define LSM6DSM_INT2_ON_INT1_MASK	0x20

#define LSM6DSM_GYRO_OUT_X_L_ADDR	0x22
#define LSM6DSM_ACCEL_OUT_X_L_ADDR	0x28

#define LSM6DSM_CTRL1_ADDR		0x10
#define LSM6DSM_CTRL2_ADDR		0x11
#define LSM6DSM_CTRL3_ADDR		0x12
#define LSM6DSM_CTRL6_ADDR		0x15
#define LSM6DSM_CTRL7_ADDR		0x16

#define LSM6DSM_STATUS_REG		0x1e

/* Output data rate registers and masks */
#define LSM6DSM_ODR_REG(_sensor) \
	(LSM6DSM_CTRL1_ADDR + _sensor)
#define LSM6DSM_ODR_MASK		0xf0

/* Common Acc/Gyro data rate */
enum lsm6dsm_odr {
	LSM6DSM_ODR_POWER_OFF_VAL = 0x00,
	LSM6DSM_ODR_13HZ_VAL,
	LSM6DSM_ODR_26HZ_VAL,
	LSM6DSM_ODR_52HZ_VAL,
	LSM6DSM_ODR_104HZ_VAL,
	LSM6DSM_ODR_208HZ_VAL,
	LSM6DSM_ODR_416HZ_VAL,
	LSM6DSM_ODR_833HZ_VAL,
	LSM6DSM_ODR_LIST_NUM
};

/* Absolute maximum rate for acc and gyro sensors */
#define LSM6DSM_ODR_MIN_VAL		13000
#define LSM6DSM_ODR_MAX_VAL		833000

/* ODR reg value from selected data rate in mHz */
#define LSM6DSM_ODR_TO_REG(_odr) \
	(31 - __builtin_clz(_odr / LSM6DSM_ODR_MIN_VAL))

/* normalized ODR value from selected data rate in mHz */
#define LSM6DSM_ODR_TO_NORMALIZE(_odr) \
	(LSM6DSM_ODR_MIN_VAL * (_odr / LSM6DSM_ODR_MIN_VAL))

/* Full Scale range value for Accel */
#define LSM6DSM_FS_LIST_NUM		4

#define LSM6DSM_ACCEL_FS_ADDR		0x10
#define LSM6DSM_ACCEL_FS_MASK		0x0c

#define LSM6DSM_ACCEL_FS_2G_VAL		0x00
#define LSM6DSM_ACCEL_FS_4G_VAL		0x02
#define LSM6DSM_ACCEL_FS_8G_VAL		0x03
#define LSM6DSM_ACCEL_FS_16G_VAL	0x01

#define LSM6DSM_ACCEL_FS_2G_GAIN	61
#define LSM6DSM_ACCEL_FS_4G_GAIN	122
#define LSM6DSM_ACCEL_FS_8G_GAIN	244
#define LSM6DSM_ACCEL_FS_16G_GAIN	488

#define LSM6DSM_ACCEL_FS_MAX_VAL	16

/* Accel Gain value from selected Full Scale */
#define LSM6DSM_ACCEL_FS_GAIN(_fs) \
	(_fs == 16 ? LSM6DSM_ACCEL_FS_16G_GAIN : \
	LSM6DSM_ACCEL_FS_2G_GAIN << (31 - __builtin_clz(_fs / 2)))

/* Accel FS Full Scale value from Gain */
#define LSM6DSM_ACCEL_GAIN_FS(_gain) \
	(1 << (32 - __builtin_clz(_gain / LSM6DSM_ACCEL_FS_2G_GAIN)))

/* Accel Reg value from Full Scale */
#define LSM6DSM_ACCEL_FS_REG(_fs) \
	(_fs == 2 ? LSM6DSM_ACCEL_FS_2G_VAL : \
	_fs == 16 ? LSM6DSM_ACCEL_FS_16G_VAL : \
	(32 - __builtin_clz(_fs / 2)))

/* Accel normalized FS value from Full Scale */
#define LSM6DSM_ACCEL_NORMALIZE_FS(_fs) \
	(1 << (32 - __builtin_clz(_fs / 2)))

/* Full Scale range value for Gyro */
#define LSM6DSM_GYRO_FS_ADDR		0x11
#define LSM6DSM_GYRO_FS_MASK		0x0c

#define LSM6DSM_GYRO_FS_245_VAL		0x00
#define LSM6DSM_GYRO_FS_500_VAL		0x01
#define LSM6DSM_GYRO_FS_1000_VAL	0x02
#define LSM6DSM_GYRO_FS_2000_VAL	0x03

#define LSM6DSM_GYRO_FS_245_GAIN	8750
#define LSM6DSM_GYRO_FS_500_GAIN	17500
#define LSM6DSM_GYRO_FS_1000_GAIN	35000
#define LSM6DSM_GYRO_FS_2000_GAIN	70000

#define LSM6DSM_GYRO_FS_MAX_VAL		20000

/* Gyro FS Gain value from selected Full Scale */
#define LSM6DSM_GYRO_FS_GAIN(_fs) \
	(LSM6DSM_GYRO_FS_245_GAIN << (31 - __builtin_clz(_fs / 245)))

/* Gyro FS Full Scale value from Gain */
#define LSM6DSM_GYRO_GAIN_FS(_gain) \
	(_gain == LSM6DSM_GYRO_FS_245_GAIN ? 245 : \
	500 << (30 - __builtin_clz(_gain / LSM6DSM_GYRO_FS_245_GAIN)))

/* Gyro Reg value from Full Scale */
#define LSM6DSM_GYRO_FS_REG(_fs) \
	((31 - __builtin_clz(_fs / 245)))

/* Gyro normalized FS value from Full Scale: for Gyro Gains are not multiple */
#define LSM6DSM_GYRO_NORMALIZE_FS(_fs) \
	(_fs == 245 ? 245 : 500 << (31 - __builtin_clz(_fs / 500)))

/* FS register address/mask for Acc/Gyro sensors */
#define LSM6DSM_RANGE_REG(_sensor)  	(LSM6DSM_ACCEL_FS_ADDR + (_sensor))
#define LSM6DSM_RANGE_MASK  		0x0c

/* Status register bitmask for Acc/Gyro data ready */
enum lsm6dsm_status {
	LSM6DSM_STS_DOWN = 0x00,
	LSM6DSM_STS_XLDA_UP = 0x01,
	LSM6DSM_STS_GDA_UP = 0x02
};

#define LSM6DSM_STS_XLDA_MASK		0x01
#define LSM6DSM_STS_GDA_MASK		0x02

/* Sensor resolution in number of bits. This sensor has fixed resolution. */
#define LSM6DSM_RESOLUTION      	16

extern const struct accelgyro_drv lsm6dsm_drv;

struct lsm6dsm_data {
	struct accelgyro_saved_data_t base;
	int16_t offset[3];
};

#endif /* __CROS_EC_ACCELGYRO_LSM6DSM_H */
