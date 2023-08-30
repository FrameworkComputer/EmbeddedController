/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "charger.h"
#include "cros_cbi.h"
#include "hooks.h"

#include <zephyr/devicetree.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VOID_FUNC(chg_enable_alternate_test, int);

static bool alt_charger;
static int cros_cbi_get_fw_config_mock(enum cbi_fw_config_field_id field_id,
				       uint32_t *value)
{
	if (field_id != FW_CHARGER)
		return -EINVAL;

	*value = alt_charger ? FW_CHARGER_ISL9538 : FW_CHARGER_ISL9241;
	return 0;
}

static void alt_charger_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(chg_enable_alternate_test);

	cros_cbi_get_fw_config_fake.custom_fake = cros_cbi_get_fw_config_mock;
}

ZTEST_SUITE(alt_charger_common, NULL, NULL, alt_charger_before, NULL, NULL);

ZTEST(alt_charger_common, test_normal_charger)
{
	alt_charger = false;
	hook_notify(HOOK_INIT);
	/* Test that the alternative charger wasn't enabled. */
	zassert_equal(chg_enable_alternate_test_fake.call_count, 0);
}

ZTEST(alt_charger_common, test_alt_charger)
{
	alt_charger = true;
	hook_notify(HOOK_INIT);
	zassert_equal(chg_enable_alternate_test_fake.call_count, 1);
	zassert_equal(chg_enable_alternate_test_fake.arg0_val, 0);
}
