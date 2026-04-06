/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_backlight.h"
#include "system.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define VALUE_NODE_PRESENT DT_NODELABEL(ufsc_kb_backlight_present)

/* Declare the function we want to mock from the CBI subsystem. */
FAKE_VALUE_FUNC(bool, cros_cbi_ufsc_check_match, enum cbi_ufsc_value_id);

static bool mock_ufsc_kb_backlight_present;

static bool mock_cbi_ufsc_check_match(enum cbi_ufsc_value_id value_id)
{
	if (value_id == CBI_UFSC_VALUE_ID(VALUE_NODE_PRESENT)) {
		return mock_ufsc_kb_backlight_present;
	}
	/* Default to false for any other UFSC checks in the system */
	return false;
}

static void ufsc_kb_backlight_before(void *data)
{
	ARG_UNUSED(data);

	/* Replace the real function with our custom fake. */
	cros_cbi_ufsc_check_match_fake.custom_fake = mock_cbi_ufsc_check_match;

	/*
	 * Reset the backlight module to its default "on" state. The 'enabled'
	 * variable inside keyboard_backlight.c is static and its state
	 * persists between test runs.
	 */
	kblight_enable(1);
}

ZTEST_SUITE(ufsc_kb_backlight, NULL, NULL, ufsc_kb_backlight_before, NULL,
	    NULL);

ZTEST(ufsc_kb_backlight, test_kblight_present)
{
	/* Emulate the KB backlight check matched. */
	mock_ufsc_kb_backlight_present = true;

	hook_notify(HOOK_INIT);
	zassert_true(kblight_get_current_enable(), "kblight should be enabled");
	zassert_true((get_feature_flags0() &
		      EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB)),
		     "EC_FEATURE_PWM_KEYB should be present in flags");
}

ZTEST(ufsc_kb_backlight, test_kblight_absent)
{
	/* Emulate the KB backlight check mismatched. */
	mock_ufsc_kb_backlight_present = false;

	hook_notify(HOOK_INIT);
	zassert_false(kblight_get_current_enable(),
		      "kblight should be disabled");
	zassert_false((get_feature_flags0() &
		       EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB)),
		      "EC_FEATURE_PWM_KEYB should be absent from flags");
}
