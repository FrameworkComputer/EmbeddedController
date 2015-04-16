/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LSM6DS0 accelerometer and gyro module for Chrome EC */

#ifndef __CROS_EC_ACCEL_LSM6DS0_H
#define __CROS_EC_ACCEL_LSM6DS0_H

#include "task.h"

/*
 * 7-bit address is 110101Xb. Where 'X' is determined
 * by the voltage on the ADDR pin.
 */
#define LSM6DS0_ADDR0             0xd4
#define LSM6DS0_ADDR1             0xd6

/* who am I  */
#define LSM6DS0_WHO_AM_I          0x68

/* Chip specific registers. */
#define LSM6DS0_ACT_THS           0x04
#define LSM6DS0_ACT_DUR           0x05
#define LSM6DS0_INT_GEN_CFG_XL    0x06
#define LSM6DS0_INT_GEN_THS_X_XL  0x07
#define LSM6DS0_INT_GEN_THS_Y_XL  0x08
#define LSM6DS0_INT_GEN_THS_Z_XL  0x09
#define LSM6DS0_INT_GEN_DUR_XL    0x0a
#define LSM6DS0_REFERENCE_G       0x0b
#define LSM6DS0_INT_CTRL          0x0c
#define LSM6DS0_WHO_AM_I_REG      0x0f
#define LSM6DS0_CTRL_REG1_G       0x10
#define LSM6DS0_CTRL_REG2_G       0x11
#define LSM6DS0_CTRL_REG3_G       0x12
#define LSM6DS0_ORIENT_CFG_G      0x13
#define LSM6DS0_INT_GEN_SRC_G     0x14
#define LSM6DS0_OUT_TEMP_L        0x15
#define LSM6DS0_OUT_TEMP_H        0x16
#define LSM6DS0_OUT_X_L_G         0x18
#define LSM6DS0_OUT_X_H_G         0x19
#define LSM6DS0_OUT_Y_L_G         0x1a
#define LSM6DS0_OUT_Y_H_G         0x1b
#define LSM6DS0_OUT_Z_L_G         0x1c
#define LSM6DS0_OUT_Z_H_G         0x1d
#define LSM6DS0_CTRL_REG4         0x1e
#define LSM6DS0_CTRL_REG5_XL      0x1f
#define LSM6DS0_CTRL_REG6_XL      0x20
#define LSM6DS0_CTRL_REG7_XL      0x21
#define LSM6DS0_CTRL_REG8         0x22
#define LSM6DS0_CTRL_REG9         0x23
#define LSM6DS0_CTRL_REG10        0x24
#define LSM6DS0_INT_GEN_SRC_XL    0x26
#define LSM6DS0_STATUS_REG        0x27
#define LSM6DS0_OUT_X_L_XL        0x28
#define LSM6DS0_OUT_X_H_XL        0x29
#define LSM6DS0_OUT_Y_L_XL        0x2a
#define LSM6DS0_OUT_Y_H_XL        0x2b
#define LSM6DS0_OUT_Z_L_XL        0x2c
#define LSM6DS0_OUT_Z_H_XL        0x2d
#define LSM6DS0_FIFO_CTRL         0x2e
#define LSM6DS0_FIFO_SRC          0x2f
#define LSM6DS0_INT_GEN_CFG_G     0x30
#define LSM6DS0_INT_GEN_THS_XH_G  0x31
#define LSM6DS0_INT_GEN_THS_XL_G  0x32
#define LSM6DS0_INT_GEN_THS_YH_G  0x33
#define LSM6DS0_INT_GEN_THS_YL_G  0x34
#define LSM6DS0_INT_GEN_THS_ZH_G  0x35
#define LSM6DS0_INT_GEN_THS_ZL_G  0x36
#define LSM6DS0_INT_GEN_DUR_G     0x37

#define LSM6DS0_DPS_SEL_245     (0 << 3)
#define LSM6DS0_DPS_SEL_500     (1 << 3)
#define LSM6DS0_DPS_SEL_1000    (2 << 3)
#define LSM6DS0_DPS_SEL_2000    (3 << 3)
#define LSM6DS0_GSEL_2G         (0 << 3)
#define LSM6DS0_GSEL_4G         (2 << 3)
#define LSM6DS0_GSEL_8G         (3 << 3)

#define LSM6DS0_RANGE_MASK      (3 << 3)

#define LSM6DS0_ODR_PD          (0 << 5)
#define LSM6DS0_ODR_10HZ        (1 << 5)
#define LSM6DS0_ODR_15HZ        (1 << 5)
#define LSM6DS0_ODR_50HZ        (2 << 5)
#define LSM6DS0_ODR_59HZ        (2 << 5)
#define LSM6DS0_ODR_119HZ       (3 << 5)
#define LSM6DS0_ODR_238HZ       (4 << 5)
#define LSM6DS0_ODR_476HZ       (5 << 5)
#define LSM6DS0_ODR_952HZ       (6 << 5)

#define LSM6DS0_ODR_MASK        (7 << 5)

/*
 * Register      : STATUS_REG
 * Address       : 0X27
 */
enum lsm6ds0_status {
	LSM6DS0_STS_DOWN                = 0x00,
	LSM6DS0_STS_XLDA_UP             = 0x01,
	LSM6DS0_STS_GDA_UP              = 0x02,
};
#define         LSM6DS0_STS_XLDA_MASK       0x01
#define         LSM6DS0_STS_GDA_MASK        0x02

/*
 * Register      : CTRL_REG8
 * Address       : 0X22
 * Bit Group Name: BDU
 */
enum lsm6ds0_bdu {
	LSM6DS0_BDU_DISABLE              = 0x00,
	LSM6DS0_BDU_ENABLE               = 0x40,
};
/* Sensor resolution in number of bits. This sensor has fixed resolution. */
#define LSM6DS0_RESOLUTION      16

/* Run-time configurable parameters */
struct lsm6ds0_data {
	/* Current range */
	int sensor_range;
	/* Current output data rate */
	int sensor_odr;
};

extern const struct accelgyro_drv lsm6ds0_drv;

#endif /* __CROS_EC_ACCEL_LSM6DS0_H */
