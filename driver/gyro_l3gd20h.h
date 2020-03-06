/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* L3GD20H gyro module for Chrome EC */

#ifndef __CROS_EC_GYRO_L3GD20H_H
#define __CROS_EC_GYRO_L3GD20H_H

#include "accelgyro.h"
#include "task.h"

/*
 * 7-bit address is 110101Xb. Where 'X' is determined
 * by the voltage on the ADDR pin.
 */
#define L3GD20_ADDR0_FLAGS       0x6a
#define L3GD20_ADDR1_FLAGS       0x6b

/* who am I  */
#define L3GD20_WHO_AM_I          0xd7

/* Chip specific registers. */
#define L3GD20_WHO_AM_I_REG      0x0f
#define L3GD20_CTRL_REG1         0x20
#define L3GD20_CTRL_REG2         0x21
#define L3GD20_CTRL_REG3         0x22
#define L3GD20_CTRL_REG4         0x23
#define L3GD20_CTRL_REG5         0x24
#define L3GD20_CTRL_REFERENCE    0x25
#define L3GD20_OUT_TEMP          0x26
#define L3GD20_STATUS_REG        0x27
#define L3GD20_OUT_X_L           0x28
#define L3GD20_OUT_X_H           0x29
#define L3GD20_OUT_Y_L           0x2a
#define L3GD20_OUT_Y_H           0x2b
#define L3GD20_OUT_Z_L           0x2c
#define L3GD20_OUT_Z_H           0x2d
#define L3GD20_FIFO_CTRL_REG     0x2e
#define L3GD20_FIFO_SRC_REG      0x2f
#define L3GD20_INT1_CFG          0x30
#define L3GD20_INT1_SRC          0x31
#define L3GD20_INT1_TSH_XH       0x32
#define L3GD20_INT1_TSH_XL       0x33
#define L3GD20_INT1_TSH_YH       0x34
#define L3GD20_INT1_TSH_YL       0x35
#define L3GD20_INT1_TSH_ZH       0x36
#define L3GD20_INT1_TSH_ZL       0x37
#define L3GD20_INT1_DURATION     0x38
#define L3GD20_LOW_ODR           0x39

#define L3GD20_DPS_SEL_245       (0 << 4)
#define L3GD20_DPS_SEL_500       BIT(4)
#define L3GD20_DPS_SEL_2000_0    (2 << 4)
#define L3GD20_DPS_SEL_2000_1    (3 << 4)

#define L3GD20_ODR_PD            (0 << 3)
#define L3GD20_ODR_12_5HZ        (0 << 6)
#define L3GD20_ODR_25HZ          BIT(6)
#define L3GD20_ODR_50HZ_0        (2 << 6)
#define L3GD20_ODR_50HZ_1        (3 << 6)
#define L3GD20_ODR_100HZ         (0 << 6)
#define L3GD20_ODR_200HZ         BIT(6)
#define L3GD20_ODR_400HZ         (2 << 6)
#define L3GD20_ODR_800HZ         (3 << 6)

#define L3GD20_ODR_MASK          (3 << 6)
#define L3GD20_STS_ZYXDA_MASK    BIT(3)
#define L3GD20_RANGE_MASK        (3 << 4)
#define L3GD20_LOW_ODR_MASK      BIT(0)
#define L3GD20_ODR_PD_MASK       BIT(3)

/* Min and Max sampling frequency in mHz */
#define L3GD20_GYRO_MIN_FREQ     12500
#define L3GD20_GYRO_MAX_FREQ     \
	MOTION_MAX_SENSOR_FREQUENCY(800000, L3GD20_GYRO_MIN_FREQ)

/*
 * Register      : STATUS_REG
 * Address       : 0X27
 */
enum l3gd20_status {
	L3GD20_STS_DOWN          = 0x00,
	L3GD20_STS_ZYXDA_UP      = 0x08,
};

/*
 * Register      : CTRL_REG4
 * Address       : 0X23
 * Bit Group Name: BDU
 */
enum l3gd20_bdu {
	L3GD20_BDU_DISABLE       = 0x00,
	L3GD20_BDU_ENABLE        = 0x80,
};

/* Sensor resolution in number of bits. This sensor has fixed resolution. */
#define L3GD20_RESOLUTION        16

extern const struct accelgyro_drv l3gd20h_drv;
struct l3gd20_data {
	struct accelgyro_saved_data_t base;
	int16_t offset[3];
};

#endif /* __CROS_EC_GYRO_L3GD20H_H */
