/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "config.h"
#include "console.h"
#include "driver/ln9310.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "power/sc7180.h"
#include "system.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

/* LN9310 switchcap */
const struct ln9310_config_t ln9310_config = {
	.i2c_port = I2C_PORT_POWER,
	.i2c_addr_flags = LN9310_I2C_ADDR_0_FLAGS,
};

static int board_has_ln9310(void)
{
	static int ln9310_present = -1;
	int status, val;

	/* Cache the status of LN9310 present or not */
	if (ln9310_present == -1) {
		status = i2c_read8(ln9310_config.i2c_port,
				   ln9310_config.i2c_addr_flags,
				   LN9310_REG_CHIP_ID,
				   &val);

		/*
		 * Any error reading LN9310 CHIP_ID over I2C means the chip
		 * not present. Fallback to use DA9313 switchcap.
		 */
		ln9310_present = !status && val == LN9310_CHIP_ID;
	}

	return ln9310_present;
}

static void switchcap_init(void)
{
	if (board_has_ln9310()) {
		CPRINTS("Use switchcap: LN9310");

		/* Configure and enable interrupt for LN9310 */
		gpio_set_flags(GPIO_SWITCHCAP_PG_INT_L, GPIO_INT_FALLING);
		gpio_enable_interrupt(GPIO_SWITCHCAP_PG_INT_L);

		/*
		 * Configure LN9310 enable, open-drain output. Don't set the
		 * level here; otherwise, it will override its value and
		 * shutdown the switchcap when sysjump to RW.
		 *
		 * Note that the gpio.inc configures it GPIO_OUT_LOW. When
		 * sysjump to RW, will output push-pull a short period of
		 * time. As it outputs LOW, should be fine.
		 *
		 * This GPIO changes like:
		 * (1) EC boots from RO -> high-Z
		 * (2) GPIO init according to gpio.inc -> push-pull LOW
		 * (3) This function configures it -> open-drain HIGH
		 * (4) Power sequence turns on the switchcap -> open-drain LOW
		 * (5) EC sysjumps to RW
		 * (6) GPIO init according to gpio.inc -> push-pull LOW
		 * (7) This function configures it -> open-drain LOW
		 */
		gpio_set_flags(GPIO_SWITCHCAP_ON_L,
			       GPIO_OUTPUT | GPIO_OPEN_DRAIN);

		/* Only configure the switchcap if not sysjump */
		if (!system_jumped_late()) {
			/*
			 * Deassert the enable pin (set it HIGH), so the
			 * switchcap won't be enabled after the switchcap is
			 * configured from standby mode to switching mode.
			 */
			gpio_set_level(GPIO_SWITCHCAP_ON_L, 1);
			ln9310_init();
		}
	} else {
		CPRINTS("Use switchcap: DA9313");

		/*
		 * When the chip in power down mode, it outputs high-Z.
		 * Set pull-down to avoid floating.
		 */
		gpio_set_flags(GPIO_DA9313_GPIO0, GPIO_INPUT | GPIO_PULL_DOWN);

		/*
		 * Configure DA9313 enable, push-pull output. Don't set the
		 * level here; otherwise, it will override its value and
		 * shutdown the switchcap when sysjump to RW.
		 */
		gpio_set_flags(GPIO_SWITCHCAP_ON, GPIO_OUTPUT);
	}
}
DECLARE_HOOK(HOOK_INIT, switchcap_init, HOOK_PRIO_DEFAULT);

void board_set_switchcap_power(int enable)
{
	if (board_has_ln9310()) {
		gpio_set_level(GPIO_SWITCHCAP_ON_L, !enable);
		ln9310_software_enable(enable);
	} else {
		gpio_set_level(GPIO_SWITCHCAP_ON, enable);
	}
}

int board_is_switchcap_enabled(void)
{
	if (board_has_ln9310())
		return !gpio_get_level(GPIO_SWITCHCAP_ON_L);
	else
		return gpio_get_level(GPIO_SWITCHCAP_ON);
}

int board_is_switchcap_power_good(void)
{
	if (board_has_ln9310())
		return ln9310_power_good();
	else
		return gpio_get_level(GPIO_DA9313_GPIO0);
}
