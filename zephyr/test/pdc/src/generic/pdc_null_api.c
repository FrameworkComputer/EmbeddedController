/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_pd.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/types.h>
#include <zephyr/ztest.h>

#include <drivers/pdc.h>

/* If true, treat a triggered assertion as a pass */
static bool expect_assert = false;

/* Set up a fake PDC API implementation with all-NULL function pointers */
static struct pdc_driver_api_t fake_pdc_api = { 0 };
static const struct device fake_pdc = {
	.api = &fake_pdc_api,
};

/* LCOV_EXCL_START - These tests expect an assertion and thus the test function
 * and `assert_post_action` do not exit naturally (we directly pass or fail the
 * test). This leaves the final lines of these functions uncoverable.
 */

/* Called by Zephyr when an __ASSERT() macro trips. */
void assert_post_action(const char *file, unsigned int line)
{
	if (!expect_assert) {
		/* Asserted somewhere we should not have */
		ztest_test_fail();
	} else {
		/* We asserted in a location we wanted to. MUST bail from the
		 * test right now because the `pdc.h` functions will try to
		 * invoke the NULL API function pointers otherwise (and crash)
		 * if execution proceeds.
		 */

		expect_assert = false;
		ztest_test_pass();
	}
}

#define EXPECT_ASSERT(test)                               \
	do {                                              \
		expect_assert = true;                     \
		(test);                                   \
		zassert_true(0, "Assert did not happen"); \
	} while (0)

ZTEST(pdc_api_null_check, test_pdc_is_init_done)
{
	EXPECT_ASSERT(pdc_is_init_done(&fake_pdc));
}

ZTEST(pdc_api_null_check, test_pdc_read_power_level)
{
	EXPECT_ASSERT(pdc_read_power_level(&fake_pdc));
}

ZTEST(pdc_api_null_check, test_pdc_get_ucsi_version)
{
	EXPECT_ASSERT(pdc_get_ucsi_version(&fake_pdc, NULL));
}

ZTEST(pdc_api_null_check, test_pdc_reset)
{
	EXPECT_ASSERT(pdc_reset(&fake_pdc));
}

ZTEST(pdc_api_null_check, test_pdc_connector_reset)
{
	union connector_reset_t cr = { 0 };

	EXPECT_ASSERT(pdc_connector_reset(&fake_pdc, cr));
}

ZTEST(pdc_api_null_check, test_pdc_set_sink_path)
{
	EXPECT_ASSERT(pdc_set_sink_path(&fake_pdc, false));
}

ZTEST(pdc_api_null_check, test_pdc_get_capability)
{
	EXPECT_ASSERT(pdc_get_capability(&fake_pdc, NULL));
}

ZTEST(pdc_api_null_check, test_pdc_get_connector_status)
{
	EXPECT_ASSERT(pdc_get_connector_status(&fake_pdc, NULL));
}

ZTEST(pdc_api_null_check, test_pdc_get_error_status)
{
	EXPECT_ASSERT(pdc_get_error_status(&fake_pdc, NULL));
}

ZTEST(pdc_api_null_check, test_pdc_get_connector_capability)
{
	EXPECT_ASSERT(pdc_get_connector_capability(&fake_pdc, NULL));
}

ZTEST(pdc_api_null_check, test_pdc_set_uor)
{
	union uor_t uor = { 0 };

	EXPECT_ASSERT(pdc_set_uor(&fake_pdc, uor));
}

ZTEST(pdc_api_null_check, test_pdc_set_pdr)
{
	union pdr_t pdr = { 0 };

	EXPECT_ASSERT(pdc_set_pdr(&fake_pdc, pdr));
}

ZTEST(pdc_api_null_check, test_pdc_set_cc_callback)
{
	EXPECT_ASSERT(pdc_set_cc_callback(&fake_pdc, NULL));
}

ZTEST(pdc_api_null_check, test_pdc_get_vbus_voltage)
{
	EXPECT_ASSERT(pdc_get_vbus_voltage(&fake_pdc, NULL));
}

ZTEST(pdc_api_null_check, test_pdc_get_info)
{
	EXPECT_ASSERT(pdc_get_info(&fake_pdc, NULL, false));
}

ZTEST(pdc_api_null_check, test_pdc_get_bus_info)
{
	EXPECT_ASSERT(pdc_get_bus_info(&fake_pdc, NULL));
}

ZTEST(pdc_api_null_check, test_pdc_get_rdo)
{
	EXPECT_ASSERT(pdc_get_rdo(&fake_pdc, NULL));
}

ZTEST(pdc_api_null_check, test_pdc_set_rdo)
{
	EXPECT_ASSERT(pdc_set_rdo(&fake_pdc, 0));
}

ZTEST(pdc_api_null_check, test_pdc_get_cable_property)
{
	EXPECT_ASSERT(pdc_get_cable_property(&fake_pdc, NULL));
}

ZTEST(pdc_api_null_check, test_pdc_get_vdo)
{
	union get_vdo_t vdo_req = { 0 };

	EXPECT_ASSERT(pdc_get_vdo(&fake_pdc, vdo_req, NULL, NULL));
}

ZTEST(pdc_api_null_check, test_pdc_set_comms_state)
{
	EXPECT_ASSERT(pdc_set_comms_state(&fake_pdc, false));
}

ZTEST(pdc_api_null_check, test_pdc_set_ccom)
{
	int rv = pdc_set_ccom(&fake_pdc, 0);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_set_drp_mode)
{
	int rv = pdc_set_drp_mode(&fake_pdc, 0);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_get_pdos)
{
	int rv = pdc_get_pdos(&fake_pdc, 0, 0, 0, 0, NULL);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}
/* LCOV_EXCL_STOP */

ZTEST(pdc_api_null_check, test_pdc_get_current_pdo)
{
	int rv = pdc_get_current_pdo(&fake_pdc, NULL);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_set_power_level)
{
	int rv = pdc_set_power_level(&fake_pdc, 0);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_reconnect)
{
	int rv = pdc_reconnect(&fake_pdc);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_get_current_flash_bank)
{
	int rv = pdc_get_current_flash_bank(&fake_pdc, NULL);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_update_retimer_fw)
{
	int rv = pdc_update_retimer_fw(&fake_pdc, false);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_get_pch_data_status)
{
	int rv = pdc_get_pch_data_status(&fake_pdc, 0, NULL);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_get_identity_discovery)
{
	int rv = pdc_get_identity_discovery(&fake_pdc, NULL);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_set_pdos)
{
	int rv = pdc_set_pdos(&fake_pdc, 0, NULL, 0);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_is_vconn_sourcing)
{
	int rv = pdc_is_vconn_sourcing(&fake_pdc, NULL);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_ack_cc_ci)
{
	union conn_status_change_bits_t ci = { 0 };

	int rv = pdc_ack_cc_ci(&fake_pdc, ci, false, 0);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_execute_ucsi_cmd)
{
	int rv = pdc_execute_ucsi_cmd(&fake_pdc, 0, 0, NULL, NULL, NULL);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_add_ci_callback)
{
	int rv = pdc_add_ci_callback(&fake_pdc, NULL);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_get_lpm_ppm_info)
{
	int rv = pdc_get_lpm_ppm_info(&fake_pdc, NULL);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_pdc_set_frs)
{
	int rv = pdc_set_frs(&fake_pdc, false);

	zassert_equal(-ENOSYS, rv, "Got %d, expected -ENOSYS (%d)", rv,
		      -ENOSYS);
}

ZTEST(pdc_api_null_check, test_completeness)
{
	/* Count the number of PDC API methods supported */
	size_t num_api_methods =
		sizeof(struct pdc_driver_api_t) / sizeof(void *);

	/* Get the number of tests, not counting this one. */
	size_t num_tests = ZTEST_TEST_COUNT - 1;

	zassert_equal(1, ZTEST_SUITE_COUNT, "This suite should be run solo");

	zassert_equal(
		num_api_methods, num_tests,
		"Found %zu API methods in 'struct pdc_driver_api_t' but only "
		"%zu tests in 'pdc_null_api.c'. Please write a test to make "
		"sure this API method is NULL-protected",
		num_api_methods, num_tests);
}

static void before(void *f)
{
	ARG_UNUSED(f);

	expect_assert = false;

	printk("If this test abruptly stops, a PDC API function pointer is not "
	       "getting NULL-checked.\n");
}

ZTEST_SUITE(pdc_api_null_check, NULL, NULL, before, NULL, NULL);
