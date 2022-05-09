/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "button.h"
#include "charge_ramp.h"
#include "charger.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_lsm6dso.h"
#include "fw_config.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power_button.h"
#include "power.h"
#include "registers.h"
#include "switch.h"
#include "tablet_mode.h"
#include "throttle_ap.h"
#include "usbc_config.h"

#include "gpio_list.h" /* Must come after other header files. */

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

/******************************************************************************/
/* USB-A charging control */

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USBA_R,
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

/******************************************************************************/

__override void board_cbi_init(void)
{
	config_usb_db_type();
}

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	/* Allow keyboard backlight to be enabled */
	gpio_set_level(GPIO_EC_KB_BL_EN_L, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/* Turn off the keyboard backlight if it's on. */
	gpio_set_level(GPIO_EC_KB_BL_EN_L, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	pen_config();
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_CHARGE_RAMP_SW

/*
 * TODO(b/181508008): tune this threshold
 */

#define BC12_MIN_VOLTAGE 4400

/**
 * Return true if VBUS is too low
 */
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	int voltage;

	if (charger_get_vbus_voltage(port, &voltage))
		voltage = 0;

	if (voltage == 0) {
		CPRINTS("%s: must be disconnected", __func__);
		return 1;
	}

	if (voltage < BC12_MIN_VOLTAGE) {
		CPRINTS("%s: port %d: vbus %d lower than %d", __func__,
			port, voltage, BC12_MIN_VOLTAGE);
		return 1;
	}

	return 0;
}

#endif /* CONFIG_CHARGE_RAMP_SW */

enum battery_present battery_hw_present(void)
{
	enum gpio_signal batt_pres;

	batt_pres = GPIO_EC_BATT_PRES_ODL;

	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(batt_pres) ? BP_NO : BP_YES;
}

static void board_init(void)
{
	if (ec_cfg_usb_db_type() == DB_USB4_NCT3807)
		db_update_usb4_config_from_config();

	if (ec_cfg_usb_mb_type() == MB_USB4_TBT)
		mb_update_usb4_tbt_config_from_config();

	if (ec_cfg_stylus() == STYLUS_PRSENT)
		gpio_enable_interrupt(GPIO_PEN_DET_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);


/**
 * Deferred function to handle pen detect change
 */
static void pendetect_deferred(void)
{
	static int debounced_pen_detect;
	int pen_detect = !gpio_get_level(GPIO_PEN_DET_ODL);

	if (pen_detect == debounced_pen_detect)
		return;

	debounced_pen_detect = pen_detect;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF))
		gpio_set_level(GPIO_EN_PP5000_PEN, debounced_pen_detect);
}
DECLARE_DEFERRED(pendetect_deferred);
DECLARE_HOOK(HOOK_INIT, pendetect_deferred, HOOK_PRIO_DEFAULT);

void pen_detect_interrupt(enum gpio_signal s)
{
	/* Trigger deferred notification of pen detect change */
	hook_call_deferred(&pendetect_deferred_data,
			500 * MSEC);
}

void pen_config(void)
{
	if (ec_cfg_stylus() == STYLUS_PRSENT) {
		/* Make sure pen detection is triggered or not at resume */
		if (!gpio_get_level(GPIO_PEN_DET_ODL))
			gpio_set_level(GPIO_EN_PP5000_PEN, 1);
		else
			gpio_set_level(GPIO_EN_PP5000_PEN, 0);
	}
}

static void board_chipset_shutdown(void)
{
	gpio_set_level(GPIO_EN_PP5000_PEN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);
