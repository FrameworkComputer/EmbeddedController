/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola daughter board detection */

#include "console.h"
#include "gpio.h"
#include "hooks.h"

#include "variant_db_detection.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

static void corsola_db_config(enum corsola_db_type type)
{
	switch (type) {
	case CORSOLA_DB_HDMI:
		/* EC_X_GPIO1 */
		gpio_set_flags(GPIO_EN_HDMI_PWR, GPIO_OUT_HIGH);
		/* X_EC_GPIO2 */
		gpio_set_flags(GPIO_PS185_EC_DP_HPD, GPIO_INT_BOTH);
		/* EC_X_GPIO3 */
		gpio_set_flags(GPIO_PS185_PWRDN_ODL, GPIO_ODR_HIGH);
		return;
	case CORSOLA_DB_TYPEC:
		/* EC_X_GPIO1 */
		gpio_set_flags(GPIO_USB_C1_FRS_EN, GPIO_OUT_LOW);
		/* X_EC_GPIO2 */
		gpio_set_flags(GPIO_USB_C1_PPC_INT_ODL,
			       GPIO_INT_BOTH | GPIO_PULL_UP);
		/* EC_X_GPIO3 */
		gpio_set_flags(GPIO_USB_C1_DP_IN_HPD, GPIO_OUT_LOW);
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

	if (!gpio_get_level(GPIO_HDMI_PRSNT_ODL))
		db = CORSOLA_DB_HDMI;
	else
		db = CORSOLA_DB_TYPEC;

	corsola_db_config(db);

	CPRINTS("Detect %s DB", db == CORSOLA_DB_HDMI ? "HDMI" : "TYPEC");
	return db;
}

static void corsola_db_init(void)
{
	corsola_get_db_type();
}
DECLARE_HOOK(HOOK_INIT, corsola_db_init, HOOK_PRIO_INIT_I2C - 1);
