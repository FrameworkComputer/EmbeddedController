/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BMA2x2 gsensor module for Chrome EC */

#ifndef __CROS_EC_ACCEL_BMA2x2_H
#define __CROS_EC_ACCEL_BMA2x2_H

enum bma2x2_accel {
	BMA255,
	SUPPORTED_BOSCH_ACCELS,
};

struct bma2x2_accel_data {
	/* Variant of Bosch Accelerometer. */
	uint8_t variant;
	/* Note, the following are indicies into their respective tables. */
	/* Current range of accelerometer. */
	int sensor_range;
	/* Current output data rate of accelerometer. */
	int sensor_datarate;
	/* Current resolution of accelerometer. */
	int sensor_resolution;
	/* Device address. */
	int accel_addr;
	int16_t offset[3];
};

extern const struct accelgyro_drv bma2x2_accel_drv;

/* I2C ADDRESS DEFINITIONS    */
/* The following definition of I2C address is used for the following sensors
* BMA255
* BMA355
* BMA280
* BMA282
* BMA223
* BMA254
* BMA284
* BMA250E
* BMA222E
*/
#define BMA2x2_I2C_ADDR1                0x30
#define BMA2x2_I2C_ADDR2                0x19

/* The following definition of I2C address is used for the following sensors
* BMC150
* BMC056
* BMC156
*/
#define BMA2x2_I2C_ADDR3                0x10
#define BMA2x2_I2C_ADDR4                0x11

/*** Chip-specific registers ***/
/* REGISTER ADDRESS DEFINITIONS */
#define BMA2x2_EEP_OFFSET                       0x16
#define BMA2x2_IMAGE_BASE                       0x38
#define BMA2x2_IMAGE_LEN                        22
#define BMA2x2_CHIP_ID_ADDR			0x00
#define BMA255_CHIP_ID_MAJOR			0xfa

/* DATA ADDRESS DEFINITIONS */
#define BMA2x2_X_AXIS_LSB_ADDR                  0x02
#define BMA2x2_X_AXIS_MSB_ADDR                  0x03
#define BMA2x2_Y_AXIS_LSB_ADDR                  0x04
#define BMA2x2_Y_AXIS_MSB_ADDR                  0x05
#define BMA2x2_Z_AXIS_LSB_ADDR                  0x06
#define BMA2x2_Z_AXIS_MSB_ADDR                  0x07
#define BMA2x2_TEMP_ADDR			0x08

/* STATUS ADDRESS DEFINITIONS */
#define BMA2x2_STAT1_ADDR			0x09
#define BMA2x2_STAT2_ADDR			0x0A
#define BMA2x2_STAT_TAP_SLOPE_ADDR		0x0B
#define BMA2x2_STAT_ORIENT_HIGH_ADDR		0x0C
#define BMA2x2_STAT_FIFO_ADDR			0x0E
#define BMA2x2_RANGE_SELECT_ADDR		0x0F
#define BMA2x2_BW_SELECT_ADDR                   0x10
#define BMA2x2_MODE_CTRL_ADDR                   0x11
#define BMA2x2_LOW_NOISE_CTRL_ADDR              0x12
#define BMA2x2_DATA_CTRL_ADDR                   0x13
#define BMA2x2_RST_ADDR                         0x14
#define BMA2x2_CMD_SOFT_RESET			0xb6

/* INTERRUPT ADDRESS DEFINITIONS */
#define BMA2x2_INTR_ENABLE1_ADDR                0x16
#define BMA2x2_INTR_ENABLE2_ADDR                0x17
#define BMA2x2_INTR_SLOW_NO_MOTION_ADDR         0x18
#define BMA2x2_INTR1_PAD_SELECT_ADDR            0x19
#define BMA2x2_INTR_DATA_SELECT_ADDR            0x1A
#define BMA2x2_INTR2_PAD_SELECT_ADDR             0x1B
#define BMA2x2_INTR_SOURCE_ADDR                  0x1E
#define BMA2x2_INTR_SET_ADDR                     0x20
#define BMA2x2_INTR_CTRL_ADDR                    0x21

/* FEATURE ADDRESS DEFINITIONS */
#define BMA2x2_LOW_DURN_ADDR                     0x22
#define BMA2x2_LOW_THRES_ADDR                    0x23
#define BMA2x2_LOW_HIGH_HYST_ADDR                0x24
#define BMA2x2_HIGH_DURN_ADDR                    0x25
#define BMA2x2_HIGH_THRES_ADDR                   0x26
#define BMA2x2_SLOPE_DURN_ADDR                   0x27
#define BMA2x2_SLOPE_THRES_ADDR                  0x28
#define BMA2x2_SLOW_NO_MOTION_THRES_ADDR         0x29
#define BMA2x2_TAP_PARAM_ADDR                    0x2A
#define BMA2x2_TAP_THRES_ADDR                    0x2B
#define BMA2x2_ORIENT_PARAM_ADDR                 0x2C
#define BMA2x2_THETA_BLOCK_ADDR                  0x2D
#define BMA2x2_THETA_FLAT_ADDR                   0x2E
#define BMA2x2_FLAT_HOLD_TIME_ADDR               0x2F
#define BMA2x2_SELFTEST_ADDR                     0x32
#define BMA2x2_EEPROM_CTRL_ADDR                  0x33
#define BMA2x2_SERIAL_CTRL_ADDR                  0x34

/* OFFSET ADDRESS DEFINITIONS */
#define BMA2x2_OFFSET_CTRL_ADDR                  0x36
#define BMA2x2_OFFSET_PARAMS_ADDR                0x37
#define BMA2x2_OFFSET_X_AXIS_ADDR                0x38
#define BMA2x2_OFFSET_Y_AXIS_ADDR                0x39
#define BMA2x2_OFFSET_Z_AXIS_ADDR                0x3A

/* GP ADDRESS DEFINITIONS */
#define BMA2x2_GP0_ADDR                          0x3B
#define BMA2x2_GP1_ADDR                          0x3C

/* FIFO ADDRESS DEFINITIONS */
#define BMA2x2_FIFO_MODE_ADDR                    0x3E
#define BMA2x2_FIFO_DATA_OUTPUT_ADDR             0x3F
#define BMA2x2_FIFO_WML_TRIG                     0x30

/* RANGE */
#define BMA2x2_RANGE_SELECT_POS             0
#define BMA2x2_RANGE_SELECT_LEN             4
#define BMA2x2_RANGE_SELECT_MSK             0x0F
#define BMA2x2_RANGE_SELECT_REG             BMA2x2_RANGE_SELECT_ADDR

#define BMA2x2_RANGE_2G                 3
#define BMA2x2_RANGE_4G                 5
#define BMA2x2_RANGE_8G                 8
#define BMA2x2_RANGE_16G                12

/* Sensor resolution in number of bits. This sensor has fixed resolution. */
#define BMA2x2_RESOLUTION      12

/* BANDWIDTH */
#define BMA2x2_BW_POS             0
#define BMA2x2_BW_LEN             5
#define BMA2x2_BW_MSK             0x1F
#define BMA2x2_BW_REG             BMA2x2_BW_SELECT_ADDR

#define BMA2x2_BW_7_81HZ        0x08 /* LowPass 7.81HZ */
#define BMA2x2_BW_15_63HZ       0x09 /* LowPass 15.63HZ */
#define BMA2x2_BW_31_25HZ       0x0A /* LowPass 31.25HZ */
#define BMA2x2_BW_62_50HZ       0x0B /* LowPass 62.50HZ */
#define BMA2x2_BW_125HZ         0x0C /* LowPass 125HZ */
#define BMA2x2_BW_250HZ         0x0D /* LowPass 250HZ */
#define BMA2x2_BW_500HZ         0x0E /* LowPass 500HZ */
#define BMA2x2_BW_1000HZ        0x0F /* LowPass 1000HZ */
#endif /* __CROS_EC_ACCEL_BMA2x2_H */
