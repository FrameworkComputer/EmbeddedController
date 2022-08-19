/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola daughter board detection */
#include <zephyr/drivers/gpio.h>

#include "console.h"
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

#include "variant_db_detection.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

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
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_usb_c1_dp_in_hpd),
				      GPIO_OUTPUT_LOW);
		return;
	case CORSOLA_DB_NONE:
		/* Set floating pins as input with PU to prevent leakage */
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_x_gpio1),
				      GPIO_INPUT | GPIO_PULL_UP);
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_x_ec_gpio2),
				      GPIO_INPUT | GPIO_PULL_UP);
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_x_gpio3),
				      GPIO_INPUT | GPIO_PULL_UP);
		return;
	default:
		break;
	}
}

enum corsola_db_type corsola_get_db_type(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(db_config))
	int ret;
	uint32_t val;
#endif
	static enum corsola_db_type db = CORSOLA_DB_UNINIT;

	if (db != CORSOLA_DB_UNINIT) {
		return db;
	}

	if (!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_hdmi_prsnt_odl))) {
		db = CORSOLA_DB_HDMI;
	} else {
		db = CORSOLA_DB_TYPEC;
	}

/* Detect for no sub board case by FW_CONFIG */
#if DT_NODE_EXISTS(DT_NODELABEL(db_config))
	ret = cros_cbi_get_fw_config(DB, &val);
	if (ret != 0) {
		CPRINTS("Error retrieving CBI FW_CONFIG field %d", DB);
	} else if (val == DB_NONE) {
		db = CORSOLA_DB_NONE;
	}
#endif

	corsola_db_config(db);

	switch (db) {
	case CORSOLA_DB_NONE:
		CPRINTS("Detect %s DB", "NONE");
		break;
	case CORSOLA_DB_TYPEC:
		CPRINTS("Detect %s DB", "TYPEC");
		break;
	case CORSOLA_DB_HDMI:
		CPRINTS("Detect %s DB", "HDMI");
		break;
	default:
		CPRINTS("DB UNINIT");
		break;
	}

	return db;
}

static void corsola_db_init(void)
{
	corsola_get_db_type();
}
DECLARE_HOOK(HOOK_INIT, corsola_db_init, HOOK_PRIO_PRE_I2C);
