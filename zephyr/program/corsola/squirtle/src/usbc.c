/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "hooks.h"
#include "usb_pd.h"
#include "usb_tc_sm.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USB, format, ##args)

#define BATT_LVL_CURRENT_LIMITED 30 /* Battery percent(%) */

#define PDO_FIXED_FLAGS \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | PDO_FIXED_COMM_CAP)

static bool current_limited;

static const uint32_t pd_src_pdo_1A5[] = {
	PDO_FIXED(5000, 1500, PDO_FIXED_FLAGS),
};

static const uint32_t pd_src_pdo_3A[] = {
	PDO_FIXED(5000, 3000, PDO_FIXED_FLAGS),
};

int dpm_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	if (current_limited) {
		*src_pdo = pd_src_pdo_1A5;
		return ARRAY_SIZE(pd_src_pdo_1A5);
	}

	*src_pdo = pd_src_pdo_3A;

	return ARRAY_SIZE(pd_src_pdo_3A);
}

static void update_src_pdo_deferred(void);
DECLARE_DEFERRED(update_src_pdo_deferred);
static void update_src_pdo_deferred(void)
{
	int i;
	static int check_cnt;

	if (chipset_in_state(CHIPSET_STATE_SUSPEND) &&
	    (charge_get_percent() < BATT_LVL_CURRENT_LIMITED)) {
		/* In S3, battery < 30%, set src pdo to 1A5 */
		current_limited = true;

		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			if (tc_is_attached_src(i)) {
				CPRINTS("Set C%d src pdo 1A5", i);
				pd_update_contract(i);
			}
		}

		hook_call_deferred(&update_src_pdo_deferred_data, -1);
	} else if (chipset_in_state(CHIPSET_STATE_SUSPEND) &&
		   (charge_get_percent() >= BATT_LVL_CURRENT_LIMITED)) {
		/* In S3, battery >= 30%, check the battery every 60s */
		hook_call_deferred(&update_src_pdo_deferred_data, 60 * SECOND);
	} else if (chipset_in_state(CHIPSET_STATE_ON)) {
		/* Resume src pdo to 3A */
		current_limited = false;

		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			if (tc_is_attached_src(i))
				pd_update_contract(i);
		}

		hook_call_deferred(&update_src_pdo_deferred_data, -1);
	} else if (check_cnt < 3) {
		/* Check 3 times for stable power state */
		check_cnt++;
		hook_call_deferred(&update_src_pdo_deferred_data, 10 * SECOND);
	} else {
		check_cnt = 0;
		current_limited = false;
		hook_call_deferred(&update_src_pdo_deferred_data, -1);
	}
}

static void check_src_port(void)
{
	int i;

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (tc_is_attached_src(i)) {
			/* Deferred 2s to avoid pd state conflict */
			hook_call_deferred(&update_src_pdo_deferred_data,
					   2 * SECOND);
			break;
		}
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, check_src_port, HOOK_PRIO_DEFAULT);

static void resume_src_port(void)
{
	/* Deferred 5s to avoid pd state conflict */
	hook_call_deferred(&update_src_pdo_deferred_data, 5 * SECOND);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, resume_src_port, HOOK_PRIO_DEFAULT);
