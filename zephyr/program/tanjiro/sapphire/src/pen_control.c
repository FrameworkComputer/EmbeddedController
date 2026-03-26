/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "peripheral_charger.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>

LOG_MODULE_REGISTER(pen_control, LOG_LEVEL_ERR);

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

__override void board_pchg_power_on(int port, bool on)
{
	if (port != 0)
		return;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pp5000_wlc_en), on);
}

uint32_t board_version = -1;
static void stylus_int_init(void)
{
	if (cbi_get_board_version(&board_version) != EC_SUCCESS) {
		LOG_ERR("Failed to get board version.");
		/* force enable stylus hall sensor detect function */
		board_version = 2;
	}

	if (board_version <= 1) {
		/* for EVT board, set interrupt always enable */
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_pen_pres),
				      GPIO_OUTPUT);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pen_pres), false);
	}
}
DECLARE_HOOK(HOOK_INIT, stylus_int_init, HOOK_PRIO_PRE_DEFAULT);

timestamp_t next_check_ts;
static bool startup_ok = false;
static bool recharging = false;
static int soc_fail_cnt;

#define SOC_FAIL_MAX 20
#define SOC_LOW 75
#define CHECK_INTERVAL_MS (20ULL * 60 * USEC_PER_SEC)

static void pchg_board_update(void);
DECLARE_DEFERRED(pchg_board_update);

static void pchg_board_update()
{
	timestamp_t now = get_time();
	int soc;

	int system_off =
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF);

	if (board_version < 2)
		return;

	/* stylus removed or stylus don't charge */
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pen_pres)) ||
	    (system_off && !extpower_is_present())) {
		pchg_shutdown();
		startup_ok = false;
		recharging = false;
		next_check_ts.val = 0;

		hook_call_deferred(&pchg_board_update_data, -1);
		CPRINTS("stylus: remove");
		return;
	}

	/* wait 20m delay */
	if (now.val < next_check_ts.val) {
		hook_call_deferred(&pchg_board_update_data, USEC_PER_SEC);
		return;
	}

	/* power on wireless charge */
	if (!startup_ok) {
		CPRINTS("stylus: after 20m check battery level");
		pchg_startup();
		startup_ok = true;
	}

	/* read stylus percent */
	soc = pchg_get_battery_percent(0);
	if (soc <= 0 || soc > 100) {
		soc_fail_cnt++;
		CPRINTS("stylus: soc read fail (%d)", soc_fail_cnt);
		if (soc_fail_cnt >= SOC_FAIL_MAX) {
			if (startup_ok) {
				pchg_shutdown();
				startup_ok = false;
			}
			soc_fail_cnt = 0;
		}
		hook_call_deferred(&pchg_board_update_data, USEC_PER_SEC);
		return;
	}
	soc_fail_cnt = 0;

	/* recharge begin */
	if (!recharging && soc < SOC_LOW) {
		recharging = true;
		CPRINTS("stylus: enter recharge soc=%d", soc);
	}

	if (recharging) {
		/* keep power on */
		if (!startup_ok) {
			pchg_startup();
			startup_ok = true;
		}
	} else {
		if (startup_ok) {
			pchg_shutdown();
			startup_ok = false;
			CPRINTS("stylus: charge full then discharge");
		}
	}

	next_check_ts.val = now.val + CHECK_INTERVAL_MS;
	hook_call_deferred(&pchg_board_update_data, USEC_PER_SEC);
}

__override void board_pchg_end_strategy()
{
	next_check_ts.val = get_time().val + CHECK_INTERVAL_MS;
	pchg_shutdown();
	startup_ok = false;
	recharging = false;
	CPRINTS("stylus: board charge strategy begin");
	hook_call_deferred(&pchg_board_update_data, USEC_PER_SEC);
}
