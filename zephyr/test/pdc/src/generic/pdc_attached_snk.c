/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This file tests various Attached.Snk scenarios.
 */

#include "emul/emul_pdc.h"
#include "usbc/pdc_power_mgmt.h"
#include "usbc/utils.h"

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(pdc_attached_snk);

#define PDC_NODE_PORT0 DT_NODELABEL(pdc_emul1)
#define TEST_USBC_PORT0 USBC_PORT_FROM_PDC_DRIVER_NODE(PDC_NODE_PORT0)

extern bool test_pdc_power_mgmt_is_snk_typec_attached_run(int port);

struct pdc_attached_snk_fixture {
	int port;
	const struct emul *emul_pdc;
};

static void *pdc_attached_snk_setup(void)
{
	static struct pdc_attached_snk_fixture fixture;

	fixture.emul_pdc = EMUL_DT_GET(PDC_NODE_PORT0);
	fixture.port = TEST_USBC_PORT0;

	return &fixture;
};

ZTEST_SUITE(pdc_attached_snk, NULL, pdc_attached_snk_setup, NULL, NULL, NULL);

/* Verify the DUT sinks the correct amount of current, when
 * the partner sends new source caps.
 */

ZTEST_USER_F(pdc_attached_snk, test_new_pd_sink_contract)
{
	union connector_status_t in = { 0 };
	union conn_status_change_bits_t in_conn_status_change_bits = { 0 };
	bool sink_path_en;

	const uint32_t pdos[] = {
		PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE),
		PDO_FIXED(15000, 3000, PDO_FIXED_DUAL_ROLE),
	};

	/* Connect a sourcing port partner */
	emul_pdc_configure_snk(fixture->emul_pdc, &in);
	emul_pdc_set_pdos(fixture->emul_pdc, SOURCE_PDO, PDO_OFFSET_0, 1,
			  PARTNER_PDO, pdos);
	emul_pdc_connect_partner(fixture->emul_pdc, &in);

	/* Ensure we are connected */
	pdc_power_mgmt_wait_for_sync(fixture->port, -1);

	/* Simulate the port partner changing its PDOs. The sink path is
	 * disabled during this step */
	emul_pdc_set_pdos(fixture->emul_pdc, SOURCE_PDO, PDO_OFFSET_0,
			  ARRAY_SIZE(pdos), PARTNER_PDO, pdos);
	in_conn_status_change_bits.supported_provider_caps = 1;
	in.raw_conn_status_change_bits = in_conn_status_change_bits.raw_value;
	emul_pdc_connect_partner(fixture->emul_pdc, &in);

	/* Pause to allow pdc_power_mgmt to process interrupt and re-settle */
	pdc_power_mgmt_wait_for_sync(fixture->port, -1);

	/* Check that the sink path is on again */
	zassert_ok(emul_pdc_get_sink_path(fixture->emul_pdc, &sink_path_en));
	zassert_true(sink_path_en);
}

/* Helper function to connect a partner that initially advertises the
 * default source caps, waits a configurable delay, and the offers
 * a single fixed 5V/0A PDO.
 *
 * @param done is set to true once the delay is long enough that
 * the PDC thread enabled the sink path after processing the default source
 * caps
 */
int connect_default_charger_then_0w(struct pdc_attached_snk_fixture *fixture,
				    int delay_ms, bool *done)
{
	union connector_status_t in = { 0 };
	union conn_status_change_bits_t in_conn_status_change_bits = { 0 };
	bool sink_path_en;
	const uint32_t pdo_0_amp[PDO_OFFSET_MAX] = {
		PDO_FIXED(5000, 0, PDO_FIXED_DUAL_ROLE),
	};
	int ret;
	k_timepoint_t timepoint;

	LOG_INF("attach default charger");
	/* Note - configure sink sets up a set of valid Source PDOs. */
	emul_pdc_configure_snk(fixture->emul_pdc, &in);
	ret = emul_pdc_connect_partner(fixture->emul_pdc, &in);
	if (ret != 0) {
		LOG_ERR("Failed to connect partner to emulator");
		return ret;
	}

	/* Allow the the PDC thread to run, but change the source caps
	 * from the partner before the PDC thread reaches the idle state.
	 */
	LOG_INF("wait %d ms", delay_ms);
	k_sleep(K_MSEC(delay_ms));

	/* Sink path should be off. */
	ret = emul_pdc_get_sink_path(fixture->emul_pdc, &sink_path_en);
	if (ret != 0) {
		LOG_ERR("Failed to read sink path status from emulator");
		return ret;
	}

	if (sink_path_en) {
		/* Once the delay is long enough for the PDC to enable the
		 * sink path after receiving the first source caps, signal
		 * to the caller that we can stop the test.
		 */
		*done = true;
		emul_pdc_disconnect(fixture->emul_pdc);
		pdc_power_mgmt_wait_for_sync(fixture->port, -1);
		return 0;
	} else {
		*done = false;
	}

	LOG_INF("attach 0W charger");
	ret = emul_pdc_set_pdos(fixture->emul_pdc, SOURCE_PDO, PDO_OFFSET_0,
				ARRAY_SIZE(pdo_0_amp), PARTNER_PDO, pdo_0_amp);
	if (ret != 0) {
		LOG_ERR("Failed to set partner source PDOs");
		return ret;
	}

	in_conn_status_change_bits.supported_provider_caps = 1;
	in.raw_conn_status_change_bits = in_conn_status_change_bits.raw_value;
	/* Update connector status and trigger an interrupt. */
	ret = emul_pdc_connect_partner(fixture->emul_pdc, &in);
	if (ret != 0) {
		LOG_ERR("Failed to connect partner to emulator");
		return ret;
	}

	timepoint = sys_timepoint_calc(
		K_MSEC(CONFIG_PDC_POWER_MGMT_STATE_MACHINE_SETTLED_TIMEOUT_MS));
	do {
		k_sleep(K_MSEC(25));
		ret = emul_pdc_get_sink_path(fixture->emul_pdc, &sink_path_en);
		if (ret != 0) {
			LOG_ERR("Failed to read sink path status from emulator");
			return ret;
		}

		zassert_false(
			sink_path_en,
			"Unexpected sink path enabled: PDO change delay %d ms",
			delay_ms);

		if (test_pdc_power_mgmt_is_snk_typec_attached_run(
			    fixture->port)) {
			break;
		}

	} while (!sys_timepoint_expired(timepoint));

	zassert_ok(emul_pdc_get_sink_path(fixture->emul_pdc, &sink_path_en));
	zassert_false(sink_path_en);

	emul_pdc_disconnect(fixture->emul_pdc);
	pdc_power_mgmt_wait_for_sync(fixture->port, -1);

	return 0;
}

#define MIN_DELAY_MS 250
#define MAX_DELAY_MS 3000
#define DELAY_INC_MS 50

/* Verify the DUT doesn't enable the sink path if the partner
 * sends new source caps rapidly. This test emulates behavior#
 * seen from PD compliance testers during the TEST.PD.PS.SNK.01
 * test.
 */
ZTEST_USER_F(pdc_attached_snk, test_0_amp_sink_contract)
{
	bool limit_reached;

	for (int i = MIN_DELAY_MS; i < MAX_DELAY_MS; i += DELAY_INC_MS) {
		zassert_ok(connect_default_charger_then_0w(fixture, i,
							   &limit_reached),
			   "Connecting charger failed at delay %d ms", i);

		if (limit_reached) {
			break;
		}
	}

	zassert_true(
		limit_reached,
		"DUT failed to enable sink after initial source caps sent");
};
