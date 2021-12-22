/* Copyright 2021 The Chromium OS Authors. All rights reserved.
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
#include "driver/als_tcs3400.h"
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

__override void board_cbi_init(void)
{
	config_usb_db_type();
}

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	/* Allow keyboard backlight to be enabled */

	if (get_board_id() == 1)
		gpio_set_level(GPIO_ID_1_EC_KB_BL_EN, 1);
	else
		gpio_set_level(GPIO_EC_KB_BL_EN_L, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/* Turn off the keyboard backlight if it's on. */

	if (get_board_id() == 1)
		gpio_set_level(GPIO_ID_1_EC_KB_BL_EN, 0);
	else
		gpio_set_level(GPIO_EC_KB_BL_EN_L, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/*
 * Explicitly apply the board ID 1 *gpio.inc settings to pins that
 * were reassigned on current boards.
 */

static void set_board_id_1_gpios(void)
{
	if (get_board_id() != 1)
		return;

	gpio_set_flags(GPIO_ID_1_EC_KB_BL_EN, GPIO_OUT_LOW);
}
DECLARE_HOOK(HOOK_INIT, set_board_id_1_gpios, HOOK_PRIO_FIRST);

/*
 * Reclaim GPIO pins on board ID 1 that are used as ADC inputs on
 * current boards. ALT function group MODULE_ADC pins are set in
 * HOOK_PRIO_INIT_ADC and can be reclaimed right after the hook runs.
 */

static void board_id_1_reclaim_adc(void)
{
	if (get_board_id() != 1)
		return;

	/*
	 * GPIO_ID_1_USB_C0_C2_TCPC_RST_ODL is on GPIO34
	 *
	 * The TCPC has already been reset by board_tcpc_init() executed
	 * from HOOK_PRIO_INIT_CHIPSET. Later, the pin gets set to ADC6
	 * in HOOK_PRIO_INIT_ADC, so we simply need to set the pin back
	 * to GPIO34.
	 */
	gpio_set_flags(GPIO_ID_1_USB_C0_C2_TCPC_RST_ODL, GPIO_ODR_HIGH);
	gpio_set_alternate_function(GPIO_PORT_3, BIT(4), GPIO_ALT_FUNC_NONE);

	/*
	 * The pin gets set to ADC7 in HOOK_PRIO_INIT_ADC, so we simply
	 * need to set it back to GPIOE1.
	 */
	gpio_set_flags(GPIO_ID_1_EC_BATT_PRES_ODL, GPIO_INPUT);
	gpio_set_alternate_function(GPIO_PORT_E, BIT(1), GPIO_ALT_FUNC_NONE);
}
DECLARE_HOOK(HOOK_INIT, board_id_1_reclaim_adc, HOOK_PRIO_INIT_ADC + 1);
