/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * BMM150 compass behind a BMI160
 */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/mag_bmm150.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#ifdef CONFIG_MAG_BMI_BMM150
#include "driver/accelgyro_bmi_common.h"
#define raw_mag_read8 bmi160_sec_raw_read8
#define raw_mag_write8 bmi160_sec_raw_write8
#else
#error "Not implemented"
#endif

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

/****************************************************************************
 * Copyright (C) 2011 - 2014 Bosch Sensortec GmbH
 *
 ****************************************************************************/
/***************************************************************************
 * License:
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
 *
 * The information provided is believed to be accurate and reliable.
 * The copyright holder assumes no responsibility for the consequences of use
 * of such information nor for any infringement of patents or
 * other rights of third parties which may result from its use.
 * No license is granted by implication or otherwise under any patent or
 * patent rights of the copyright holder.
 */

#define BMI150_READ_16BIT_COM_REG(store_, addr_)                              \
	do {                                                                  \
		int val;                                                      \
		raw_mag_read8(s->port, s->i2c_spi_addr_flags, (addr_), &val); \
		store_ = val;                                                 \
		raw_mag_read8(s->port, s->i2c_spi_addr_flags, (addr_) + 1,    \
			      &val);                                          \
		store_ |= (val << 8);                                         \
	} while (0)

int bmm150_init(struct motion_sensor_t *s)
{
	int ret;
	int val;
	struct bmm150_comp_registers *regs = BMM150_COMP_REG(s);
	struct mag_cal_t *moc = BMM150_CAL(s);

	/* Set the compass from Suspend to Sleep */
	ret = raw_mag_write8(s->port, s->i2c_spi_addr_flags, BMM150_PWR_CTRL,
			     BMM150_PWR_ON);
	crec_msleep(4);
	/* Now we can read the device id */
	ret = raw_mag_read8(s->port, s->i2c_spi_addr_flags, BMM150_CHIP_ID,
			    &val);
	if (ret)
		return EC_ERROR_UNKNOWN;

	if (val != BMM150_CHIP_ID_MAJOR)
		return EC_ERROR_ACCESS_DENIED;

	/* Read the private registers for compensation */
	ret = raw_mag_read8(s->port, s->i2c_spi_addr_flags, BMM150_REGA_DIG_X1,
			    &val);
	if (ret)
		return EC_ERROR_UNKNOWN;
	regs->dig1[X] = val;
	raw_mag_read8(s->port, s->i2c_spi_addr_flags, BMM150_REGA_DIG_Y1, &val);
	regs->dig1[Y] = val;
	raw_mag_read8(s->port, s->i2c_spi_addr_flags, BMM150_REGA_DIG_X2, &val);
	regs->dig2[X] = val;
	raw_mag_read8(s->port, s->i2c_spi_addr_flags, BMM150_REGA_DIG_Y2, &val);
	regs->dig2[Y] = val;

	raw_mag_read8(s->port, s->i2c_spi_addr_flags, BMM150_REGA_DIG_XY1,
		      &val);
	regs->dig_xy1 = val;

	raw_mag_read8(s->port, s->i2c_spi_addr_flags, BMM150_REGA_DIG_XY2,
		      &val);
	regs->dig_xy2 = val;

	BMI150_READ_16BIT_COM_REG(regs->dig_z1, BMM150_REGA_DIG_Z1_LSB);
	BMI150_READ_16BIT_COM_REG(regs->dig_z2, BMM150_REGA_DIG_Z2_LSB);
	BMI150_READ_16BIT_COM_REG(regs->dig_z3, BMM150_REGA_DIG_Z3_LSB);
	BMI150_READ_16BIT_COM_REG(regs->dig_z4, BMM150_REGA_DIG_Z4_LSB);
	BMI150_READ_16BIT_COM_REG(regs->dig_xyz1, BMM150_REGA_DIG_XYZ1_LSB);

	/* Set the repetition in "Regular Preset" */
	raw_mag_write8(s->port, s->i2c_spi_addr_flags, BMM150_REPXY,
		       BMM150_REP(SPECIAL, XY));
	raw_mag_write8(s->port, s->i2c_spi_addr_flags, BMM150_REPZ,
		       BMM150_REP(SPECIAL, Z));
	ret = raw_mag_read8(s->port, s->i2c_spi_addr_flags, BMM150_REPXY, &val);
	ret = raw_mag_read8(s->port, s->i2c_spi_addr_flags, BMM150_REPZ, &val);
	/*
	 * Set the compass forced mode, to sleep after each measure.
	 */
	ret = raw_mag_write8(s->port, s->i2c_spi_addr_flags, BMM150_OP_CTRL,
			     BMM150_OP_MODE_FORCED << BMM150_OP_MODE_OFFSET);

	init_mag_cal(moc);
	moc->radius = 0.0f;
	return ret;
}

void bmm150_temp_compensate_xy(const struct motion_sensor_t *s, intv3_t raw,
			       intv3_t comp, int r)
{
	int inter, axis;
	struct bmm150_comp_registers *regs = BMM150_COMP_REG(s);
	if (r == 0)
		inter = 0;
	else
		inter = ((int)regs->dig_xyz1 << 14) / r - BIT(14);

	for (axis = X; axis <= Y; axis++) {
		if (raw[axis] == BMM150_FLIP_OVERFLOW_ADCVAL) {
			comp[axis] = BMM150_OVERFLOW_OUTPUT;
			continue;
		}
		/*
		 * The formula is, using 4 LSB for precision:
		 * (mdata_x * ((((dig_xy2 * i^2 / 268435456) +
		 *              i * dig_xy1) / 16384) + 256) *
		 *  (dig2 + 160)) / 8192 + dig1 * 8.0f
		 * To prevent precision loss, we calculate at << 12:
		 * 1 / 268435456 = 1 >> 28 = 1 >> (7 + 9 + 12)
		 * 1 / 16384 = 1 >> (-7 + 9 + 12)
		 * 256 = 1 << (20 - 12)
		 */
		comp[axis] = (int)regs->dig_xy2 * ((inter * inter) >> 7);
		comp[axis] += inter * ((int)regs->dig_xy1 << 7);
		comp[axis] >>= 9;
		comp[axis] += 1 << (8 + 12);
		comp[axis] *= (int)regs->dig2[axis] + 160;
		comp[axis] >>= 12;
		comp[axis] *= raw[axis];
		comp[axis] >>= 13;
		comp[axis] += (int)regs->dig1[axis] << 3;
	}
}

void bmm150_temp_compensate_z(const struct motion_sensor_t *s, intv3_t raw,
			      intv3_t comp, int r)
{
	int dividend, divisor;
	struct bmm150_comp_registers *regs = BMM150_COMP_REG(s);

	if (raw[Z] == BMM150_HALL_OVERFLOW_ADCVAL) {
		comp[Z] = BMM150_OVERFLOW_OUTPUT;
		return;
	}
	/*
	 * The formula is
	 * ((z - dig_z4) * 131072 - dig_z3 * (r - dig_xyz1)) /
	 * ((dig_z2 + dig_z1 * r / 32768) * 4);
	 *
	 * We spread 4 so we multiply by 131072 / 4 == BIT(15) only.
	 */
	dividend = (raw[Z] - (int)regs->dig_z4) << 15;
	dividend -= (regs->dig_z3 * (r - (int)regs->dig_xyz1)) >> 2;
	/* add BIT(15) to round to next integer. */
	divisor = (int)regs->dig_z1 * (r << 1) + BIT(15);
	divisor >>= 16;
	divisor += (int)regs->dig_z2;
	comp[Z] = dividend / divisor;
	if (comp[Z] > BIT(15) || comp[Z] < -(BIT(15)))
		comp[Z] = BMM150_OVERFLOW_OUTPUT;
}

void bmm150_normalize(const struct motion_sensor_t *s, intv3_t v, uint8_t *data)
{
	uint16_t r;
	intv3_t raw;
	struct mag_cal_t *cal = BMM150_CAL(s);

	/* X and Y are two's complement 13 bits vectors */
	raw[X] = ((int16_t)(data[0] | (data[1] << 8))) >> 3;
	raw[Y] = ((int16_t)(data[2] | (data[3] << 8))) >> 3;
	/* Z are two's complement 15 bits vectors */
	raw[Z] = ((int16_t)(data[4] | (data[5] << 8))) >> 1;

	/* RHALL value to compensate with - unsigned 14 bits */
	r = (data[6] | (data[7] << 8)) >> 2;

	bmm150_temp_compensate_xy(s, raw, v, r);
	bmm150_temp_compensate_z(s, raw, v, r);
	mag_cal_update(cal, v);

	v[X] += cal->bias[X];
	v[Y] += cal->bias[Y];
	v[Z] += cal->bias[Z];
}

int bmm150_set_offset(const struct motion_sensor_t *s, const intv3_t offset)
{
	struct mag_cal_t *cal = BMM150_CAL(s);
	cal->bias[X] = offset[X];
	cal->bias[Y] = offset[Y];
	cal->bias[Z] = offset[Z];
	return EC_SUCCESS;
}

int bmm150_get_offset(const struct motion_sensor_t *s, intv3_t offset)
{
	struct mag_cal_t *cal = BMM150_CAL(s);
	offset[X] = cal->bias[X];
	offset[Y] = cal->bias[Y];
	offset[Z] = cal->bias[Z];
	return EC_SUCCESS;
}
