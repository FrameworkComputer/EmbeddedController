/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "fw_config.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "keyboard_raw.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "rgb_keyboard.h"
#include "switch.h"
#include "throttle_ap.h"
#include "usbc_config.h"
#include "util.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

/******************************************************************************/
/* USB-A charging control */

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USBA_R,
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

/******************************************************************************/

const struct rgbkbd_init rgbkbd_init_taniks = {
	.gcc = RGBKBD_MAX_GCC_LEVEL / 2,
	.scale = { .r = 190, .g = 255, .b = 255 },
	.color = { .r = 255, .g = 255, .b = 255 },
};

__override void board_cbi_init(void)
{
	config_usb_db_type();
}

void board_init(void)
{
	rgbkbd_register_init_setting(&rgbkbd_init_taniks);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

__override void board_kblight_shutdown(void)
{
	gpio_set_level(GPIO_EC_KB_BL_EN_L, 1);
}

__override void board_kblight_init(void)
{
	gpio_set_level(GPIO_RGBKBD_SDB_L, 1);
	gpio_set_level(GPIO_EC_KB_BL_EN_L, 0);
	msleep(10);
}

#ifdef CONFIG_CHARGE_RAMP_SW

/*
 * TODO: tune this threshold
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
		CPRINTS("%s: port %d: vbus %d lower than %d", __func__, port,
			voltage, BC12_MIN_VOLTAGE);
		return 1;
	}

	return 0;
}

#endif /* CONFIG_CHARGE_RAMP_SW */

enum battery_present battery_hw_present(void)
{
	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(GPIO_EC_BATT_PRES_ODL) ? BP_NO : BP_YES;
}
