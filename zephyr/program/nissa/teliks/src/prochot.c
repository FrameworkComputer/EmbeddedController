/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

#define PROC_HOLD_CURR (-4700) /* Prochot control discharge current(mA) */
#define PROC_RLS_CURR (-4000) /* Prochot release discharge current(mA) */
#define BATTCURR_CNT 4 /* Read discharge current counter */

static void update_prochot_deferred(void);
DECLARE_DEFERRED(update_prochot_deferred);
static void update_prochot_deferred(void)
{
	static int read_curr[BATTCURR_CNT];
	static int read_cnt;
	int i;
	int hold_cnt, release_cnt;
	const struct batt_params *batt = charger_current_battery_params();

	/* Read battery discharge current */
	read_curr[read_cnt++] = batt->current;

	if (read_cnt >= BATTCURR_CNT) {
		read_cnt = 0;
	}

	hold_cnt = 0;
	release_cnt = 0;

	for (i = 0; i < BATTCURR_CNT; i++) {
		if (read_curr[i] < PROC_HOLD_CURR) {
			hold_cnt++;
		} else if ((read_curr[i] >= PROC_RLS_CURR)) {
			release_cnt++;
		}
	}

	/* Control action is taken once only after the current is
	 * exceeded BATTCURR_CNT times.
	 */
	if (hold_cnt == BATTCURR_CNT) {
		if (!gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_ec_prochot_odl)))
			goto defferred;

		CPRINTS("Hold prochot!");
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_prochot_odl), 0);
	} else if (release_cnt == BATTCURR_CNT) {
		if (gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_ec_prochot_odl)))
			goto defferred;

		CPRINTS("Release prochot!");
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_prochot_odl), 1);
	}

defferred:
	/* Check the battert discharge current every 500ms */
	hook_call_deferred(&update_prochot_deferred_data, 500 * MSEC);
}

static void check_batt_current(void)
{
	struct battery_static_info *bs = &battery_static[BATT_IDX_MAIN];

	/* Just check the B140435 battery */
	if (strcasecmp(bs->model_ext, "B140435")) {
		CPRINTS("Not B140435");
		hook_call_deferred(&update_prochot_deferred_data, -1);
		return;
	}

	/* Deferred 2s to avoid state conflict */
	hook_call_deferred(&update_prochot_deferred_data, 2 * SECOND);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, check_batt_current, HOOK_PRIO_DEFAULT);

static void stop_check_batt(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_prochot_odl), 1);
	hook_call_deferred(&update_prochot_deferred_data, -1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, stop_check_batt, HOOK_PRIO_DEFAULT);
