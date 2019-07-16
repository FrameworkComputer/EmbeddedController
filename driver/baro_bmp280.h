/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/** \mainpage
*
****************************************************************************
* Copyright (C) 2012 - 2015 Bosch Sensortec GmbH
*
* File : bmp280.h
*
* Date : 2015/03/27
*
* Revision : 2.0.4(Pressure and Temperature compensation code revision is 1.1)
*
* Usage: Sensor Driver for BMP280 sensor
*
****************************************************************************
*
* \section License
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
*   Redistributions of source code must retain the above copyright
*   notice, this list of conditions and the following disclaimer.
*
*   Redistributions in binary form must reproduce the above copyright
*   notice, this list of conditions and the following disclaimer in the
*   documentation and/or other materials provided with the distribution.
*
*   Neither the name of the copyright holder nor the names of the
*   contributors may be used to endorse or promote products derived from
*   this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER
* OR CONTRIBUTORS BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
* OR CONSEQUENTIAL DAMAGES(INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
* ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*
* The information provided is believed to be accurate and reliable.
* The copyright holder assumes no responsibility
* for the consequences of use
* of such information nor for any infringement of patents or
* other rights of third parties which may result from its use.
* No license is granted by implication or otherwise under any patent or
* patent rights of the copyright holder.
**************************************************************************/
/* BMP280 pressure and temperature module for Chrome EC */

#ifndef __CROS_EC_BARO_BMP280_H
#define __CROS_EC_BARO_BMP280_H

/*
 * The addr field of barometer_sensor for I2C:
 *
 * +-------------------------------+---+
 * |    7 bit i2c address          | 0 |
 * +-------------------------------+---+
 */

/*
 * Bit 1 of 7-bit address: 0 - If SDO is connected to GND
 * Bit 1 of 7-bit address: 1 - If SDO is connected to Vddio
 */
#define BMP280_I2C_ADDRESS1_FLAGS	0x76
#define BMP280_I2C_ADDRESS2_FLAGS	0x77

/*
 *     CHIP ID
 */
#define BMP280_CHIP_ID			0x58
/************************************************/
/*	CALIBRATION PARAMETERS DEFINITION       */
/************************************************/

#define BMP280_TEMPERATURE_CALIB_DIG_T1_LSB_REG	0x88

/************************************************/
/*	REGISTER ADDRESS DEFINITION             */
/************************************************/
#define BMP280_CHIP_ID_REG		     0xD0
#define BMP280_RST_REG                       0xE0  /*Softreset Register */
#define BMP280_STAT_REG                      0xF3  /*Status Register */
#define BMP280_CTRL_MEAS_REG                 0xF4  /*Ctrl Measure Register */
#define BMP280_CONFIG_REG                    0xF5  /*Configuration Register */
#define BMP280_PRESSURE_MSB_REG              0xF7  /*Pressure MSB Register */
#define BMP280_PRESSURE_LSB_REG              0xF8  /*Pressure LSB Register */
#define BMP280_PRESSURE_XLSB_REG             0xF9  /*Pressure XLSB Register */
/************************************************/
/*	POWER MODE DEFINITION                   */
/************************************************/
/* Sensor Specific constants */
#define BMP280_SLEEP_MODE                    0x00
#define BMP280_FORCED_MODE                   0x01
#define BMP280_NORMAL_MODE                   0x03
#define BMP280_SOFT_RESET_CODE               0xB6
/************************************************/
/*	STANDBY TIME DEFINITION                 */
/************************************************/
#define BMP280_STANDBY_TIME_1_MS              0x00
#define BMP280_STANDBY_TIME_63_MS             0x01
#define BMP280_STANDBY_TIME_125_MS            0x02
#define BMP280_STANDBY_TIME_250_MS            0x03
#define BMP280_STANDBY_TIME_500_MS            0x04
#define BMP280_STANDBY_TIME_1000_MS           0x05
#define BMP280_STANDBY_TIME_2000_MS           0x06
#define BMP280_STANDBY_TIME_4000_MS           0x07
/************************************************/
/*	OVERSAMPLING DEFINITION                 */
/************************************************/
#define BMP280_OVERSAMP_SKIPPED          0x00
#define BMP280_OVERSAMP_1X               0x01
#define BMP280_OVERSAMP_2X               0x02
#define BMP280_OVERSAMP_4X               0x03
#define BMP280_OVERSAMP_8X               0x04
#define BMP280_OVERSAMP_16X              0x05
/************************************************/
/*	DEFINITIONS FOR ARRAY SIZE OF DATA      */
/************************************************/
#define	BMP280_PRESSURE_DATA_SIZE	3
#define	BMP280_DATA_FRAME_SIZE		6
#define	BMP280_CALIB_DATA_SIZE		24
/*******************************************************/
/*         SAMPLING PERIOD COMPUTATION CONSTANT	       */
/*******************************************************/
#define BMP280_STANDBY_CNT		8
#define T_INIT_MAX			(20) /* (20/16 = 1.25ms) */
#define T_MEASURE_PER_OSRS_MAX		(37) /* (37/16 = 2.31ms) */
#define T_SETUP_PRESSURE_MAX		(10) /* (10/16 = 0.62ms) */

/*
 * This is the measurement time required for pressure and temp
 */
#define BMP280_COMPUTE_TIME \
	((T_INIT_MAX + T_MEASURE_PER_OSRS_MAX * \
	  ((BIT(BMP280_OVERSAMP_TEMP) >> 1) + \
	   (BIT(BMP280_OVERSAMP_PRES) >> 1)) + \
	  (BMP280_OVERSAMP_PRES ? T_SETUP_PRESSURE_MAX : 0) + 15) / 16)

/*
 * These values are selected as per Bosch recommendation for
 * standard handheld devices, with temp sensor not being used
 */
#define BMP280_OVERSAMP_PRES BMP280_OVERSAMP_4X
#define BMP280_OVERSAMP_TEMP BMP280_OVERSAMP_SKIPPED
/*******************************************************/
/*             GET DRIVER DATA			       */
/*******************************************************/
#define BMP280_GET_DATA(_s) \
	((struct bmp280_drv_data_t *)(_s)->drv_data)

/* Min and Max sampling frequency in mHz based on x4 oversampling used */
/* FIXME - verify how chip is setup to make sure MAX is correct, manual says
 * "Typical", not Max.
 */
#define BMP280_BARO_MIN_FREQ  75000
#define BMP280_BARO_MAX_FREQ  87000
#if (CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ <= BMP280_BARO_MAX_FREQ)
#error "EC too slow for accelerometer"
#endif

/**************************************************************/
/*	STRUCTURE and ENUM DEFINITIONS                        */
/**************************************************************/

/*
 * struct bmp280_calib_param_t - Holds all device specific
 *                                calibration parameters
 *
 * @dig_T1 to dig_T3:   calibration Temp data
 * @dig_P1 to dig_P9:   calibration Pressure data
 * @t_fine:   calibration t_fine data
 *
 */
struct bmp280_calib_param_t {
	uint16_t dig_T1;
	int16_t dig_T2;
	int16_t dig_T3;
	uint16_t dig_P1;
	int16_t dig_P2;
	int16_t dig_P3;
	int16_t dig_P4;
	int16_t dig_P5;
	int16_t dig_P6;
	int16_t dig_P7;
	int16_t dig_P8;
	int16_t dig_P9;

	int32_t t_fine;
};

/*
 * struct bmp280_t - This structure holds BMP280 initialization parameters
 * @calib_param:          calibration data
 * @rate:     frequency, in mHz.
 * @range:		bit offset to fit data in 16 bit or less.
 */
struct bmp280_drv_data_t {

	struct   bmp280_calib_param_t calib_param;
	uint16_t rate;
	uint16_t range;
};
#define BMP280_RATE_SHIFT 1

extern const struct accelgyro_drv bmp280_drv;

#ifdef CONFIG_CMD_I2C_STRESS_TEST_ACCEL
extern struct i2c_stress_test_dev bmp280_i2c_stress_test_dev;
#endif

#endif
