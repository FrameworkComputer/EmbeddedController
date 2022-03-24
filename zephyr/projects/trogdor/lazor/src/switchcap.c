/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/gpio.h>

#include "common.h"
#include "config.h"
#include "console.h"
#include "driver/ln9310.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "i2c.h"
#include "power/qcom.h"
#include "system.h"
#include "sku.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

/* LN9310 switchcap */
const struct ln9310_config_t ln9310_config = {
	.i2c_port = I2C_PORT_POWER,
	.i2c_addr_flags = LN9310_I2C_ADDR_0_FLAGS,
};

static int switchcap_init(const struct device *unused)
{
	ARG_UNUSED(unused);
	if (board_has_da9313()) {
		CPRINTS("Use switchcap: DA9313");

		/*
		 * When the chip in power down mode, it outputs high-Z.
		 * Set pull-down to avoid floating.
		 */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(gpio_da9313_gpio0),
			GPIO_INPUT | GPIO_PULL_DOWN);

		/*
		 * Configure DA9313 enable, push-pull output. Don't set the
		 * level here; otherwise, it will override its value and
		 * shutdown the switchcap when sysjump to RW.
		 */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(gpio_switchcap_on),
			GPIO_OUTPUT);
	} else if (board_has_ln9310()) {
		CPRINTS("Use switchcap: LN9310");

		/* Enable interrupt for LN9310 */
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_switchcap_pg));

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
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(gpio_switchcap_on),
			GPIO_OUTPUT | GPIO_OPEN_DRAIN);

		/* Only configure the switchcap if not sysjump */
		if (!system_jumped_late()) {
			/*
			 * Deassert the enable pin, so the
			 * switchcap won't be enabled after the switchcap is
			 * configured from standby mode to switching mode.
			 */
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(gpio_switchcap_on),
				0);
			ln9310_init();
		}
	} else if (board_has_buck_ic()) {
		CPRINTS("Use Buck IC");
	} else {
		CPRINTS("ERROR: No switchcap solution");
	}

	return 0;
}
SYS_INIT(switchcap_init, APPLICATION, HOOK_PRIO_DEFAULT);

void board_set_switchcap_power(int enable)
{
	if (board_has_da9313()) {
		gpio_pin_set_dt(
			GPIO_DT_FROM_NODELABEL(gpio_switchcap_on),
			enable);
	} else if (board_has_ln9310()) {
		gpio_pin_set_dt(
			GPIO_DT_FROM_NODELABEL(gpio_switchcap_on),
			enable);
		ln9310_software_enable(enable);
	} else if (board_has_buck_ic()) {
		gpio_pin_set_dt(
			GPIO_DT_FROM_NODELABEL(gpio_vbob_en),
			enable);
	}
}

int board_is_switchcap_enabled(void)
{
	if (board_has_da9313() || board_has_ln9310())
		return gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_switchcap_on));

	/* Board has buck ic*/
	return gpio_pin_get_dt(
			GPIO_DT_FROM_NODELABEL(gpio_vbob_en));
}

int board_is_switchcap_power_good(void)
{
	if (board_has_da9313())
		return gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_da9313_gpio0));
	else if (board_has_ln9310())
		return ln9310_power_good();

	/* Board has buck ic no way to check POWER GOOD */
	return 1;
}
