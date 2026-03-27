/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "acok_emul.h"
#include "extpower.h"
#include "hooks.h"
#include "host_command.h"
#include "lpc.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

static int ac_hook_count;

static void before_and_after(void *unused)
{
	if (IS_ENABLED(HAS_TASK_HOSTCMD)) {
		host_clear_events(0xFFFFFFFF);
	}
}

static void test_ac_change_hook(void)
{
	ac_hook_count++;
}
DECLARE_HOOK(HOOK_AC_CHANGE, test_ac_change_hook, HOOK_PRIO_DEFAULT);

ZTEST(extpower, test_extpower_gpio)
{
	set_ac(0, true);
	ac_hook_count = 0;

	set_ac(1, true);
	zassert_equal(ac_hook_count, 1);
	zassert_equal(extpower_is_present(), 1);

	if (IS_ENABLED(HAS_TASK_HOSTCMD)) {
		zassert_true(host_is_event_set(EC_HOST_EVENT_AC_CONNECTED));
	}

	set_ac(0, true);
	zassert_equal(ac_hook_count, 2);
	zassert_equal(extpower_is_present(), 0);

	if (IS_ENABLED(HAS_TASK_HOSTCMD)) {
		zassert_true(host_is_event_set(EC_HOST_EVENT_AC_DISCONNECTED));
	}
}

ZTEST(extpower, test_extpower_gpio_debounce)
{
	/* Verify that changes to AC OK that are less than the debounce time
	 * do not generate HOOK or HOSTCMD events.
	 */
	set_ac(0, true);
	ac_hook_count = 0;

	set_ac(1, false);
	k_msleep(CONFIG_PLATFORM_EC_EXTPOWER_DEBOUNCE_MS / 2);
	set_ac(0, true);

	zassert_equal(ac_hook_count, 0);

	if (IS_ENABLED(HAS_TASK_HOSTCMD)) {
		zassert_false(host_is_event_set(EC_HOST_EVENT_AC_CONNECTED));
		zassert_false(host_is_event_set(EC_HOST_EVENT_AC_DISCONNECTED));
	}
}

#ifdef HAS_TASK_HOSTCMD
ZTEST(extpower, test_extpower_ap_startup)
{
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	host_clear_events(0xFFFFFFFF);

	/* Verify that the EC posts an AC connect/disconnect event
	 * after booting the AP.
	 */
	set_ac(1, true);
	ac_hook_count = 0;
	set_ac(0, true);

	/* Ensure the AC hook has completed before powering up the AP. */
	zassert_equal(ac_hook_count, 1);
	host_clear_events(0xFFFFFFFF);

	hook_notify(HOOK_CHIPSET_STARTUP);

	zassert_false(host_is_event_set(EC_HOST_EVENT_AC_CONNECTED));
	zassert_true(host_is_event_set(EC_HOST_EVENT_AC_DISCONNECTED));

	hook_notify(HOOK_CHIPSET_SHUTDOWN);

	/* Verify that the EC posts an AC connect/discoonect event
	 * after booting the AP.
	 */
	ac_hook_count = 0;
	set_ac(1, true);

	/* Ensure the AC hook has completed before powering up the AP. */
	zassert_equal(ac_hook_count, 1);
	host_clear_events(0xFFFFFFFF);

	hook_notify(HOOK_CHIPSET_STARTUP);

	zassert_true(host_is_event_set(EC_HOST_EVENT_AC_CONNECTED));
	zassert_false(host_is_event_set(EC_HOST_EVENT_AC_DISCONNECTED));
}
#endif

ZTEST_SUITE(extpower, NULL, NULL, before_and_after, before_and_after, NULL);
