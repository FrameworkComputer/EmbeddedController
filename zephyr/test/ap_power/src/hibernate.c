/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "test_mocks.h"
#include "test_state.h"

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#include <ap_power/ap_power_interface.h>

ZTEST(hibernate, test_g3_hibernate)
{
	extpower_is_present_fake.return_val = 0;
	ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
	k_sleep(K_SECONDS(30));
	zassert_equal(1, system_hibernate_fake.call_count);
}

ZTEST(hibernate, test_ac_changed)
{
	extpower_is_present_fake.return_val = 1;
	hook_notify(HOOK_AC_CHANGE);
	k_sleep(K_SECONDS(30));
	zassert_equal(0, system_hibernate_fake.call_count);
	extpower_is_present_fake.return_val = 0;
	hook_notify(HOOK_AC_CHANGE);
	k_sleep(K_SECONDS(30));
	zassert_equal(1, system_hibernate_fake.call_count);
}

ZTEST_SUITE(hibernate, ap_power_predicate_post_main, NULL, NULL, NULL, NULL);
