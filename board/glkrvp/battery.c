/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "console.h"
#include "ioexpander_pca9555.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

#define I2C_PORT_PCA555_BATT_PRESENT_GPIO	NPCX_I2C_PORT0_0
#define I2C_ADDR_PCA555_BATT_PRESENT_GPIO	0x42
#define PCA555_BATT_PRESENT_GPIO_READ(reg, data) \
		pca9555_read(I2C_PORT_PCA555_BATT_PRESENT_GPIO, \
			I2C_ADDR_PCA555_BATT_PRESENT_GPIO, (reg), (data))

/* SMP-CA-445 Battery */
static const struct battery_info info = {
	.voltage_max = 8700,		/* mV */
	.voltage_normal = 7600,

	/*
	 * Actual value 6000mV, added 100mV for charger accuracy so that
	 * unwanted low VSYS_Prochot# assertion can be avoided.
	 */
	.voltage_min = 6100,
	.precharge_current = 150,	/* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c = 0,
	.charging_max_c = 45,
	.discharging_min_c = -20,
	.discharging_max_c = 60,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

static inline enum battery_present battery_hw_present(void)
{
	int data;
	int rv;

	rv = PCA555_BATT_PRESENT_GPIO_READ(PCA9555_CMD_INPUT_PORT_0, &data);

	/* GPIO is low when the battery is physically present */
	return rv || (data & PCA9555_IO_5) ? BP_NO : BP_YES;
}

/*
 * Physical detection of battery.
 */
enum battery_present battery_is_present(void)
{
	return battery_hw_present();
}
