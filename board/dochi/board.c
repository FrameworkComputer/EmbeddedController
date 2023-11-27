/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_fuel_gauge.h"
#include "button.h"
#include "cbi.h"
#include "charge_ramp.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_lsm6dso.h"
#include "fw_config.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "switch.h"
#include "tablet_mode.h"
#include "throttle_ap.h"
#include "usbc_config.h"
#include "util.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

/* touch panel power sequence control */
#define TOUCH_ENABLE_DELAY_MS (500 * MSEC)
#define TOUCH_DISABLE_DELAY_MS (0 * MSEC)

__override void board_cbi_init(void)
{
	config_usb_db_type();
}

void touch_disable(void)
{
	gpio_set_level(GPIO_EC_TOUCH_EN, 0);
}
DECLARE_DEFERRED(touch_disable);

void touch_enable(void)
{
	gpio_set_level(GPIO_EC_TOUCH_EN, 1);
}
DECLARE_DEFERRED(touch_enable);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	/* Allow keyboard backlight to be enabled */
	if (IS_ENABLED(CONFIG_PWM_KBLIGHT))
		gpio_set_level(GPIO_EC_KB_BL_EN_L, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/* Turn off the keyboard backlight if it's on. */
	if (IS_ENABLED(CONFIG_PWM_KBLIGHT))
		gpio_set_level(GPIO_EC_KB_BL_EN_L, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void pogo_chipset_shutdown(void)
{
	/* Cancel touch_enable touch_enable touch_disable_hook. */
	hook_call_deferred(&touch_enable_data, -1);
	hook_call_deferred(&touch_disable_data, -1);

	gpio_set_level(GPIO_EC_TOUCH_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pogo_chipset_shutdown, HOOK_PRIO_DEFAULT);

__override int board_get_leave_safe_mode_delay_ms(void)
{
	const struct batt_conf_embed *batt = get_batt_conf();

	/* If it's COSMX battery, there's need more delay time. */
	if (!strcasecmp(batt->manuf_name, "COSMX KT0030B002") ||
	    !strcasecmp(batt->manuf_name, "COSMX KT0030B004"))
		return 2000;
	else
		return 500;
}

void pch_edp_bl_interrupt(enum gpio_signal signal)
{
	int state;

	if (signal != GPIO_PCH_EDP_BL_EN)
		return;

	/* Wait until host hub INTR# signal is asserted */
	state = gpio_get_level(GPIO_PCH_EDP_BL_EN);

	CPRINTS("%s: %d", __func__, state);

	if (state) {
		hook_call_deferred(&touch_disable_data, -1);
		hook_call_deferred(&touch_enable_data, TOUCH_ENABLE_DELAY_MS);
	} else {
		hook_call_deferred(&touch_enable_data, -1);
		hook_call_deferred(&touch_disable_data, TOUCH_DISABLE_DELAY_MS);
	}
}

static void touch_enable_init(void)
{
	if (ec_cfg_panel_power_ec_control() == PANEL_POWER_EC_CONTROL_ENABLE)
		gpio_enable_interrupt(GPIO_PCH_EDP_BL_EN);
}
DECLARE_HOOK(HOOK_INIT, touch_enable_init, HOOK_PRIO_DEFAULT);
