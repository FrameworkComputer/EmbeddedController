/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "button.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/als_tcs3400.h"
#include "fw_config.h"
#include "hooks.h"
#include "lid_switch.h"
#include "peripheral_charger.h"
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

/* Battery discharging over-current limit is 8A */
#define BATT_OC_LIMIT -8000

/* PCHG control */
#ifdef SECTION_IS_RW
extern struct pchg_drv ctn730_drv;

struct pchg pchgs[] = {
	[0] = {
		.cfg = &(const struct pchg_config) {
			.drv = &ctn730_drv,
			.i2c_port = I2C_PORT_WLC,
			.irq_pin = GPIO_PEN_INT_ODL,
			.full_percent = 96,
			.block_size = 128,
		},
		.events = QUEUE_NULL(PCHG_EVENT_QUEUE_SIZE, enum pchg_event),
	},
};
const int pchg_count = ARRAY_SIZE(pchgs);
#endif

/******************************************************************************/
/* USB-A charging control */

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USBA_R,
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

/******************************************************************************/

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	/* Allow keyboard backlight to be enabled */
	gpio_set_level(GPIO_EC_KB_BL_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/* Turn off the keyboard backlight if it's on. */
	gpio_set_level(GPIO_EC_KB_BL_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

enum battery_present battery_hw_present(void)
{
	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(GPIO_EC_BATT_PRES_ODL) ? BP_NO : BP_YES;
}

int charger_profile_override(struct charge_state_data *curr)
{
	/* Turn off haptic pad LRA if battery discharing current over 8A */
	if (!(curr->batt.flags & BATT_FLAG_BAD_CURRENT) &&
	    curr->batt.current < BATT_OC_LIMIT)
		gpio_set_level(GPIO_LRA_DIS_ODL, 0);
	else
		gpio_set_level(GPIO_LRA_DIS_ODL, 1);

	return 0;
}

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}
