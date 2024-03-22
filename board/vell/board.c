/* Copyright 2021 The ChromiumOS Authors
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
#include "driver/als_tcs3400.h"
#include "fw_config.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "lid_switch.h"
#include "panic.h"
#include "power.h"
#include "power/intel_x86.h"
#include "power_button.h"
#include "registers.h"
#include "switch.h"
#include "throttle_ap.h"
#include "usbc_config.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

static void board_chipset_startup(void)
{
	/* Allow keyboard backlight to be enabled */

	gpio_set_level(GPIO_EC_KB_BL_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

static void board_chipset_shutdown(void)
{
	/* Turn off the keyboard backlight if it's on. */

	gpio_set_level(GPIO_EC_KB_BL_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

static void set_board_id_5_gpios(void)
{
	if (get_board_id() < 6) {
		power_signal_list[X86_ALL_SYS_PGOOD].gpio =
			GPIO_ID_5_SEQ_EC_ALL_SYS_PG;
	}
}
DECLARE_HOOK(HOOK_INIT, set_board_id_5_gpios, HOOK_PRIO_POST_FIRST);

__override int board_get_all_sys_pgood(void)
{
	/*
	 * board_id < 6 uses GPIO D7, which does not support interrupts. So
	 * power_signal_interrupt is not triggered when the pin changes, and the
	 * common power code state is not updated. So we need to read the gpio
	 * directly instead of using power_get_signals(), etc.
	 */
	if (get_board_id() < 6)
		return gpio_get_level(GPIO_ID_5_SEQ_EC_ALL_SYS_PG);

	return gpio_get_level(GPIO_PG_EC_ALL_SYS_PWRGD);
}
