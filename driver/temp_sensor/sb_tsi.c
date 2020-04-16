/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * SB-TSI: SB Temperature Sensor Interface.
 * This is an I2C slave temp sensor on the AMD Stony Ridge FT4 SOC.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "sb_tsi.h"
#include "util.h"

static int raw_read8(const int offset, int *data_ptr)
{
	return i2c_read8(I2C_PORT_THERMAL_AP, SB_TSI_I2C_ADDR_FLAGS,
			 offset, data_ptr);
}

int sb_tsi_get_val(int idx, int *temp_ptr)
{
	int ret;
	/* There is only one temp sensor on the FT4 */
	if (idx != 0)
		return EC_ERROR_PARAM1;
	/* FT4 SB-TSI sensor only powered in S0 */
	if (!chipset_in_state(CHIPSET_STATE_ON))
		return EC_ERROR_NOT_POWERED;
	/* Read the value over I2C */
	ret = raw_read8(SB_TSI_TEMP_H, temp_ptr);
	if (ret)
		return ret;
	*temp_ptr = C_TO_K(*temp_ptr);
	return EC_SUCCESS;
}
