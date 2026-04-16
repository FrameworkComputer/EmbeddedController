/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This file tests the sourcing policies on type-C ports.  See the diagram
 * under "ChromeOS as Source - Policy for Type-C" in the usb_power.md.
 */

#include "chipset.h"
#include "emul/emul_pdc.h"
#include "src_policy_common.h"
#include "test/util.h"
#include "timer.h"
#include "usbc/pdc_dpm.h"
#include "usbc/pdc_power_mgmt.h"
#include "usbc/utils.h"

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(pdc_src_policy);

FAKE_VALUE_FUNC(int, chipset_in_state, int);

BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == 2,
	     "PDC source policy test suite must supply exactly 2 PDC ports");

static enum chipset_state_mask fake_chipset_state = CHIPSET_STATE_ON;

static int custom_fake_chipset_in_state(int mask)
{
	return !!(fake_chipset_state & mask);
}

static void *src_policy_setup(void)
{
	static struct src_policy_fixture fixture;

	fixture.emul_pdc[0] = EMUL_DT_GET(PDC_NODE_PORT0);
	fixture.emul_pdc[1] = EMUL_DT_GET(PDC_NODE_PORT1);

	return &fixture;
};

static void src_policy_before(void *f)
{
	struct src_policy_fixture *fixture = f;

	RESET_FAKE(chipset_in_state);

	chipset_in_state_fake.custom_fake = custom_fake_chipset_in_state;

	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		/* Start with both ports disconnected. */
		zassert_ok(emul_pdc_disconnect(fixture->emul_pdc[i]));

		zassert_true(TEST_WAIT_FOR(!pdc_power_mgmt_is_connected(i),
					   PDC_TEST_TIMEOUT));
	}
}

ZTEST_SUITE(src_policy, NULL, src_policy_setup, src_policy_before, NULL, NULL);

ZTEST_USER_F(src_policy, test_src_policy_no_3a)
{
	union connector_status_t connector_status_port0 = { 0 };
	uint32_t partner_snk_pdo = PDO_FIXED(5000, 1500, 0);

	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT0],
			       &connector_status_port0);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &connector_status_port0));

	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	/* The emulator doesn't negotiate a real contract with the partner
	 * as this is under the control of the PDC firmware.
	 * Check the configured LPM source PDO to verify our policy manager
	 * offered a higher contract.
	 */
	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT0, 5000, 1500,
					 PDO_PEAK_OCP),
		   "1st PD sink port pdo incorrect");

	/* Verify the correct voltages are reported to the host. */
	struct ec_response_usb_pd_power_info response;
	response = host_cmd_power_info(TEST_USBC_PORT0);
	zassert_equal(
		response.role, USB_PD_PORT_POWER_SOURCE,
		"EC_CMD_USB_PD_POWER_INFO - Port %d Expected power role %d, "
		"but EC reports role %d",
		TEST_USBC_PORT0, USB_PD_PORT_POWER_DISCONNECTED, response.role);
	zassert_equal(
		response.meas.current_max, 1500,
		"EC_CMD_USB_PD_POWER_INFO - Port %d: expected current %d mA, "
		"actual current %d mA",
		TEST_USBC_PORT0, 1500, response.meas.current_max);
}

/* Verify PDC reports 7.5W Max PDP on boards without 3A ports. */
ZTEST_USER_F(src_policy, test_src_max_pdp)
{
	enum max_pdp_t max_pdp;

	zassert_ok(emul_pdc_get_max_pdp(fixture->emul_pdc[TEST_USBC_PORT0],
					&max_pdp));
	zassert_equal(MAX_PDP_7_5W, max_pdp,
		      "Expected max PDP to be 7.5W, got %d", max_pdp);
}
