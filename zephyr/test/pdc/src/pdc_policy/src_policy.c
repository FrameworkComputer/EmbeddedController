/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "emul/emul_pdc.h"
#include "test/util.h"
#include "timer.h"
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

BUILD_ASSERT(CONFIG_PLATFORM_EC_CONFIG_USB_PD_3A_PORTS == 1,
	     "PDC source policy test suite only supports one 3A port");

#define PDC_TEST_TIMEOUT 2000

/* TODO: b/343760437 - Once the emulator can detect the PDC threads are idle,
 * remove the sleep delay to let the policy code run.
 */
#define PDC_POLICY_DELAY_MS 500
#define PDC_NODE_PORT0 DT_NODELABEL(pdc_emul1)
#define PDC_NODE_PORT1 DT_NODELABEL(pdc_emul2)

#define TEST_USBC_PORT0 USBC_PORT_FROM_DRIVER_NODE(PDC_NODE_PORT0, pdc)
#define TEST_USBC_PORT1 USBC_PORT_FROM_DRIVER_NODE(PDC_NODE_PORT1, pdc)

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

/* Verify first port connected is offered 3A contract. */
ZTEST_USER_F(src_policy, test_src_policy_one_3a)
{
	union connector_status_t connector_status_port0;
	union connector_status_t connector_status_port1;
	uint32_t partner_snk_pdo = PDO_FIXED(5000, 3000, 0);
	uint32_t lpm_src_pdo_actual_port0;
	uint32_t lpm_src_pdo_actual_port1;

	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT0],
			       &connector_status_port0);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &connector_status_port0));

	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(TEST_USBC_PORT0));

	/* The emulator doesn't negotiate a real contract with the partner
	 * as this is under the control of the PDC firmware.
	 * Check the configured LPM source PDO to verify our policy manager
	 * offered a higher contract.
	 */
	zassert_ok(emul_pdc_get_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SOURCE_PDO, PDO_OFFSET_0, 1, LPM_PDO,
				     &lpm_src_pdo_actual_port0));

	zassert_equal(PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port0), 5000,
		      "LPM SOURCE_PDO voltage %d, but expected %d",
		      PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port0), 5000);
	zassert_equal(PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port0), 3000,
		      "LPM SOURCE_PDO current %d, but expected %d",
		      PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port0), 3000);

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

	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(TEST_USBC_PORT1));

	zassert_ok(emul_pdc_get_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SOURCE_PDO, PDO_OFFSET_0, 1, LPM_PDO,
				     &lpm_src_pdo_actual_port0));
	zassert_ok(emul_pdc_get_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SOURCE_PDO, PDO_OFFSET_0, 1, LPM_PDO,
				     &lpm_src_pdo_actual_port1));

	/* Port 0 should still offer 5V 3A. */
	zassert_equal(PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port0), 5000,
		      "LPM SOURCE_PDO voltage %d, but expected %d",
		      PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port0), 5000);
	zassert_equal(PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port0), 3000,
		      "LPM SOURCE_PDO current %d, but expected %d",
		      PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port0), 5000);

	/* Port 1 should only offer 5V 1.5A. */
	zassert_equal(PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port1), 5000,
		      "LPM SOURCE_PDO voltage %d, but expected %d",
		      PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port1), 5000);
	zassert_equal(PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port1), 1500,
		      "LPM SOURCE_PDO current %d, but expected %d",
		      PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port1), 1500);
}

/* Verify 3A contract switches port when first port disconnected. */
ZTEST_USER_F(src_policy, test_src_policy_disconnect_3a)
{
	union connector_status_t connector_status;
	uint32_t partner_snk_pdo = PDO_FIXED(5000, 3000, 0);
	uint32_t lpm_src_pdo_actual_port0;
	uint32_t lpm_src_pdo_actual_port1;

	/* Connect port 0 */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT0],
			       &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &connector_status));

	/* Wait for connection to settle and source policies to run. */
	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(TEST_USBC_PORT0));

	/* Connect port 1 */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT1],
			       &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT1],

					    &connector_status));
	/* Wait for connection to settle and source policies to run. */
	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(TEST_USBC_PORT1));

	/* Port 1 should only offer 5V 1.5A. */
	zassert_ok(emul_pdc_get_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SOURCE_PDO, PDO_OFFSET_0, 1, LPM_PDO,
				     &lpm_src_pdo_actual_port1));
	zassert_equal(PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port1), 5000,
		      "LPM SOURCE_PDO voltage %d, but expected %d",
		      PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port1), 5000);
	zassert_equal(PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port1), 1500,
		      "LPM SOURCE_PDO current %d, but expected %d",
		      PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port1), 1500);

	/* Disconnect port 0 */
	zassert_ok(emul_pdc_disconnect(fixture->emul_pdc[TEST_USBC_PORT0]));
	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(TEST_USBC_PORT0));

	/* Port 1 should now be offered 3A */
	zassert_ok(emul_pdc_get_pdos(fixture->emul_pdc[TEST_USBC_PORT1],
				     SOURCE_PDO, PDO_OFFSET_0, 1, LPM_PDO,
				     &lpm_src_pdo_actual_port1));
	zassert_equal(PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port1), 5000,
		      "LPM SOURCE_PDO voltage %d, but expected %d",
		      PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port1), 5000);
	zassert_equal(PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port1), 3000,
		      "LPM SOURCE_PDO current %d, but expected %d",
		      PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port1), 3000);

	/* Port 0 should also be setup to only offer 1.5A for next connection */
	zassert_ok(emul_pdc_get_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SOURCE_PDO, PDO_OFFSET_0, 1, LPM_PDO,
				     &lpm_src_pdo_actual_port0));
	zassert_equal(PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port0), 5000,
		      "LPM SOURCE_PDO voltage %d, but expected %d",
		      PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port0), 5000);
	zassert_equal(PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port0), 1500,
		      "LPM SOURCE_PDO current %d, but expected %d",
		      PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port0), 1500);
}

ZTEST_USER_F(src_policy, test_src_policy_pr_swap)
{
	union connector_status_t connector_status;
	union conn_status_change_bits_t change_bits;
	uint32_t partner_snk_pdo = PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);
	uint32_t lpm_src_pdo_actual_port0;

	/* Connect port 0 */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT0],
			       &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &connector_status));

	/* Wait for connection to settle and source policies to run. */
	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(TEST_USBC_PORT0));

	zassert_ok(emul_pdc_get_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SOURCE_PDO, PDO_OFFSET_0, 1, LPM_PDO,
				     &lpm_src_pdo_actual_port0));

	zassert_equal(PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port0), 5000,
		      "LPM SOURCE_PDO voltage %d, but expected %d",
		      PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port0), 5000);
	zassert_equal(PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port0), 3000,
		      "LPM SOURCE_PDO current %d, but expected %d",
		      PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port0), 5000);

	/* Following a PR swap, the LPM PDO should be configured for only
	 * 1.5A.
	 */
	change_bits.raw_value = connector_status.raw_conn_status_change_bits;
	change_bits.pwr_direction = 1;
	connector_status.power_direction = 0;
	connector_status.raw_conn_status_change_bits = change_bits.raw_value;
	emul_pdc_set_connector_status(fixture->emul_pdc[TEST_USBC_PORT0],
				      &connector_status);
	emul_pdc_pulse_irq(fixture->emul_pdc[TEST_USBC_PORT0]);

	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(TEST_USBC_PORT0));

	zassert_ok(emul_pdc_get_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SOURCE_PDO, PDO_OFFSET_0, 1, LPM_PDO,
				     &lpm_src_pdo_actual_port0));

	zassert_equal(PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port0), 5000,
		      "LPM SOURCE_PDO voltage %d, but expected %d",
		      PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port0), 5000);
	zassert_equal(PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port0), 1500,
		      "LPM SOURCE_PDO current %d, but expected %d",
		      PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port0), 1500);
}

ZTEST_USER_F(src_policy, test_src_policy_non_pd)
{
	union connector_status_t connector_status;
	uint32_t partner_snk_pdo = PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);
	uint32_t lpm_src_pdo_actual_port0;
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
	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(TEST_USBC_PORT0));

	zassert_ok(emul_pdc_get_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SOURCE_PDO, PDO_OFFSET_0, 1, LPM_PDO,
				     &lpm_src_pdo_actual_port0));

	zassert_equal(PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port0), 5000,
		      "LPM SOURCE_PDO voltage %d, but expected %d",
		      PDO_FIXED_GET_VOLT(lpm_src_pdo_actual_port0), 5000);
	zassert_equal(PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port0), 3000,
		      "LPM SOURCE_PDO current %d, but expected %d",
		      PDO_FIXED_GET_CURR(lpm_src_pdo_actual_port0), 5000);

	/* Connect a non-PD sink.  The Rp should be set for 1.5A. */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT1],
			       &connector_status);
	connector_status.power_operation_mode = USB_DEFAULT_OPERATION;
	emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT1],
				 &connector_status);

	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(TEST_USBC_PORT1));

	zassert_ok(emul_pdc_get_requested_power_level(
		fixture->emul_pdc[TEST_USBC_PORT1], &typec_current));
	zassert_equal(typec_current, TC_CURRENT_1_5A);

	/* Disconnect port 0 */
	zassert_ok(emul_pdc_disconnect(fixture->emul_pdc[TEST_USBC_PORT0]));
	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(TEST_USBC_PORT0));

	/* Non-PD should now be offered 3A current. */
	zassert_ok(emul_pdc_get_requested_power_level(
		fixture->emul_pdc[TEST_USBC_PORT1], &typec_current));
	zassert_equal(typec_current, TC_CURRENT_3_0A);

	/* Connecting a PD sink causes a downgrade of the non-PD sink. */
	emul_pdc_configure_src(fixture->emul_pdc[TEST_USBC_PORT0],
			       &connector_status);
	zassert_ok(emul_pdc_set_pdos(fixture->emul_pdc[TEST_USBC_PORT0],
				     SINK_PDO, PDO_OFFSET_0, 1, PARTNER_PDO,
				     &partner_snk_pdo));
	zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc[TEST_USBC_PORT0],
					    &connector_status));
	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(TEST_USBC_PORT0));

	k_sleep(K_USEC(PD_T_SINK_ADJ));

	/* Non-PD should now be downgraded to 1.5A current. */
	zassert_ok(emul_pdc_get_requested_power_level(
		fixture->emul_pdc[TEST_USBC_PORT1], &typec_current));
	zassert_equal(typec_current, TC_CURRENT_1_5A);
}
