/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LIS2DS accelerometer module for Chrome EC */

#ifndef __CROS_EC_ACCEL_LIS2DS_H
#define __CROS_EC_ACCEL_LIS2DS_H

#include "driver/stm_mems_common.h"

/*
 * 7-bit address is 110101Xb. Where 'X' is determined
 * by the voltage on the ADDR pin.
 */
#define LIS2DS_ADDR0_FLAGS 0x1a
#define LIS2DS_ADDR1_FLAGS 0x1e

/* who am I  */
#define LIS2DS_WHO_AM_I_REG 0x0f
#define LIS2DS_WHO_AM_I 0x43

/* X, Y, Z axis data len */
#define LIS2DS_OUT_XYZ_SIZE 6

/* COMMON DEFINE FOR ACCEL SENSOR */
#define LIS2DS_EN_BIT 0x01
#define LIS2DS_DIS_BIT 0x00

#define LIS2DS_CTRL1_ADDR 0x20
#define LIS2DS_CTRL2_ADDR 0x21
#define LIS2DS_CTRL3_ADDR 0x22
#define LIS2DS_TAP_X_EN 0x20
#define LIS2DS_TAP_Y_EN 0x10
#define LIS2DS_TAP_Z_EN 0x08
#define LIS2DS_TAP_EN_MASK (LIS2DS_TAP_X_EN | LIS2DS_TAP_Y_EN | LIS2DS_TAP_Z_EN)
#define LIS2DS_TAP_EN_ALL 0x07

#define LIS2DS_CTRL4_ADDR 0x23
#define LIS2DS_INT1_FTH 0x02
#define LIS2DS_INT1_D_TAP 0x08
#define LIS2DS_INT1_S_TAP 0x40

#define LIS2DS_CTRL5_ADDR 0x24
#define LIS2DS_FIFO_CTRL_ADDR 0x25
#define LIS2DS_FIFO_MODE_MASK 0xe0
#define LIS2DS_FIFO_BYPASS_MODE 0
#define LIS2DS_FIFO_MODE 1
#define LIS2DS_FIFO_CONT_MODE 6

#define LIS2DS_STATUS_REG 0x27
#define LIS2DS_STS_XLDA_UP 0x01
#define LIS2DS_SINGLE_TAP_UP 0x08
#define LIS2DS_DOUBLE_TAP_UP 0x10
#define LIS2DS_FIFO_THS_UP 0x80

#define LIS2DS_OUT_X_L_ADDR 0x28
#define LIS2DS_FIFO_THS_ADDR 0x2e

#define LIS2DS_FIFO_SRC_ADDR 0x2f
#define LIS2DS_FIFO_DIFF_MASK 0xff
#define LIS2DS_FIFO_DIFF8_MASK 0x20
#define LIS2DS_FIFO_OVR_MASK 0x40
#define LIS2DS_FIFO_FTH_MASK 0x80

/*
 * Concatenated with DIFF8 bit in FIFO_SRC (2Fh) register, it represents the
 * number of unread samples stored in FIFO. (000000000 = FIFO empty;
 * 100000000 = FIFO full, 256 unread samples).
 */
#define LIS2DS_FIFO_SAMPLES_ADDR 0x30
#define LIS2DS_TAP_6D_THS_ADDR 0x31
#define LIS2DS_INT_DUR_ADDR 0x32
#define LIS2DS_WAKE_UP_THS_ADDR 0x33

#define LIS2DS_TAP_SRC_ADDR 0x38
#define LIS2DS_TAP_EVENT_DETECT 0x40

/* Alias Register/Mask */
#define LIS2DS_ACC_ODR_ADDR LIS2DS_CTRL1_ADDR
#define LIS2DS_ACC_ODR_MASK 0xf0

#define LIS2DS_BDU_ADDR LIS2DS_CTRL1_ADDR
#define LIS2DS_BDU_MASK 0x01

#define LIS2DS_SOFT_RESET_ADDR LIS2DS_CTRL2_ADDR
#define LIS2DS_SOFT_RESET_MASK 0x40

#define LIS2DS_LIR_ADDR LIS2DS_CTRL3_ADDR
#define LIS2DS_LIR_MASK 0x04

#define LIS2DS_H_ACTIVE_ADDR LIS2DS_CTRL3_ADDR
#define LIS2DS_H_ACTIVE_MASK 0x02

#define LIS2DS_INT1_FTH_ADDR LIS2DS_CTRL4_ADDR
#define LIS2DS_INT1_FTH_MASK 0x02

#define LIS2DS_INT2_ON_INT1_ADDR LIS2DS_CTRL5_ADDR
#define LIS2DS_INT2_ON_INT1_MASK 0x20

#define LIS2DS_DRDY_PULSED_ADDR LIS2DS_CTRL5_ADDR
#define LIS2DS_DRDY_PULSED_MASK 0x80

/* Acc data rate for HR mode */
enum lis2ds_odr {
	LIS2DS_ODR_POWER_OFF_VAL = 0x00,
	LIS2DS_ODR_12HZ_VAL,
	LIS2DS_ODR_25HZ_VAL,
	LIS2DS_ODR_50HZ_VAL,
	LIS2DS_ODR_100HZ_VAL,
	LIS2DS_ODR_200HZ_VAL,
	LIS2DS_ODR_400HZ_VAL,
	LIS2DS_ODR_800HZ_VAL,
	LIS2DS_ODR_LIST_NUM
};

/* Absolute Acc rate */
#define LIS2DS_ODR_MIN_VAL 12500
#define LIS2DS_ODR_MAX_VAL \
	MOTION_MAX_SENSOR_FREQUENCY(800000, LIS2DS_ODR_MIN_VAL)

/* ODR reg value from selected data rate in mHz */
#define LIS2DS_ODR_TO_REG(_odr) (__fls(_odr / LIS2DS_ODR_MIN_VAL) + 1)

/* Normalized ODR value from selected ODR register value */
#define LIS2DS_REG_TO_ODR(_reg) \
	(LIS2DS_ODR_MIN_VAL << (_reg - LIS2DS_ODR_12HZ_VAL))

/* Full scale range registers */
#define LIS2DS_FS_ADDR LIS2DS_CTRL1_ADDR
#define LIS2DS_FS_MASK 0x0c

/* Acc FS value */
enum lis2ds_fs {
	LIS2DS_FS_2G_VAL = 0x00,
	LIS2DS_FS_16G_VAL,
	LIS2DS_FS_4G_VAL,
	LIS2DS_FS_8G_VAL,
	LIS2DS_FS_LIST_NUM
};

#define LIS2DS_ACCEL_FS_MAX_VAL 16
#define LIS2DS_ACCEL_FS_MIN_VAL 2

/* Reg value from Full Scale */
#define LIS2DS_FS_REG(_fs)               \
	(_fs == 2  ? LIS2DS_FS_2G_VAL :  \
	 _fs == 16 ? LIS2DS_FS_16G_VAL : \
		     __fls(_fs))

/*
 * Sensor resolution in number of bits. Sensor has two resolution:
 * 10 and 14 bit for LP and HR mode resp.
 */
#define LIS2DS_RESOLUTION 16

extern const struct accelgyro_drv lis2ds_drv;

void lis2ds_interrupt(enum gpio_signal signal);

#if defined(CONFIG_ZEPHYR)
#if DT_NODE_EXISTS(DT_ALIAS(lis2ds_int))
/* Get the motion sensor ID of the LIS2DS12 sensor that generates the
 * interrupt. The interrupt is converted to the event and transferred to
 * motion sense task that actually handles the interrupt.
 *
 * Here we use an alias (lis2ds_int) to get the motion sensor ID. This alias
 * MUST be defined for this driver to work.
 * aliases {
 *   lis2ds-int = &lid_accel;
 * };
 */
#define CONFIG_ACCEL_LIS2DS_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(SENSOR_ID(DT_ALIAS(lis2ds_int)))
#endif
#endif

#endif /* __CROS_EC_ACCEL_LIS2DS_H */
