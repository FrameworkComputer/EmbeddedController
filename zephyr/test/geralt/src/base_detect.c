/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "base_state.h"
#include "hooks.h"
#include "test_state.h"

#include <zephyr/devicetree/io-channels.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

static void set_base_detect_adc(int voltage)
{
	const struct device *adc_dev =
		DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(DT_NODELABEL(adc_base_det)));
	const int channel = DT_IO_CHANNELS_INPUT(DT_NODELABEL(adc_base_det));

	adc_emul_const_value_set(adc_dev, channel, voltage);
}

ZTEST(base_detect, test_s0_attach_detach)
{
	hook_notify(HOOK_CHIPSET_STARTUP);
	k_sleep(K_SECONDS(1));

	set_base_detect_adc(0);
	k_sleep(K_SECONDS(1));
	zassert_true(base_get_state());

	set_base_detect_adc(3300);
	k_sleep(K_SECONDS(1));
	zassert_false(base_get_state());
}

ZTEST(base_detect, test_g3_attach_detach)
{
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	k_sleep(K_SECONDS(1));

	/* base is always detached in g3 */
	set_base_detect_adc(0);
	k_sleep(K_SECONDS(1));
	zassert_false(base_get_state());

	set_base_detect_adc(3300);
	k_sleep(K_SECONDS(1));
	zassert_false(base_get_state());
}

ZTEST(base_detect, test_force_state)
{
	base_force_state(EC_SET_BASE_STATE_ATTACH);

	/* after base_force_state(), adc should not change base state */
	set_base_detect_adc(3300);
	k_sleep(K_SECONDS(1));
	zassert_true(base_get_state());

	base_force_state(EC_SET_BASE_STATE_DETACH);

	/* after base_force_state(), adc should not change base state */
	set_base_detect_adc(0);
	k_sleep(K_SECONDS(1));
	zassert_false(base_get_state());
}

static void base_state_before(void *fixture)
{
	set_base_detect_adc(3300);
	base_force_state(EC_SET_BASE_STATE_RESET);
	k_sleep(K_SECONDS(1));
}

ZTEST_SUITE(base_detect, geralt_predicate_post_main, NULL, base_state_before,
	    NULL, NULL);
