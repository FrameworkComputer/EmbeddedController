/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/*
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
#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/baro_bmp280.h"
#include "i2c.h"
#include "timer.h"

#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ## args)

static const uint16_t standby_durn[] = {1, 63, 125, 250, 500, 1000, 2000, 4000};

/*
 * This function is used to get calibration parameters used for
 * calculation in the registers
 *
 *  parameter | Register address |   bit
 *------------|------------------|----------------
 *	dig_T1    |  0x88 and 0x89   | from 0 : 7 to 8: 15
 *	dig_T2    |  0x8A and 0x8B   | from 0 : 7 to 8: 15
 *	dig_T3    |  0x8C and 0x8D   | from 0 : 7 to 8: 15
 *	dig_P1    |  0x8E and 0x8F   | from 0 : 7 to 8: 15
 *	dig_P2    |  0x90 and 0x91   | from 0 : 7 to 8: 15
 *	dig_P3    |  0x92 and 0x93   | from 0 : 7 to 8: 15
 *	dig_P4    |  0x94 and 0x95   | from 0 : 7 to 8: 15
 *	dig_P5    |  0x96 and 0x97   | from 0 : 7 to 8: 15
 *	dig_P6    |  0x98 and 0x99   | from 0 : 7 to 8: 15
 *	dig_P7    |  0x9A and 0x9B   | from 0 : 7 to 8: 15
 *	dig_P8    |  0x9C and 0x9D   | from 0 : 7 to 8: 15
 *	dig_P9    |  0x9E and 0x9F   | from 0 : 7 to 8: 15
 *
 *	@return results of bus communication function
 *	@retval 0 -> Success
 *
 */
static int bmp280_get_calib_param(const struct motion_sensor_t *s)
{
	int ret;

	uint8_t a_data_u8[BMP280_CALIB_DATA_SIZE] = {0};
	struct bmp280_drv_data_t *data = BMP280_GET_DATA(s);

	ret = i2c_read_block(s->port, s->i2c_spi_addr_flags,
			BMP280_TEMPERATURE_CALIB_DIG_T1_LSB_REG,
			a_data_u8, BMP280_CALIB_DATA_SIZE);

	if (ret)
		return ret;

	/* read calibration values*/
	data->calib_param.dig_T1 = (a_data_u8[1] << 8) | a_data_u8[0];
	data->calib_param.dig_T2 = (a_data_u8[3] << 8  | a_data_u8[2]);
	data->calib_param.dig_T3 = (a_data_u8[5] << 8) | a_data_u8[4];

	data->calib_param.dig_P1 = (a_data_u8[7] << 8) | a_data_u8[6];
	data->calib_param.dig_P2 = (a_data_u8[9] << 8) | a_data_u8[8];
	data->calib_param.dig_P3 = (a_data_u8[11] << 8) | a_data_u8[10];
	data->calib_param.dig_P4 = (a_data_u8[13] << 8) | a_data_u8[12];
	data->calib_param.dig_P5 = (a_data_u8[15] << 8) | a_data_u8[14];
	data->calib_param.dig_P6 = (a_data_u8[17] << 8) | a_data_u8[16];
	data->calib_param.dig_P7 = (a_data_u8[19] << 8) | a_data_u8[18];
	data->calib_param.dig_P8 = (a_data_u8[21] << 8) | a_data_u8[20];
	data->calib_param.dig_P9 = (a_data_u8[23] << 8) | a_data_u8[22];

	return EC_SUCCESS;
}

static int bmp280_read_uncomp_pressure(const struct motion_sensor_t *s,
					int *uncomp_pres)
{
	int ret;
	uint8_t a_data_u8[BMP280_PRESSURE_DATA_SIZE] = {0};

	ret = i2c_read_block(s->port, s->i2c_spi_addr_flags,
			BMP280_PRESSURE_MSB_REG,
			a_data_u8, BMP280_PRESSURE_DATA_SIZE);

	if (ret)
		return ret;

	*uncomp_pres = (int32_t)((a_data_u8[0] << 12) |
		     (a_data_u8[1] << 4) |
		     (a_data_u8[2] >> 4));

	return EC_SUCCESS;
}

/*
 * Reads actual pressure from uncompensated pressure
 * and returns the value in Pascal(Pa)
 * @note Output value of "96386" equals 96386 Pa =
 * 963.86 hPa = 963.86 millibar
 *
 * Algorithm from BMP280 Datasheet Rev 1.15 Section 8.2
 *
 */
static int bmp280_compensate_pressure(const struct motion_sensor_t *s,
					int uncomp_pressure)
{
	int var1, var2;
	uint32_t p;
	struct bmp280_drv_data_t *data = BMP280_GET_DATA(s);

	/* calculate x1 */
	var1 = (((int32_t)data->calib_param.t_fine)
		>> 1) - 64000;
	/* calculate x2 */
	var2 = (((var1 >> 2) * (var1 >> 2)) >> 11)
		* ((int32_t)data->calib_param.dig_P6);
	var2 = var2 + ((var1 * ((int32_t)data->calib_param.dig_P5)) << 1);
	var2 = (var2 >> 2) + (((int32_t)data->calib_param.dig_P4) << 16);
	/* calculate x1 */
	var1 = (((data->calib_param.dig_P3 *
		(((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) +
		((((int32_t)data->calib_param.dig_P2) * var1) >> 1)) >> 18;
	var1 = ((((32768 + var1)) *
		((int32_t)data->calib_param.dig_P1)) >> 15);

	/* Avoid exception caused by division by zero */
	if (!var1)
		return 0;

	/* calculate pressure */
	p = (((uint32_t)((1048576) - uncomp_pressure) -
		(var2 >> 12))) * 3125;

	/* check overflow */
	if (p < 0x80000000)
		p = (p << 1) / ((uint32_t)var1);
	else
		p = (p / (uint32_t)var1) << 1;

	/* calculate x1 */
	var1 = (((int32_t)data->calib_param.dig_P9) *
		((int32_t)(((p >> 3) * (p >> 3)) >> 13))) >> 12;
	/* calculate x2 */
	var2 = (((int32_t)(p >> 2)) *
		((int32_t)data->calib_param.dig_P8)) >> 13;
	/* calculate true pressure */
	return (uint32_t)((int32_t)p + ((var1 + var2 +
		data->calib_param.dig_P7) >> 4));
}

/*
 * Set the standby duration
 * standby_durn: The standby duration time value.
 *  value     |  standby duration
 *  ----------|--------------------
 *    0x00    | 1_MS
 *    0x01    | 63_MS
 *    0x02    | 125_MS
 *    0x03    | 250_MS
 *    0x04    | 500_MS
 *    0x05    | 1000_MS
 *    0x06    | 2000_MS
 *    0x07    | 4000_MS
 */
static int bmp280_set_standby_durn(const struct motion_sensor_t *s,
				uint8_t durn)
{
	int ret, val;

	ret = i2c_read8(s->port, s->i2c_spi_addr_flags,
			BMP280_CONFIG_REG, &val);

	if (ret == EC_SUCCESS) {
		val = (val & 0xE0) | ((durn << 5) & 0xE0);
		/* write the standby duration*/
		ret = i2c_write8(s->port, s->i2c_spi_addr_flags,
			   BMP280_CONFIG_REG, val);
	}

	return ret;
}

static int bmp280_set_power_mode(const struct motion_sensor_t *s,
				uint8_t power_mode)
{
	int val;

	val = (BMP280_OVERSAMP_TEMP << 5) +
		(BMP280_OVERSAMP_PRES << 2) + power_mode;

	return i2c_write8(s->port, s->i2c_spi_addr_flags,
			  BMP280_CTRL_MEAS_REG, val);
}

static int bmp280_set_range(const struct motion_sensor_t *s,
				int range,
				int rnd)
{
	struct bmp280_drv_data_t *data = BMP280_GET_DATA(s);
	/*
	 * ->range contains the number of bit to right shift in order for the
	 * measurment to fit into 16 bits (or less if the AP wants to).
	 */
	data->range = 15 - __builtin_clz(range);
	return EC_SUCCESS;
}

static int bmp280_get_range(const struct motion_sensor_t *s)
{
	struct bmp280_drv_data_t *data = BMP280_GET_DATA(s);

	return 1 << (16 + data->range);
}

/*
 * bmp280_init() - Used to initialize barometer with default config
 *
 * @return results of bus communication function
 * @retval 0 -> Success
 */

static int bmp280_init(const struct motion_sensor_t *s)
{
	int val, ret;

	if (!s)
		return EC_ERROR_INVAL;

	/* Read chip id */
	ret = i2c_read8(s->port, s->i2c_spi_addr_flags,
			BMP280_CHIP_ID_REG, &val);
	if (ret)
		return ret;

	if (val != BMP280_CHIP_ID)
		return EC_ERROR_INVAL;

	/* set power mode */
	ret = bmp280_set_power_mode(s, BMP280_SLEEP_MODE);
	if (ret)
		return ret;

	/* Read bmp280 calibration parameter */
	ret = bmp280_get_calib_param(s);
	if (ret)
		return ret;

	return sensor_init_done(s);
}

static int bmp280_read(const struct motion_sensor_t *s, intv3_t v)
{
	int ret, pres;
	struct bmp280_drv_data_t *data = BMP280_GET_DATA(s);

	ret = bmp280_read_uncomp_pressure(s, &pres);

	if (ret)
		return ret;

	v[0] = bmp280_compensate_pressure(s, pres) >> data->range;
	v[1] = v[2] = 0;

	return EC_SUCCESS;
}

/*
 * Set data rate, rate in mHz.
 * Calculate the delay (in ms) to apply.
 */
static int bmp280_set_data_rate(const struct motion_sensor_t *s, int rate,
							int roundup)
{
	struct bmp280_drv_data_t *data = BMP280_GET_DATA(s);
	int durn, i, ret;
	int period; /* Period in ms */

	if (rate == 0) {
		/* Set to sleep mode */
		data->rate = 0;
		return bmp280_set_power_mode(s, BMP280_SLEEP_MODE);
	} else
		period = 1000000 / rate;

	/* reset power mode, waking from sleep */
	if (!data->rate) {
		ret = bmp280_set_power_mode(s, BMP280_NORMAL_MODE);
		if (ret)
			return ret;
	}

	durn = 0;
	for (i = BMP280_STANDBY_CNT-1;  i > 0; i--) {
		if (period >= standby_durn[i] + BMP280_COMPUTE_TIME) {
			durn = i;
			break;
		} else if (period > standby_durn[i-1] + BMP280_COMPUTE_TIME) {
			durn = roundup ? i-1 : i;
			break;
		}
	}
	ret = bmp280_set_standby_durn(s, durn);
	if (ret == EC_SUCCESS)
		/*
		 * The maximum frequency is around 76Hz. Be sure it fits in 16
		 * bits by shifting by one bit.
		 */
		data->rate = (1000000 >> BMP280_RATE_SHIFT) /
			     (standby_durn[durn] + BMP280_COMPUTE_TIME);
	return ret;
}

static int bmp280_get_data_rate(const struct motion_sensor_t *s)
{
	struct bmp280_drv_data_t *data = BMP280_GET_DATA(s);

	return data->rate << BMP280_RATE_SHIFT;
}

const struct accelgyro_drv bmp280_drv = {
	.init = bmp280_init,
	.read = bmp280_read,
	.set_range = bmp280_set_range,
	.get_range = bmp280_get_range,
	.set_data_rate = bmp280_set_data_rate,
	.get_data_rate = bmp280_get_data_rate,
};

#ifdef CONFIG_CMD_I2C_STRESS_TEST_ACCEL
struct i2c_stress_test_dev bmp280_i2c_stress_test_dev = {
	.reg_info = {
		.read_reg = BMP280_CHIP_ID_REG,
		.read_val = BMP280_CHIP_ID,
		.write_reg = BMP280_CONFIG_REG,
	},
	.i2c_read = &i2c_read8,
	.i2c_write = &i2c_write8,
};
#endif /* CONFIG_CMD_I2C_STRESS_TEST_ACCEL */
