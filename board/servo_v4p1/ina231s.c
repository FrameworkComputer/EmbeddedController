/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ina2xx.h"
#include "util.h"

#define PP_DUT_IDX	0
#define PP_CHG_IDX	1
#define SR_CHG_IDX	2

void init_ina231s(void)
{
	/* Calibrate INA0 (PP DUT) with 1mA/LSB scale */
	ina2xx_init(PP_DUT_IDX, 0x8000, INA2XX_CALIB_1MA(5 /*mOhm*/));

	/* Calibrate INA1 (PP CHG) with 1mA/LSB scale */
	ina2xx_init(PP_CHG_IDX, 0x8000, INA2XX_CALIB_1MA(5 /*mOhm*/));

	/* Calibrate INA2 (SR CHG) with 1mA/LSB scale*/
	ina2xx_init(SR_CHG_IDX, 0x8000, INA2XX_CALIB_1MA(5 /*mOhm*/));
}

int pp_dut_voltage(void)
{
	return ina2xx_get_voltage(PP_DUT_IDX);
}

int pp_dut_current(void)
{
	return ina2xx_get_current(PP_DUT_IDX);
}

int pp_dut_power(void)
{
	return ina2xx_get_power(PP_DUT_IDX);
}

int pp_chg_voltage(void)
{
	return ina2xx_get_voltage(PP_CHG_IDX);
}

int pp_chg_current(void)
{
	return ina2xx_get_current(PP_CHG_IDX);
}

int pp_chg_power(void)
{
	return ina2xx_get_power(PP_CHG_IDX);
}

int sr_chg_voltage(void)
{
	return ina2xx_get_voltage(SR_CHG_IDX);
}

int sr_chg_current(void)
{
	return ina2xx_get_current(SR_CHG_IDX);
}

int sr_chg_power(void)
{
	return ina2xx_get_power(SR_CHG_IDX);
}
