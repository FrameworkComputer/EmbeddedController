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

static void *tcpm_header_setup(void)
{
	static struct tcpm_header_fixture fixture;

	return &fixture;
}

static void tcpm_header_before(void *state)
{
	struct tcpm_header_fixture *fixture = state;

	RESET_FAKE(set_vconn);

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
