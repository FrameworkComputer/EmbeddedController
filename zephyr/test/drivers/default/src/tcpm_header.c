/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tcpm/tcpci.h"
#include "tcpm/tcpm.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define TCPM_TEST_PORT USBC_PORT_C0

FAKE_VALUE_FUNC(int, set_vconn, int, int);
FAKE_VALUE_FUNC(int, reset_bist_type_2, int);

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
	int res;

	res = tcpm_reset_bist_type_2(TCPM_TEST_PORT);

	zassert_equal(EC_SUCCESS, res);
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
