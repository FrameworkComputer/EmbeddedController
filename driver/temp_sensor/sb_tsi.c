/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * SB-TSI: SB Temperature Sensor Interface.
 * This is an I2C temp sensor on the AMD Stony Ridge FT4 SOC.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "temp_sensor/sb_tsi.h"
#include "util.h"

static int temp;

static int raw_read8(const int offset, int *data_ptr)
{
	return i2c_read8(I2C_PORT_THERMAL_AP, SB_TSI_I2C_ADDR_FLAGS, offset,
			 data_ptr);
}

#ifndef CONFIG_ZEPHYR
static void sb_tsi_poll(void)
{
	int data, ret;

	/* FT4 SB-TSI sensor only powered in S0 */
	if (!chipset_in_state(CHIPSET_STATE_ON) || chipset_in_low_power_mode())
		return;

	/* Read the value over I2C */
	ret = raw_read8(SB_TSI_TEMP_H, &data);

	if (ret)
		return;

	temp = C_TO_K(data);
}
DECLARE_HOOK(HOOK_SECOND, sb_tsi_poll, HOOK_PRIO_TEMP_SENSOR);
#else
void sb_tsi_update_temperature(int idx)
{
	int data, ret;

	/* There is only one temp sensor on the FT4 */
	if (idx != 0)
		return;
	/* FT4 SB-TSI sensor only powered in S0 */
	if (!chipset_in_state(CHIPSET_STATE_ON) || chipset_in_low_power_mode())
		return;
	/* Read the value over I2C */
	ret = raw_read8(SB_TSI_TEMP_H, &data);
	if (ret)
		return;
	temp = C_TO_K(data);
}
#endif /* CONFIG_ZEPHYR */


int sb_tsi_get_val(int idx, int *temp_ptr)
{
	/* There is only one temp sensor on the FT4 */
	if (idx != 0)
		return EC_ERROR_PARAM1;

	*temp_ptr = temp;
	return EC_SUCCESS;
}
