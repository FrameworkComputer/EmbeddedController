/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "usbc_ocp.h"
#include "test/drivers/test_state.h"
#include "timer.h"

/* Tests for USBC OCP (Overcurrent Protection) Common Code */

#define TEST_PORT 0

/* Returns non-zero if state never reached */
static int wait_for_port_latched_off_state(bool state)
{
	WAIT_FOR(state == usbc_ocp_is_port_latched_off(TEST_PORT),
		 5000000, /* 5 Second */
		 k_sleep(K_MSEC(1)));

	return !(state == usbc_ocp_is_port_latched_off(TEST_PORT));
}

static void usbc_ocpc_suite_before_after(void *data)
{
	ARG_UNUSED(data);

	usbc_ocp_clear_event_counter(TEST_PORT);
	zassert_ok(wait_for_port_latched_off_state(false));
}

ZTEST(usbc_ocp, test_events_add_then_clear)
{
	for (int i = 0; i < OCP_MAX_CNT - 1; i++) {
		zassert_ok(usbc_ocp_add_event(TEST_PORT),
			   "Could not add ocp event %d", i);

		zassert_ok(wait_for_port_latched_off_state(false),
			   "Max OC events too soon");
	}

	zassert_ok(usbc_ocp_add_event(TEST_PORT));
	zassert_ok(wait_for_port_latched_off_state(true),
		   "Max OC events too soon");

	zassert_ok(usbc_ocp_clear_event_counter(TEST_PORT));
	zassert_ok(wait_for_port_latched_off_state(false),
		   "Max OC events too soon");
}

ZTEST(usbc_ocp, test_bad_port_arguments)
{
	zassert_ok(usbc_ocp_is_port_latched_off(-1));

	zassert_equal(EC_ERROR_INVAL, usbc_ocp_clear_event_counter(-1));
	zassert_equal(EC_ERROR_INVAL, usbc_ocp_add_event(-1));
}

ZTEST_SUITE(usbc_ocp, drivers_predicate_post_main, NULL,
	    usbc_ocpc_suite_before_after, usbc_ocpc_suite_before_after, NULL);
