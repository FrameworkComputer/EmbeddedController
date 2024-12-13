/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This file tests the sourcing policies on type-C ports.  See the diagram
 * under "ChromeOS as Source - Policy for Type-C" in the usb_power.md.
 */

#include "chipset.h"
#include "emul/emul_pdc.h"
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

BUILD_ASSERT(CONFIG_PLATFORM_EC_USB_PD_3A_PORTS == 1,
	     "PDC source policy test suite only supports one 3A port");

#define PDC_TEST_TIMEOUT 2000

/* TODO: b/343760437 - Once the emulator can detect the PDC threads are idle,
 * remove the sleep delay to let the policy code run.
 */
#define PDC_POLICY_DELAY_MS 500
#define PDC_NODE_PORT0 DT_NODELABEL(pdc_emul1)
#define PDC_NODE_PORT1 DT_NODELABEL(pdc_emul2)

#define TEST_USBC_PORT0 USBC_PORT_FROM_PDC_DRIVER_NODE(PDC_NODE_PORT0)
#define TEST_USBC_PORT1 USBC_PORT_FROM_PDC_DRIVER_NODE(PDC_NODE_PORT1)

bool pdc_power_mgmt_is_pd_attached(int port);

struct src_policy_fixture {
	const struct emul *emul_pdc[CONFIG_USB_PD_PORT_MAX_COUNT];
};

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
	uint32_t lpm_src_pdo = PDO_FIXED(5000, 1500, 0);

	RESET_FAKE(chipset_in_state);

	chipset_in_state_fake.custom_fake = custom_fake_chipset_in_state;

	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		/* Start with both ports disconnected. */
		zassert_ok(emul_pdc_disconnect(fixture->emul_pdc[i]));

		zassert_true(TEST_WAIT_FOR(!pdc_power_mgmt_is_connected(i),
					   PDC_TEST_TIMEOUT));

		/* Our USB souring policy indicates that PDCs must be configured
		 * to source only 1.5A by default.  Set the LPM source PDOs as
		 * if the PDC just had a hard reset.
		 */
		zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[i], SOURCE_PDO,
					     PDO_OFFSET_0, 1, LPM_PDO,
					     &lpm_src_pdo));
	}
}

ZTEST_SUITE(src_policy, NULL, src_policy_setup, src_policy_before, NULL, NULL);

static inline struct ec_response_usb_pd_power_info host_cmd_power_info(int port)
{
	struct ec_params_usb_pd_power_info params = { .port = port };
	struct ec_response_usb_pd_power_info response;

	zassert_ok(ec_cmd_usb_pd_power_info(NULL, &params, &response),
		   "Failed to get power info for port %d", port);
	return response;
}

/* Read the LPM's source PDO and verify the voltage and current. */
int verify_lpm_source_pdo(struct src_policy_fixture *fixture, uint32_t port,
			  int mv, int ma)
{
	uint32_t lpm_src_pdo;

	emul_pdc_get_pdos(fixture->emul_pdc[port], SOURCE_PDO, PDO_OFFSET_0, 1,
			  LPM_PDO, &lpm_src_pdo);

	if (PDO_FIXED_VOLTAGE(lpm_src_pdo) != mv) {
		/* LCOV_EXCL_START - error path only run when test fails */
		LOG_ERR("Expected fixed voltage %d mV, actual %d mV", mv,
			PDO_FIXED_VOLTAGE(lpm_src_pdo));
		return -ERANGE;
		/* LCOV_EXCL_STOP */
	}

	if (PDO_FIXED_CURRENT(lpm_src_pdo) != ma) {
		/* LCOV_EXCL_START - error path only run when test fails */
		LOG_ERR("Expected fixed current %d mA, actual %d mA", ma,
			PDO_FIXED_CURRENT(lpm_src_pdo));
		return -ERANGE;
		/* LCOV_EXCL_STOP */
	}
	return 0;
}

/* Verify first port connected is offered 3A contract. */
ZTEST_USER_F(src_policy, test_src_policy_one_3a)
{
	union connector_status_t connector_status_port0 = { 0 };
	union connector_status_t connector_status_port1 = { 0 };
	uint32_t partner_snk_pdo = PDO_FIXED(5000, 3000, 0);

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
	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT0, 5000, 3000),
		   "1st PD sink port not offered 15W");

	/* Connect a second 3A capable sink.  We should only offer a 1.5A
	 * contract.
	 */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT1],
			       &connector_status_port1);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT1],
					    &connector_status_port1));

	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT1, -1));

	/* Port 0 should still offer 5V 3A. */
	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT0, 5000, 3000),
		   "1st PD sink port downgraded from 15W unexpectedly");

	/* Port 1 should only offer 5V 1.5A. */
	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT1, 5000, 1500),
		   "2nd PD sink not limited to 7.5W");

	/* Verify the correct voltages are reported to the host. */
	struct ec_response_usb_pd_power_info response;
	response = host_cmd_power_info(TEST_USBC_PORT0);
	zassert_equal(
		response.role, USB_PD_PORT_POWER_SOURCE,
		"EC_CMD_USB_PD_POWER_INFO - Port %d Expected power role %d, "
		"but EC reports role %d",
		TEST_USBC_PORT0, USB_PD_PORT_POWER_DISCONNECTED, response.role);
	zassert_equal(
		response.meas.current_max, 3000,
		"EC_CMD_USB_PD_POWER_INFO - Port %d: expected current %d mA, "
		"actual current %d mA",
		TEST_USBC_PORT0, 3000, response.meas.current_max);

	response = host_cmd_power_info(TEST_USBC_PORT1);
	zassert_equal(
		response.role, USB_PD_PORT_POWER_SOURCE,
		"EC_CMD_USB_PD_POWER_INFO - Port %d Expected power role %d, "
		"but EC reports role %d",
		TEST_USBC_PORT1, USB_PD_PORT_POWER_DISCONNECTED, response.role);
	zassert_equal(
		response.meas.current_max, 1500,
		"EC_CMD_USB_PD_POWER_INFO - Port %d: expected current %d mA, "
		"actual current %d mA",
		TEST_USBC_PORT1, 3000, response.meas.current_max);
}

/* Verify 3A contract switches port when first port disconnected. */
ZTEST_USER_F(src_policy, test_src_policy_disconnect_3a)
{
	union connector_status_t connector_status = { 0 };
	uint32_t partner_snk_pdo = PDO_FIXED(5000, 3000, 0);

	/* Connect port 0 */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT0],
			       &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &connector_status));

	/* Wait for connection to settle and source policies to run. */
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	/* Connect port 1 */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT1],
			       &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT1],

					    &connector_status));
	/* Wait for connection to settle and source policies to run. */
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT1, -1));

	/* Port 1 should only offer 5V 1.5A. */
	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT1, 5000, 1500),
		   "2nd port connected should only be offered 7.5W");

	/* Disconnect port 0 */
	zassert_ok(emul_pdc_disconnect(fixture->emul_pdc[TEST_USBC_PORT0]));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	/* Port 1 should now be offered 3A */
	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT1, 5000, 3000),
		   "Port 1 didn't get 15W after disconnecting port 0");

	/* Port 0 should also be setup to only offer 1.5A for next connection */
	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT0, 5000, 1500),
		   "Disconnected port should only offer 7.5W");
}

ZTEST_USER_F(src_policy, test_src_policy_pr_swap)
{
	union connector_status_t connector_status = { 0 };
	union conn_status_change_bits_t change_bits = { 0 };
	uint32_t partner_snk_pdo = PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);

	/* Connect port 0 */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT0],
			       &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &connector_status));

	/* Wait for connection to settle and source policies to run. */
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT0, 5000, 3000),
		   "1st PD sink connected not offered 15W");

	/* Following a PR swap, the LPM PDO should be configured for
	 * only 1.5A.
	 */
	change_bits.raw_value = connector_status.raw_conn_status_change_bits;
	change_bits.pwr_direction = 1;
	connector_status.power_direction = 0;
	connector_status.raw_conn_status_change_bits = change_bits.raw_value;
	emul_pdc_set_connector_status(fixture->emul_pdc[TEST_USBC_PORT0],
				      &connector_status);
	emul_pdc_pulse_irq(fixture->emul_pdc[TEST_USBC_PORT0]);

	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT0, 5000, 1500),
		   "After PR swap, LPM should only offer 7.5W");
}

ZTEST_USER_F(src_policy, test_src_policy_non_pd)
{
	union connector_status_t connector_status = { 0 };
	uint32_t partner_snk_pdo = PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);
	enum usb_typec_current_t typec_current;

	/* Connect port 0 */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT0],
			       &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &connector_status));

	/* Wait for connection to settle and source policies to run. */
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT0, 5000, 3000),
		   "1st PD sink connected not offered 15W");

	/* Connect a non-PD sink.  The Rp should be set for 1.5A. */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT1],
			       &connector_status);
	connector_status.power_operation_mode = USB_DEFAULT_OPERATION;
	emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT1],
				 &connector_status);

	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT1, -1));

	zassert_ok(emul_pdc_get_requested_power_level(
		fixture->emul_pdc[TEST_USBC_PORT1], &typec_current));
	zassert_equal(typec_current, TC_CURRENT_1_5A);

	/* Disconnect port 0 */
	zassert_ok(emul_pdc_disconnect(fixture->emul_pdc[TEST_USBC_PORT0]));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	/* Non-PD should now be offered 3A current. */
	zassert_ok(emul_pdc_get_requested_power_level(
		fixture->emul_pdc[TEST_USBC_PORT1], &typec_current));
	zassert_equal(typec_current, TC_CURRENT_3_0A);

	/* Connecting a PD sink causes a downgrade of the non-PD sink.
	 */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT0],
			       &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &connector_status));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	k_sleep(K_USEC(PD_T_SINK_ADJ));

	/* Non-PD should now be downgraded to 1.5A current. */
	zassert_ok(emul_pdc_get_requested_power_level(
		fixture->emul_pdc[TEST_USBC_PORT1], &typec_current));
	zassert_equal(typec_current, TC_CURRENT_1_5A);
}

/* Verify operation with an FRS partner that requires 1.5A. */
ZTEST_USER_F(src_policy, test_src_policy_frs_1a5)
{
	union connector_status_t snk_partner_connector_status = { 0 };
	union connector_status_t frs_partner_connector_status = { 0 };
	uint32_t snk_partner_snk_pdo =
		PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);
	uint32_t frs_partner_src_pdo =
		PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);
	uint32_t frs_partner_snk_pdo = PDO_FIXED(
		5000, 3000, PDO_FIXED_DUAL_ROLE | PDO_FIXED_FRS_CURR_1A5_AT_5V);
	union connector_capability_t frs_ccaps = {
		.op_mode_drp = 1,
		.partner_pd_revision = PD_REV30,
	};
	bool frs_enabled;

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_FRS)) {
		ztest_test_skip();
	}

	/* Connect a PD sink at 3.0A */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT0],
			       &snk_partner_connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &snk_partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &snk_partner_connector_status));

	/* Wait for connection to settle and source policies to run. */
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT0, 5000, 3000),
		   "1st PD sink attached not offered 15W");

	/* Connect an FRS source that needs 1.5A. */
	zassert_ok(emul_pdc_set_connector_capability(
		fixture->emul_pdc[TEST_USBC_PORT1], &frs_ccaps));
	emul_pdc_configure_snk(fixture->emul_pdc[TEST_USBC_PORT1],
			       &frs_partner_connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SOURCE_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_src_pdo));
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT1],
					    &frs_partner_connector_status));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT1, -1));

	/* FRS should be enabled, even while providing 3A on another
	 * port. */
	zassert_ok(emul_pdc_get_frs(fixture->emul_pdc[TEST_USBC_PORT1],
				    &frs_enabled));
	zassert_true(frs_enabled);

	/* The source PDO should also be configured for 1.5A prior to
	 * the swap.
	 */
	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT1, 5000, 1500),
		   "7.5W FRS partner not limited to 7.5W");
}

/* Verify operation with an FRS partner that requires 3A. */
ZTEST_USER_F(src_policy, test_src_policy_frs_3a)
{
	union connector_status_t snk_partner_connector_status = { 0 };
	union connector_status_t frs_partner_connector_status = { 0 };
	uint32_t snk_partner_snk_pdo =
		PDO_FIXED(5000, 1500, PDO_FIXED_DUAL_ROLE);
	uint32_t frs_partner_src_pdo =
		PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);
	uint32_t frs_partner_snk_pdo = PDO_FIXED(
		5000, 3000, PDO_FIXED_DUAL_ROLE | PDO_FIXED_FRS_CURR_3A0_AT_5V);
	union connector_capability_t frs_ccaps = {
		.op_mode_drp = 1,
		.partner_pd_revision = PD_REV30,
	};
	bool frs_enabled;

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_FRS)) {
		ztest_test_skip();
	}

	/* When FRS partners connect as a source, and the FRS partner
	 * indicates it needs 3.0A, the EC should enable FRS only if no
	 * other PD sinks are connected and need 3.0A.
	 */

	/* Connect a PD sink at 1.5A */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT0],
			       &snk_partner_connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &snk_partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &snk_partner_connector_status));

	/* Wait for connection to settle and source policies to run. */
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT0, 5000, 1500),
		   "7.5W PD sink not limited to 7.5W");

	/* Connect an FRS source that supports 3.0A. */
	zassert_ok(emul_pdc_set_connector_capability(
		fixture->emul_pdc[TEST_USBC_PORT1], &frs_ccaps));
	emul_pdc_configure_snk(fixture->emul_pdc[TEST_USBC_PORT1],
			       &frs_partner_connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SOURCE_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_src_pdo));
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT1],
					    &frs_partner_connector_status));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT1, -1));

	/* FRS should be enabled. */
	zassert_ok(emul_pdc_get_frs(fixture->emul_pdc[TEST_USBC_PORT1],
				    &frs_enabled));
	zassert_true(frs_enabled);

	/* The source PDO should also be configured for 3.0A prior to
	 * the swap.
	 */
	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT1, 5000, 3000),
		   "15W FRS port not offered 15W");
}

/* Verify inserting a PD sink downgrades an FRS partner. */
ZTEST_USER_F(src_policy, test_src_policy_fsr_downgrade_for_pd)
{
	union connector_status_t snk_partner_connector_status = { 0 };
	union connector_status_t frs_partner_connector_status = { 0 };
	uint32_t snk_partner_snk_pdo =
		PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);
	uint32_t frs_partner_src_pdo =
		PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);
	uint32_t frs_partner_snk_pdo = PDO_FIXED(
		5000, 3000, PDO_FIXED_DUAL_ROLE | PDO_FIXED_FRS_CURR_3A0_AT_5V);
	union connector_capability_t frs_ccaps = {
		.op_mode_drp = 1,
		.partner_pd_revision = PD_REV30,
	};
	bool frs_enabled;

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_FRS)) {
		ztest_test_skip();
	}

	/* Connect an FRS source that supports 3.0A on port 1. */
	zassert_ok(emul_pdc_set_connector_capability(
		fixture->emul_pdc[TEST_USBC_PORT1], &frs_ccaps));
	emul_pdc_configure_snk(fixture->emul_pdc[TEST_USBC_PORT1],
			       &frs_partner_connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SOURCE_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_src_pdo));
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT1],
					    &frs_partner_connector_status));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT1, -1));

	/* FRS should be enabled. */
	zassert_ok(emul_pdc_get_frs(fixture->emul_pdc[TEST_USBC_PORT1],
				    &frs_enabled));
	zassert_true(frs_enabled);

	/* The source PDO should also be configured for 3.0A prior to
	 * the swap.
	 */
	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT1, 5000, 3000),
		   "15W FRS port not offered 15W");

	/* Connecting a PD sink that needs 3.0A on port 0.
	 * This should downgrade the FRS source, disabling FRS and
	 * changing the current limit.
	 */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT0],
			       &snk_partner_connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &snk_partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &snk_partner_connector_status));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	/* Allow for policies to run on port 1. */
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT1, -1));

	/* TODO - validate that the FRS port is degraded before the sink
	 * port is offered 3.0A.
	 */

	/* FRS should be disabled. */
	zassert_ok(emul_pdc_get_frs(fixture->emul_pdc[TEST_USBC_PORT1],
				    &frs_enabled));
	zassert_false(frs_enabled);

	/* LPM source PDO offered should only be 1.5A to the FRS port. */
	zassert_ok(
		verify_lpm_source_pdo(fixture, TEST_USBC_PORT1, 5000, 1500),
		"15W FRS port not downgraded to 7.5W after connecting PD sink");

	/* PD sink should be offered 3.0A. */
	zassert_ok(
		verify_lpm_source_pdo(fixture, TEST_USBC_PORT0, 5000, 3000),
		"15W PD sink not offered 15W when connecting after FRS port");

	/* Disconnecting the PD sink on port 0 should re-enable FRS. */
	zassert_ok(emul_pdc_disconnect(fixture->emul_pdc[TEST_USBC_PORT0]));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	/* Allow for policies to run on port 1. */
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT1, -1));

	/* FRS should be enabled. */
	zassert_ok(emul_pdc_get_frs(fixture->emul_pdc[TEST_USBC_PORT1],
				    &frs_enabled));
	zassert_true(frs_enabled);

	/* The source PDO should also be configured for 3.0A prior to
	 * the swap.
	 */
	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT1, 5000, 3000),
		   "15W FRS port not upgraded to 15W after removing PD sink");
}

/* Verify inserting and FRS partner downgrades non PD partners. */
ZTEST_USER_F(src_policy, test_src_policy_non_pd_downgrade_for_frs)
{
	union connector_status_t snk_partner_connector_status = { 0 };
	union connector_status_t frs_partner_connector_status = { 0 };
	uint32_t frs_partner_src_pdo =
		PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);
	uint32_t frs_partner_snk_pdo = PDO_FIXED(
		5000, 3000, PDO_FIXED_DUAL_ROLE | PDO_FIXED_FRS_CURR_3A0_AT_5V);
	union connector_capability_t frs_ccaps = {
		.op_mode_drp = 1,
		.partner_pd_revision = PD_REV30,
	};
	bool frs_enabled;
	enum usb_typec_current_t typec_current;

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_FRS)) {
		ztest_test_skip();
	}

	/* Connect a non-PD sink at 3.0A */
	snk_partner_connector_status.power_direction = 1;
	snk_partner_connector_status.power_operation_mode = USB_TC_CURRENT_3A;
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &snk_partner_connector_status));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	zassert_ok(emul_pdc_get_requested_power_level(
		fixture->emul_pdc[TEST_USBC_PORT0], &typec_current));
	zassert_equal(typec_current, TC_CURRENT_3_0A);

	/* Connect an FRS source, non-PD port should downgrade */
	zassert_ok(emul_pdc_set_connector_capability(
		fixture->emul_pdc[TEST_USBC_PORT1], &frs_ccaps));
	emul_pdc_configure_snk(fixture->emul_pdc[TEST_USBC_PORT1],
			       &frs_partner_connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SOURCE_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_src_pdo));
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT1],
					    &frs_partner_connector_status));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT1, -1));

	/* Non-PD sink should be downgraded. */
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));
	zassert_ok(emul_pdc_get_requested_power_level(
		fixture->emul_pdc[TEST_USBC_PORT0], &typec_current));
	zassert_equal(typec_current, TC_CURRENT_1_5A);

	/* FRS should be enabled. */
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT1, -1));
	zassert_ok(emul_pdc_get_frs(fixture->emul_pdc[TEST_USBC_PORT1],
				    &frs_enabled));
	zassert_true(frs_enabled);

	zassert_ok(
		verify_lpm_source_pdo(fixture, TEST_USBC_PORT1, 5000, 3000),
		"15W FRS port not offerred 15W after connecting non-PD sink first");
}

/* Verify paths where the partner's sink PDO fails requirements
 * to be offered 3A.
 */
ZTEST_USER_F(src_policy, test_src_policy_sink_pdo_errors)
{
	union connector_status_t connector_status = { 0 };
	uint32_t partner_snk_pdo;

	/* We only offer 3A to partners with a fixed PDO. */
	partner_snk_pdo = PDO_VAR(5000, 15000, 3000);
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT0],
			       &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &connector_status));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	/* Source caps should only be 5V/1.5A. */
	zassert_ok(verify_lpm_source_pdo(fixture, TEST_USBC_PORT0, 5000, 1500),
		   "15W PD sink with missing Fixed PDO not limited to 7.5W");

	zassert_ok(emul_pdc_disconnect(fixture->emul_pdc[TEST_USBC_PORT0]));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));
}

/* Verify error paths related to handling of FRS partner sink PDOs. */
ZTEST_USER_F(src_policy, test_src_policy_frs_sink_pdo_errors)
{
	union connector_status_t connector_status = { 0 };
	uint32_t frs_partner_src_pdo =
		PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);
	uint32_t frs_partner_snk_pdo;
	union connector_capability_t frs_ccaps = {
		.op_mode_drp = 1,
	};
	bool frs_enabled;

	/* Following code paths require FRS support. */
	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_FRS)) {
		ztest_test_skip();
	}

	/* FRS only offered for PD rev 3.0 and above */
	frs_partner_snk_pdo = PDO_FIXED(
		5000, 3000, PDO_FIXED_DUAL_ROLE | PDO_FIXED_FRS_CURR_1A5_AT_5V);
	frs_ccaps.partner_pd_revision = PD_REV20;
	/* Connect an FRS source that supports 3.0A on port 1. */
	zassert_ok(emul_pdc_set_connector_capability(
		fixture->emul_pdc[TEST_USBC_PORT0], &frs_ccaps));
	emul_pdc_configure_snk(fixture->emul_pdc[TEST_USBC_PORT0],
			       &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SOURCE_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_src_pdo));
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &connector_status));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	/* FRS should be disabled. */
	zassert_ok(emul_pdc_get_frs(fixture->emul_pdc[TEST_USBC_PORT0],
				    &frs_enabled));
	zassert_false(frs_enabled);

	zassert_ok(emul_pdc_disconnect(fixture->emul_pdc[TEST_USBC_PORT0]));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	/* Partner must also advertise FRS before we enable FRS. */
	frs_partner_snk_pdo = PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);
	frs_ccaps.partner_pd_revision = 3;
	zassert_ok(emul_pdc_set_connector_capability(
		fixture->emul_pdc[TEST_USBC_PORT0], &frs_ccaps));
	emul_pdc_configure_snk(fixture->emul_pdc[TEST_USBC_PORT0],
			       &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SOURCE_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_src_pdo));
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &connector_status));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	/* FRS should be disabled. */
	zassert_ok(emul_pdc_get_frs(fixture->emul_pdc[TEST_USBC_PORT0],
				    &frs_enabled));
	zassert_false(frs_enabled);
	zassert_ok(emul_pdc_disconnect(fixture->emul_pdc[TEST_USBC_PORT0]));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	/* Verify FRS enabled if the partner only wants default power */
	frs_partner_snk_pdo = PDO_FIXED(
		5000, 3000,
		PDO_FIXED_DUAL_ROLE | PDO_FIXED_FRS_CURR_DFLT_USB_POWER);
	frs_ccaps.partner_pd_revision = 3;
	zassert_ok(emul_pdc_set_connector_capability(
		fixture->emul_pdc[TEST_USBC_PORT0], &frs_ccaps));
	emul_pdc_configure_snk(fixture->emul_pdc[TEST_USBC_PORT0],
			       &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SOURCE_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_src_pdo));
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &frs_partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &connector_status));
	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_USBC_PORT0, -1));

	zassert_ok(emul_pdc_get_frs(fixture->emul_pdc[TEST_USBC_PORT0],
				    &frs_enabled));
	zassert_true(frs_enabled);
}
