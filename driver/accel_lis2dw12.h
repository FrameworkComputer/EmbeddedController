/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * LIS2DW12 accelerometer include file for Chrome EC 3D digital accelerometer.
 * For more details on LIS2DW12 device please refer to www.st.com.
 */

#ifndef __CROS_EC_ACCEL_LIS2DW12_H
#define __CROS_EC_ACCEL_LIS2DW12_H

#include "driver/stm_mems_common.h"

/*
 * 7-bit address is 011000Xb. Where 'X' is determined
 * by the voltage on the ADDR pin.
 */
#define LIS2DW12_ADDR0			0x18
#define LIS2DW12_ADDR1			0x19

#define LIS2DWL_ADDR0_FLAGS		0x18
#define LIS2DWL_ADDR1_FLAGS		0x19

#define LIS2DW12_EN_BIT			0x01
#define LIS2DW12_DIS_BIT		0x00

/* Who am I. */
#define LIS2DW12_WHO_AM_I_REG		0x0f
#define LIS2DW12_WHO_AM_I		0x44

/* Registers sensor. */
#define LIS2DW12_CTRL1_ADDR		0x20
#define LIS2DW12_CTRL2_ADDR		0x21
#define LIS2DW12_CTRL3_ADDR		0x22

#define LIS2DW12_CTRL4_ADDR		0x23

/* CTRL4 bits. */
#define LIS2DW12_INT1_FTH		0x02
#define LIS2DW12_INT1_D_TAP		0x08
#define LIS2DW12_INT1_S_TAP		0x40

#define LIS2DW12_CTRL5_ADDR		0x24

/* CTRL5 bits. */
#define LIS2DW12_INT2_FTH		0x02

#define LIS2DW12_CTRL6_ADDR		0x25
#define LIS2DW12_STATUS_REG		0x27

/* STATUS bits. */
#define LIS2DW12_STS_DRDY_UP		0x01
#define LIS2DW12_SINGLE_TAP_UP		0x08
#define LIS2DW12_DOUBLE_TAP_UP		0x10
#define LIS2DW12_FIFO_THS_UP		0x80

#define LIS2DW12_OUT_X_L_ADDR		0x28

#define LIS2DW12_FIFO_CTRL_ADDR		0x2e

/* FIFO_CTRL bits. */
#define LIS2DW12_FIFO_MODE_MASK		0xe0

/* List of supported FIFO mode. */
enum lis2dw12_fmode {
	LIS2DW12_FIFO_BYPASS_MODE = 0,
	LIS2DW12_FIFO_MODE,
	LIS2DW12_FIFO_CONT_MODE = 6
};

#define LIS2DW12_FIFO_THRESHOLD_MASK	0x1f

#define LIS2DW12_FIFO_SAMPLES_ADDR	0x2f
#define LIS2DW12_TAP_THS_X_ADDR		0x30
#define LIS2DW12_TAP_THS_Y_ADDR		0x31
#define LIS2DW12_TAP_THS_Z_ADDR		0x32
#define LIS2DW12_INT_DUR_ADDR		0x33

#define LIS2DW12_WAKE_UP_THS_ADDR	0x34

/* TAP bits. */
#define LIS2DW12_SINGLE_DOUBLE_TAP	0x80

/* FIFO_SAMPLES bits. */
#define LIS2DW12_FIFO_DIFF_MASK		0x3f
#define LIS2DW12_FIFO_OVR_MASK		0x40
#define LIS2DW12_FIFO_FTH_MASK		0x80

#define LIS2DW12_ABS_INT_CFG_ADDR	0x3f

/* INT Configuration bits. */
#define LIS2DW12_DRDY_PULSED		0x80
#define LIS2DW12_INT2_ON_INT1		0x40
#define LIS2DW12_INT_ENABLE		0x20

/* Alias Registers/Masks. */
#define LIS2DW12_ACC_ODR_ADDR		LIS2DW12_CTRL1_ADDR
#define LIS2DW12_ACC_ODR_MASK		0xf0

#define LIS2DW12_ACC_MODE_ADDR		LIS2DW12_CTRL1_ADDR
#define LIS2DW12_ACC_MODE_MASK		0x0c

/* Power mode selection. */
enum lis2sw12_mode {
	LIS2DW12_LOW_POWER = 0,
	LIS2DW12_HIGH_PERF,
	LIS2DW12_SINGLE_DC,
	LIS2DW12_LOW_POWER_LIST_NUM
};

#define LIS2DW12_ACC_LPMODE_ADDR	LIS2DW12_CTRL1_ADDR
#define LIS2DW12_ACC_LPMODE_MASK	0x03

/*
 * Low power mode selection.
 * TODO: Support all Low Power Mode. Actually is not supported only
 * LOW_POWER_MODE_1.
 */
enum lis2sw12_lpmode {
	LIS2DW12_LOW_POWER_MODE_1 = 0,
	LIS2DW12_LOW_POWER_MODE_2,
	LIS2DW12_LOW_POWER_MODE_3,
	LIS2DW12_LOW_POWER_MODE_4,
	LIS2DW12_LOW_POWER_MODE_LIST_NUM
};

#define LIS2DW12_BDU_ADDR		LIS2DW12_CTRL2_ADDR
#define LIS2DW12_BDU_MASK		0x08

#define LIS2DW12_SOFT_RESET_ADDR	LIS2DW12_CTRL2_ADDR
#define LIS2DW12_SOFT_RESET_MASK	0x40

#define LIS2DW12_BOOT_ADDR		LIS2DW12_CTRL2_ADDR
#define LIS2DW12_BOOT_MASK		0x80

#define LIS2DW12_LIR_ADDR		LIS2DW12_CTRL3_ADDR
#define LIS2DW12_LIR_MASK		0x10

#define LIS2DW12_H_ACTIVE_ADDR		LIS2DW12_CTRL3_ADDR
#define LIS2DW12_H_ACTIVE_MASK		0x08

#define LIS2DW12_INT1_FTH_ADDR		LIS2DW12_CTRL4_ADDR
#define LIS2DW12_INT1_FTH_MASK		LIS2DW12_INT1_FTH

#define LIS2DW12_INT1_TAP_ADDR		LIS2DW12_CTRL4_ADDR
#define LIS2DW12_INT1_DTAP_MASK		0x08
#define LIS2DW12_INT1_STAP_MASK		0x40

#define LIS2DW12_INT1_D_TAP_EN		LIS2DW12_INT1_DTAP_MASK

#define LIS2DW12_STATUS_TAP		LIS2DW12_STS_DRDY_UP
#define LIS2DW12_SINGLE_TAP		LIS2DW12_SINGLE_TAP_UP
#define LIS2DW12_DOUBLE_TAP		LIS2DW12_DOUBLE_TAP_UP

#define LIS2DW12_INT2_ON_INT1_ADDR	LIS2DW12_ABS_INT_CFG_ADDR
#define LIS2DW12_INT2_ON_INT1_MASK	LIS2DW12_INT2_ON_INT1

#define LIS2DW12_DRDY_PULSED_ADDR	LIS2DW12_ABS_INT_CFG_ADDR
#define LIS2DW12_DRDY_PULSED_MASK	LIS2DW12_DRDY_PULSED

/* Acc data rate for HR mode. */
enum lis2dw12_odr {
	LIS2DW12_ODR_POWER_OFF_VAL = 0x00,
	LIS2DW12_ODR_12HZ_VAL = 0x02,
	LIS2DW12_ODR_25HZ_VAL,
	LIS2DW12_ODR_50HZ_VAL,
	LIS2DW12_ODR_100HZ_VAL,
	LIS2DW12_ODR_200HZ_VAL,
	LIS2DW12_ODR_400HZ_VAL,
	LIS2DW12_ODR_800HZ_VAL,
	LIS2DW12_ODR_1_6kHZ_VAL,
	LIS2DW12_ODR_LIST_NUM
};

/* Absolute Acc rate. */
#define LIS2DW12_ODR_MIN_VAL		12500
#define LIS2DW12_ODR_MAX_VAL		\
	MOTION_MAX_SENSOR_FREQUENCY(1600000, LIS2DW12_ODR_MIN_VAL)


/* Full scale range registers. */
#define LIS2DW12_FS_ADDR		LIS2DW12_CTRL6_ADDR
#define LIS2DW12_FS_MASK		0x30

/* Acc FS value. */
enum lis2dw12_fs {
	LIS2DW12_FS_2G_VAL = 0x00,
	LIS2DW12_FS_4G_VAL,
	LIS2DW12_FS_8G_VAL,
	LIS2DW12_FS_16G_VAL,
	LIS2DW12_FS_LIST_NUM
};

#define LIS2DW12_ACCEL_FS_MAX_VAL	16

/* Acc Gain value. */
#define LIS2DW12_FS_2G_GAIN		3904
#define LIS2DW12_FS_4G_GAIN		(LIS2DW12_FS_2G_GAIN << 1)
#define LIS2DW12_FS_8G_GAIN		(LIS2DW12_FS_2G_GAIN << 2)
#define LIS2DW12_FS_16G_GAIN		(LIS2DW12_FS_2G_GAIN << 3)

/* FS Full Scale value from Gain. */
#define LIS2DW12_GAIN_FS(_gain) \
	(2 << (31 - __builtin_clz(_gain / LIS2DW12_FS_2G_GAIN)))

/* Gain value from selected Full Scale. */
#define LIS2DW12_FS_GAIN(_fs) \
	(LIS2DW12_FS_2G_GAIN << (30 - __builtin_clz(_fs)))

/* Reg value from Full Scale. */
#define LIS2DW12_FS_REG(_fs) \
	(30 - __builtin_clz(_fs))

/* Normalized FS value from Full Scale. */
#define LIS2DW12_NORMALIZE_FS(_fs) \
	(1 << (30 - __builtin_clz(_fs)))

/*
 * Sensor resolution in number of bits.
 * Sensor driver support 14 bits resolution.
 * TODO: Support all "LP Power Mode" (res. 12/14 bits).
 */
#define LIS2DW12_RESOLUTION		14

extern const struct accelgyro_drv lis2dw12_drv;

void lis2dw12_interrupt(enum gpio_signal signal);

#endif /* __CROS_EC_ACCEL_LIS2DW12_H */
