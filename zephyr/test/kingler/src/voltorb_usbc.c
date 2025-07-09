/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "chipset.h"
#include "hooks.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define PDO_FIXED_FLAGS \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | PDO_FIXED_COMM_CAP)

FAKE_VALUE_FUNC(int, charge_get_percent);
FAKE_VALUE_FUNC(int, chipset_in_state, int);
FAKE_VALUE_FUNC(int, tc_is_attached_src, int);

FAKE_VOID_FUNC(x_ec_interrupt);
FAKE_VOID_FUNC(bmi3xx_interrupt);
FAKE_VOID_FUNC(pd_update_contract, int);
FAKE_VOID_FUNC(check_src_port);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, check_src_port, HOOK_PRIO_DEFAULT);
FAKE_VOID_FUNC(resume_src_port);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, resume_src_port, HOOK_PRIO_DEFAULT);

enum chipset_state_mask fake_chipset_state;

int chipset_in_state_mock(int state_mask)
{
	return state_mask & fake_chipset_state;
}

int tc_is_attached_src_mock(int port)
{
	/* Assume type-c port role is source */
	return 1;
}

uint8_t board_get_usb_pd_port_count(void)
{
	return 2;
}

ZTEST(current_limit, test_check_src_port)
{
	const int fake_port = 0;
	const uint32_t *fake_pdo;

	tc_is_attached_src_fake.custom_fake = tc_is_attached_src_mock;
	chipset_in_state_fake.custom_fake = chipset_in_state_mock;

	charge_get_percent_fake.return_val = 20;
	fake_chipset_state = CHIPSET_STATE_SUSPEND;
	hook_notify(HOOK_CHIPSET_SUSPEND);
	k_sleep(K_SECONDS(3));
	zassert_equal(1, check_src_port_fake.call_count);
	zassert_equal(1, dpm_get_source_pdo(&fake_pdo, fake_port));

	charge_get_percent_fake.return_val = 40;
	fake_chipset_state = CHIPSET_STATE_SUSPEND;
	hook_notify(HOOK_CHIPSET_SUSPEND);
	k_sleep(K_SECONDS(3));
	zassert_equal(2, check_src_port_fake.call_count);

	fake_chipset_state = CHIPSET_STATE_SOFT_OFF;
	hook_notify(HOOK_CHIPSET_SUSPEND);
	k_sleep(K_SECONDS(33));
	zassert_equal(3, check_src_port_fake.call_count);
	zassert_equal(1, dpm_get_source_pdo(&fake_pdo, fake_port));

	/* Assume type-c port role is sink */
	tc_is_attached_src_fake.custom_fake = NULL;
	tc_is_attached_src_fake.return_val = 0;
	hook_notify(HOOK_CHIPSET_SUSPEND);
	k_sleep(K_SECONDS(3));
	zassert_equal(4, check_src_port_fake.call_count);
}

ZTEST(current_limit, test_resume_src_port)
{
	tc_is_attached_src_fake.custom_fake = tc_is_attached_src_mock;
	chipset_in_state_fake.custom_fake = chipset_in_state_mock;
	fake_chipset_state = CHIPSET_STATE_ON;
	hook_notify(HOOK_CHIPSET_RESUME);
	k_sleep(K_SECONDS(3));
	zassert_equal(1, resume_src_port_fake.call_count);
}

ZTEST_SUITE(current_limit, NULL, NULL, NULL, NULL, NULL);
