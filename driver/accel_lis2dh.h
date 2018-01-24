/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LIS2DH accelerometer module for Chrome EC */

#ifndef __CROS_EC_ACCEL_LIS2DH_H
#define __CROS_EC_ACCEL_LIS2DH_H

#define LIS2DH_I2C_ADDR(__x)	(__x << 1)

/* 7-bit address is 000110Xb. Where 'X' is determined
 * by the voltage on the ADDR pin
 */
#define LIS2DH_ADDR0		LIS2DH_I2C_ADDR(0x18)
#define LIS2DH_ADDR1		LIS2DH_I2C_ADDR(0x19)

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

#ifdef CONFIG_ACCEL_FIFO

/* FIFO regs, masks and define */
#define LIS2DH_FIFO_WTM_INT_MASK	0x04
#define LIS2DH_FIFO_CTRL_REG	0x2e
#define LIS2DH_FIFO_MODE_MASK	0xc0
#define LIS2DH_FIFO_THR_MASK	0x1f

/* Select FIFO supported mode:
 * BYPASS - Bypass FIFO
 * FIFO - FIFO mode collect data
 * STREAM - FIFO older data is replaced by new data
 * SFIFO - Stream-to-FIFO mode. Mix FIFO & STREAM
 */
enum lis2dh_fifo_modes {
	LIS2DH_FIFO_BYPASS_MODE = 0x00,
	LIS2DH_FIFO_MODE,
	LIS2DH_FIFO_STREAM_MODE,
	LIS2DH_FIFO_SFIFO_MODE
};

/* Defines for LIS2DH_CTRL5_ADDR FIFO register */
#define LIS2DH_FIFO_EN_MASK	0x40

#define LIS2DH_FIFO_SRC_REG	0x2f
#define LIS2DH_FIFO_EMPTY_FLAG	0x20
#define LIS2DH_FIFO_UNREAD_MASK	0x1f
#endif /* CONFIG_ACCEL_FIFO */

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
#define LIS2DH_ODR_MAX_VAL		400000

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

/* Sensor resolution in number of bits
 * This sensor has variable precision (8/10/12 bits) depending Power Mode
 * selected
 * TODO: Only Normal Power mode supported (10 bits)
 */
#define LIS2DH_RESOLUTION      	10

extern const struct accelgyro_drv lis2dh_drv;

void lis2dh_interrupt(enum gpio_signal signal);

#endif /* __CROS_EC_ACCEL_LIS2DH_H */
