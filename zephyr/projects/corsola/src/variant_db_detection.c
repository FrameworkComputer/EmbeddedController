/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola daughter board detection */
#include <drivers/gpio.h>
#include <toolchain.h>

#include "console.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

#include "variant_db_detection.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

static void corsola_db_config(enum corsola_db_type type)
{
	switch (type) {
	case CORSOLA_DB_HDMI:
		/* EC_X_GPIO1 */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_en_hdmi_pwr),
				   GPIO_OUTPUT_HIGH);
		/* X_EC_GPIO2 */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_ec_dp_hpd),
				      GPIO_INPUT);
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_x_ec_gpio2));
		/* EC_X_GPIO3 */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_pwrdn_odl),
				     GPIO_OUTPUT_HIGH | GPIO_OPEN_DRAIN);
		return;
	case CORSOLA_DB_TYPEC:
		/* EC_X_GPIO1 */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_usb_c1_frs_en),
				      GPIO_OUTPUT_LOW);
		/* X_EC_GPIO2 */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_usb_c1_ppc_int_odl),
			GPIO_INPUT | GPIO_PULL_UP);
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_x_ec_gpio2));
		/* EC_X_GPIO3 */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_ALIAS(gpio_usb_c1_dp_in_hpd),
			GPIO_OUTPUT_LOW);
		return;
	default:
		break;
	}
}

enum corsola_db_type corsola_get_db_type(void)
{
	static enum corsola_db_type db = CORSOLA_DB_NONE;

	if (db != CORSOLA_DB_NONE)
		return db;

	if (!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_hdmi_prsnt_odl)))
		db = CORSOLA_DB_HDMI;
	else
		db = CORSOLA_DB_TYPEC;

	corsola_db_config(db);

	CPRINTS("Detect %s DB", db == CORSOLA_DB_HDMI ? "HDMI" : "TYPEC");
	return db;
}

static int corsola_db_init(const struct device *unused)
{
	ARG_UNUSED(unused);
	corsola_get_db_type();
	return 0;
}
SYS_INIT(corsola_db_init, APPLICATION, HOOK_PRIO_PRE_I2C);
