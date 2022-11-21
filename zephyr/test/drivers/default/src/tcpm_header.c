/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tcpm/tcpm.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define TCPM_TEST_PORT USBC_PORT_C0

FAKE_VALUE_FUNC(int, set_vconn, int, int);
FAKE_VALUE_FUNC(int, reset_bist_type_2, int);
FAKE_VALUE_FUNC(int, debug_accessory, int, bool);
FAKE_VALUE_FUNC(int, debug_detach, int);
FAKE_VALUE_FUNC(int, hard_reset_reinit, int);
FAKE_VALUE_FUNC(int, set_frs_enable, int, int);

struct tcpm_header_fixture {
	/* The original driver pointer that gets restored after the tests */
	const struct tcpm_drv *saved_driver_ptr;
	/* Mock driver that gets substituted */
	struct tcpm_drv mock_driver;
	/* Saved tcpc config flags that get restored after the tests */
	uint32_t saved_tcpc_flags;
};

ZTEST_F(tcpm_header, test_tcpm_header_drv_set_vconn_failure)
{
	int res;

	tcpc_config[TCPM_TEST_PORT].flags = TCPC_FLAGS_CONTROL_VCONN;

	fixture->mock_driver.set_vconn = set_vconn;
	set_vconn_fake.return_val = -1;

	res = tcpm_set_vconn(TCPM_TEST_PORT, true);

	zassert_true(set_vconn_fake.call_count > 0);
	zassert_equal(-1, res);
}

ZTEST_F(tcpm_header, test_tcpm_header_reset_bist_type_2__unimplemented)
{
	zassert_ok(tcpm_reset_bist_type_2(TCPM_TEST_PORT));
}

ZTEST_F(tcpm_header, test_tcpm_header_reset_bist_type_2__implemented)
{
	int res;
	const int driver_return_code = 7458; /* arbitrary */

	fixture->mock_driver.reset_bist_type_2 = reset_bist_type_2;
	reset_bist_type_2_fake.return_val = driver_return_code;
	res = tcpm_reset_bist_type_2(TCPM_TEST_PORT);

	zassert_equal(1, reset_bist_type_2_fake.call_count);
	zassert_equal(TCPM_TEST_PORT, reset_bist_type_2_fake.arg0_history[0]);
	zassert_equal(driver_return_code, res);
}

ZTEST_F(tcpm_header, test_tcpm_header_debug_accessory__unimplemented)
{
	zassert_ok(tcpm_debug_accessory(TCPM_TEST_PORT, true));
	zassert_ok(tcpm_debug_accessory(TCPM_TEST_PORT, false));
}

ZTEST_F(tcpm_header, test_tcpm_header_debug_accessory__implemented)
{
	int res;
	const int driver_return_code = 7458; /* arbitrary */

	fixture->mock_driver.debug_accessory = debug_accessory;
	debug_accessory_fake.return_val = driver_return_code;
	res = tcpm_debug_accessory(TCPM_TEST_PORT, true);

	zassert_equal(1, debug_accessory_fake.call_count);
	zassert_equal(TCPM_TEST_PORT, debug_accessory_fake.arg0_history[0]);
	zassert_true(debug_accessory_fake.arg1_history[0]);
	zassert_equal(driver_return_code, res);
}

ZTEST_F(tcpm_header, test_tcpm_header_debug_detach__unimplemented)
{
	zassert_ok(tcpm_debug_detach(TCPM_TEST_PORT));
}

ZTEST_F(tcpm_header, test_tcpm_header_debug_detach__implemented)
{
	int res;
	const int driver_return_code = 7458; /* arbitrary */

	fixture->mock_driver.debug_detach = debug_detach;
	debug_detach_fake.return_val = driver_return_code;
	res = tcpm_debug_detach(TCPM_TEST_PORT);

	zassert_equal(1, debug_detach_fake.call_count);
	zassert_equal(TCPM_TEST_PORT, debug_detach_fake.arg0_history[0]);
	zassert_equal(driver_return_code, res);
}

ZTEST_F(tcpm_header, test_tcpm_header_hard_reset_reinit__unimplemented)
{
	int res;

	res = tcpm_hard_reset_reinit(TCPM_TEST_PORT);

	zassert_equal(EC_ERROR_UNIMPLEMENTED, res);
}

ZTEST_F(tcpm_header, test_tcpm_header_hard_reset_reinit__implemented)
{
	int res;
	const int driver_return_code = 7458; /* arbitrary */

	fixture->mock_driver.hard_reset_reinit = hard_reset_reinit;
	hard_reset_reinit_fake.return_val = driver_return_code;
	res = tcpm_hard_reset_reinit(TCPM_TEST_PORT);

	zassert_equal(1, hard_reset_reinit_fake.call_count);
	zassert_equal(TCPM_TEST_PORT, hard_reset_reinit_fake.arg0_history[0]);
	zassert_equal(driver_return_code, res);
}

ZTEST_F(tcpm_header, test_tcpm_header_set_frs_enable__unimplemented)
{
	Z_TEST_SKIP_IFNDEF(CONFIG_PLATFORM_EC_USB_PD_FRS);

	zassert_ok(tcpm_set_frs_enable(TCPM_TEST_PORT, 1));
	zassert_ok(tcpm_set_frs_enable(TCPM_TEST_PORT, 0));
}

ZTEST_F(tcpm_header, test_tcpm_header_set_frs_enable__implemented)
{
	int res;
	const int driver_return_code = 7458; /* arbitrary */

	Z_TEST_SKIP_IFNDEF(CONFIG_PLATFORM_EC_USB_PD_FRS);

	fixture->mock_driver.set_frs_enable = set_frs_enable;
	set_frs_enable_fake.return_val = driver_return_code;
	res = tcpm_set_frs_enable(TCPM_TEST_PORT, 1);

	zassert_equal(1, set_frs_enable_fake.call_count);
	zassert_equal(TCPM_TEST_PORT, set_frs_enable_fake.arg0_history[0]);
	zassert_equal(1, set_frs_enable_fake.arg1_history[0]);
	zassert_equal(driver_return_code, res);
}

ZTEST_F(tcpm_header, test_tcpm_header_tcpc_get_bist_test_mode__unimplemented)
{
	int res;
	bool enabled = true; /* Should be overwritten to false */

	res = tcpc_get_bist_test_mode(TCPM_TEST_PORT, &enabled);

	zassert_equal(EC_ERROR_UNIMPLEMENTED, res);
	zassert_false(enabled);
}

static void *tcpm_header_setup(void)
{
	static struct tcpm_header_fixture fixture;

	return &fixture;
}

static void tcpm_header_before(void *state)
{
	struct tcpm_header_fixture *fixture = state;

	RESET_FAKE(set_vconn);
	RESET_FAKE(reset_bist_type_2);
	RESET_FAKE(debug_accessory);
	RESET_FAKE(debug_detach);
	RESET_FAKE(hard_reset_reinit);
	RESET_FAKE(set_frs_enable);

	fixture->mock_driver = (struct tcpm_drv){ 0 };
	fixture->saved_driver_ptr = tcpc_config[TCPM_TEST_PORT].drv;
	tcpc_config[TCPM_TEST_PORT].drv = &fixture->mock_driver;

	fixture->saved_tcpc_flags = tcpc_config[TCPM_TEST_PORT].flags;
}

static void tcpm_header_after(void *state)
{
	struct tcpm_header_fixture *fixture = state;

	tcpc_config[TCPM_TEST_PORT].drv = fixture->saved_driver_ptr;
	tcpc_config[TCPM_TEST_PORT].flags = fixture->saved_tcpc_flags;
}

ZTEST_SUITE(tcpm_header, drivers_predicate_pre_main, tcpm_header_setup,
	    tcpm_header_before, tcpm_header_after, NULL);
