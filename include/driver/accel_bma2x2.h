/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BMA2x2 gsensor module for Chrome EC */

#ifndef __CROS_EC_ACCEL_BMA2x2_H
#define __CROS_EC_ACCEL_BMA2x2_H

#include "accel_bma2x2_public.h"

/*** Chip-specific registers ***/
/* REGISTER ADDRESS DEFINITIONS */
#define BMA2x2_EEP_OFFSET 0x16
#define BMA2x2_IMAGE_BASE 0x38
#define BMA2x2_IMAGE_LEN 22
#define BMA2x2_CHIP_ID_ADDR 0x00
#define BMA255_CHIP_ID_MAJOR 0xfa

/* DATA ADDRESS DEFINITIONS */
#define BMA2x2_X_AXIS_LSB_ADDR 0x02
#define BMA2x2_X_AXIS_MSB_ADDR 0x03
#define BMA2x2_Y_AXIS_LSB_ADDR 0x04
#define BMA2x2_Y_AXIS_MSB_ADDR 0x05
#define BMA2x2_Z_AXIS_LSB_ADDR 0x06
#define BMA2x2_Z_AXIS_MSB_ADDR 0x07
#define BMA2x2_TEMP_ADDR 0x08

#define BMA2x2_AXIS_LSB_NEW_DATA 0x01

/* STATUS ADDRESS DEFINITIONS */
#define BMA2x2_STAT1_ADDR 0x09
#define BMA2x2_STAT2_ADDR 0x0A
#define BMA2x2_STAT_TAP_SLOPE_ADDR 0x0B
#define BMA2x2_STAT_ORIENT_HIGH_ADDR 0x0C
#define BMA2x2_STAT_FIFO_ADDR 0x0E
#define BMA2x2_RANGE_SELECT_ADDR 0x0F
#define BMA2x2_RANGE_SELECT_MSK 0x0F
#define BMA2x2_RANGE_2G 3
#define BMA2x2_RANGE_4G 5
#define BMA2x2_RANGE_8G 8
#define BMA2x2_RANGE_16G 12

#define BMA2x2_RANGE_TO_REG(_range)                            \
	((_range) < 8 ? BMA2x2_RANGE_2G + ((_range) / 4) * 2 : \
			BMA2x2_RANGE_8G + ((_range) / 16) * 4)

#define BMA2x2_REG_TO_RANGE(_reg)                                \
	((_reg) < BMA2x2_RANGE_8G ? 2 + (_reg)-BMA2x2_RANGE_2G : \
				    8 + ((_reg)-BMA2x2_RANGE_8G) * 2)

#define BMA2x2_BW_SELECT_ADDR 0x10
#define BMA2x2_BW_MSK 0x1F
#define BMA2x2_BW_7_81HZ 0x08 /* LowPass   7.8125HZ */
#define BMA2x2_BW_15_63HZ 0x09 /* LowPass  15.625HZ */
#define BMA2x2_BW_31_25HZ 0x0A /* LowPass  31.25HZ */
#define BMA2x2_BW_62_50HZ 0x0B /* LowPass  62.50HZ */
#define BMA2x2_BW_125HZ 0x0C /* LowPass 125HZ */
#define BMA2x2_BW_250HZ 0x0D /* LowPass 250HZ */
#define BMA2x2_BW_500HZ 0x0E /* LowPass 500HZ */
#define BMA2x2_BW_1000HZ 0x0F /* LowPass 1000HZ */

/* Do not use BW lower than 7813, because __fls cannot be call for 0 */
#define BMA2x2_BW_TO_REG(_bw)                                              \
	((_bw) < 125000 ? BMA2x2_BW_7_81HZ + __fls(((_bw) * 10) / 78125) : \
			  BMA2x2_BW_125HZ + __fls((_bw) / 125000))

#define BMA2x2_REG_TO_BW(_reg)                               \
	((_reg) < BMA2x2_BW_125HZ ?                          \
		 (78125 << ((_reg)-BMA2x2_BW_7_81HZ)) / 10 : \
		 125000 << ((_reg)-BMA2x2_BW_125HZ))

#define BMA2x2_MODE_CTRL_ADDR 0x11
#define BMA2x2_LOW_NOISE_CTRL_ADDR 0x12
#define BMA2x2_DATA_CTRL_ADDR 0x13
#define BMA2x2_DATA_HIGH_BW 0x80
#define BMA2x2_DATA_SHADOW_DIS 0x40
#define BMA2x2_RST_ADDR 0x14
#define BMA2x2_CMD_SOFT_RESET 0xb6

/* INTERRUPT ADDRESS DEFINITIONS */
#define BMA2x2_INTR_ENABLE1_ADDR 0x16
#define BMA2x2_INTR_ENABLE2_ADDR 0x17
#define BMA2x2_INTR_SLOW_NO_MOTION_ADDR 0x18
#define BMA2x2_INTR1_PAD_SELECT_ADDR 0x19
#define BMA2x2_INTR_DATA_SELECT_ADDR 0x1A
#define BMA2x2_INTR2_PAD_SELECT_ADDR 0x1B
#define BMA2x2_INTR_SOURCE_ADDR 0x1E
#define BMA2x2_INTR_SET_ADDR 0x20
#define BMA2x2_INTR_CTRL_ADDR 0x21
#define BMA2x2_INTR_CTRL_RST_INT 0x80

/* FEATURE ADDRESS DEFINITIONS */
#define BMA2x2_LOW_DURN_ADDR 0x22
#define BMA2x2_LOW_THRES_ADDR 0x23
#define BMA2x2_LOW_HIGH_HYST_ADDR 0x24
#define BMA2x2_HIGH_DURN_ADDR 0x25
#define BMA2x2_HIGH_THRES_ADDR 0x26
#define BMA2x2_SLOPE_DURN_ADDR 0x27
#define BMA2x2_SLOPE_THRES_ADDR 0x28
#define BMA2x2_SLOW_NO_MOTION_THRES_ADDR 0x29
#define BMA2x2_TAP_PARAM_ADDR 0x2A
#define BMA2x2_TAP_THRES_ADDR 0x2B
#define BMA2x2_ORIENT_PARAM_ADDR 0x2C
#define BMA2x2_THETA_BLOCK_ADDR 0x2D
#define BMA2x2_THETA_FLAT_ADDR 0x2E
#define BMA2x2_FLAT_HOLD_TIME_ADDR 0x2F
#define BMA2x2_SELFTEST_ADDR 0x32
#define BMA2x2_EEPROM_CTRL_ADDR 0x33
#define BMA2x2_EEPROM_REMAIN_OFF 4
#define BMA2x2_EEPROM_REMAIN_MSK 0xF0
#define BMA2x2_EEPROM_LOAD 0x08
#define BMA2x2_EEPROM_RDY 0x04
#define BMA2x2_EEPROM_PROG 0x02
#define BMA2x2_EEPROM_PROG_EN 0x01
#define BMA2x2_SERIAL_CTRL_ADDR 0x34

/* OFFSET ADDRESS DEFINITIONS */
#define BMA2x2_OFFSET_CTRL_ADDR 0x36
#define BMA2x2_OFFSET_RESET 0x80
#define BMA2x2_OFFSET_TRIGGER_OFF 5
#define BMA2x2_OFFSET_TRIGGER_MASK (0x3 << BMA2x2_OFFSET_TRIGGER_OFF)
#define BMA2x2_OFFSET_CAL_READY 0x10
#define BMA2x2_OFFSET_CAL_SLOW_X 0x04
#define BMA2x2_OFFSET_CAL_SLOW_Y 0x02
#define BMA2x2_OFFSET_CAL_SLOW_Z 0x01

#define BMA2x2_OFC_SETTING_ADDR 0x37
#define BMA2x2_OFC_TARGET_AXIS_OFF 1
#define BMA2x2_OFC_TARGET_AXIS_LEN 2
#define BMA2x2_OFC_TARGET_AXIS(_axis) \
	(BMA2x2_OFC_TARGET_AXIS_LEN * (_axis) + BMA2x2_OFC_TARGET_AXIS_OFF)
#define BMA2x2_OFC_TARGET_0G 0
#define BMA2x2_OFC_TARGET_PLUS_1G 1
#define BMA2x2_OFC_TARGET_MINUS_1G 2

#define BMA2x2_OFFSET_X_AXIS_ADDR 0x38
#define BMA2x2_OFFSET_Y_AXIS_ADDR 0x39
#define BMA2x2_OFFSET_Z_AXIS_ADDR 0x3A

/* GP ADDRESS DEFINITIONS */
#define BMA2x2_GP0_ADDR 0x3B
#define BMA2x2_GP1_ADDR 0x3C

/* FIFO ADDRESS DEFINITIONS */
#define BMA2x2_FIFO_MODE_ADDR 0x3E
#define BMA2x2_FIFO_DATA_OUTPUT_ADDR 0x3F
#define BMA2x2_FIFO_WML_TRIG 0x30

/* Sensor resolution in number of bits. This sensor has fixed resolution. */
#define BMA2x2_RESOLUTION 12

#endif /* __CROS_EC_ACCEL_BMA2x2_H */
