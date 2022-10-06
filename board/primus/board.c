/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "button.h"
#include "charge_ramp.h"
#include "charger.h"
#include "common.h"
#include "charge_manager.h"
#include "charge_state_v2.h"
#include "compile_time_macros.h"
#include "console.h"
#include "fw_config.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "lid_switch.h"
#include "power_button.h"
#include "power.h"
#include "pwm.h"
#include "registers.h"
#include "switch.h"
#include "throttle_ap.h"
#include "usbc_config.h"
#include "util.h"

#include "gpio_list.h" /* Must come after other header files. */

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

#define KBLIGHT_LED_ON_LVL 100
#define KBLIGHT_LED_OFF_LVL 0

#define PD_MAX_SUSPEND_CURRENT_MA 3000

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
	pwm_set_duty(PWM_CH_KBLIGHT, KBLIGHT_LED_ON_LVL);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/* Turn off the keyboard backlight if it's on. */
	pwm_set_duty(PWM_CH_KBLIGHT, KBLIGHT_LED_OFF_LVL);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

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
		CPRINTS("%s: port %d: vbus %d lower than %d", __func__, port,
			voltage, BC12_MIN_VOLTAGE);
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

static void keyboard_init(void)
{
	/*
	 * Set T15(KSI0/KSO11) to Lock key(KSI3/KSO9)
	 */
	set_scancode_set2(0, 11, get_scancode_set2(3, 9));
}
DECLARE_HOOK(HOOK_INIT, keyboard_init, HOOK_PRIO_DEFAULT);

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	/*
	 * Need to set different input current limit depend on system state.
	 * Guard adapter plug/ un-plug here.
	 */

	if (((max_ma == PD_MAX_CURRENT_MA) &&
	     chipset_in_state(CHIPSET_STATE_ANY_OFF)) ||
	    (max_ma != PD_MAX_CURRENT_MA))
		charge_ma = charge_ma * 97 / 100;
	else
		charge_ma = charge_ma * 93 / 100;

	charge_set_input_current_limit(
		MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}

static void configure_input_current_limit(void)
{
	/*
	 * If adapter == 3250mA, we need system be charged at 3150mA in S5.
	 * And system be charged at 3000mA in S0.
	 */
	int adapter_current_ma;
	int adapter_current_mv;
	/* Get adapter voltage/ current */
	adapter_current_mv = charge_manager_get_charger_voltage();
	adapter_current_ma = charge_manager_get_charger_current();

	if ((adapter_current_ma == PD_MAX_CURRENT_MA) &&
	    chipset_in_or_transitioning_to_state(CHIPSET_STATE_SUSPEND))
		adapter_current_ma = PD_MAX_SUSPEND_CURRENT_MA;
	else
		adapter_current_ma = adapter_current_ma * 97 / 100;

	charge_set_input_current_limit(MAX(adapter_current_ma,
					   CONFIG_CHARGER_INPUT_CURRENT),
				       adapter_current_mv);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, configure_input_current_limit,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN_COMPLETE, configure_input_current_limit,
	     HOOK_PRIO_DEFAULT);
