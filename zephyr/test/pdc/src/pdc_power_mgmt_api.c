/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

#define RTS5453P_NODE DT_NODELABEL(rts5453p_emul)
#define EMUL_DATA rts5453p_emul_get_i2c_common_data(EMUL)

static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);
#define TEST_PORT 0

void pdc_power_mgmt_setup(void)
{
	zassume(TEST_PORT < CONFIG_USB_PD_PORT_MAX_COUNT,
		"TEST_PORT is invalid");

	emul_pdc_set_response_delay(emul, 0);
	emul_pdc_disconnect(emul);
	k_sleep(K_MSEC(1000));
}

ZTEST_SUITE(pdc_power_mgmt_api, NULL, pdc_power_mgmt_setup, NULL, NULL, NULL);

ZTEST_USER(pdc_power_mgmt_api, test_get_usb_pd_port_count)
{
	zassert_equal(CONFIG_USB_PD_PORT_MAX_COUNT,
		      pdc_power_mgmt_get_usb_pd_port_count());
}

ZTEST_USER(pdc_power_mgmt_api, test_is_connected)
{
	struct connector_status_t connector_status;

	zassert_false(
		pdc_power_mgmt_is_connected(CONFIG_USB_PD_PORT_MAX_COUNT));

	zassert_false(pdc_power_mgmt_is_connected(TEST_PORT));

	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_true(pdc_power_mgmt_is_connected(TEST_PORT));

	emul_pdc_disconnect(emul);
	k_sleep(K_MSEC(1000));
	zassert_false(pdc_power_mgmt_is_connected(TEST_PORT));

/* TODO: Add subcommands to support SNK Mode */
#ifdef EMUL_SNK_CMDS_SUPPORTED
	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_charger(emul, &connector_status);
	k_sleep(K_MSEC(2000));
	zassert_true(pdc_power_mgmt_is_connected(TEST_PORT));
#endif /* EMUL_SNK_CMDS_SUPPORTED */
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_get_polarity)
{
	zassert_equal(POLARITY_COUNT, pdc_power_mgmt_pd_get_polarity(
					      CONFIG_USB_PD_PORT_MAX_COUNT));

/* TODO(b/321749548) - Read connector status after its read from I2C bus
 * not after reading PING status.
 */
#ifdef TODO_B_321749548
	struct connector_status_t connector_status;

	connector_status.orientation = 0;
	emul_pdc_set_connector_status(emul, &connector_status);
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(500));
	zassert_equal(POLARITY_CC1, pdc_power_mgmt_pd_get_polarity(TEST_PORT));

	connector_status.orientation = 1;
	emul_pdc_set_connector_status(emul, &connector_status);
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(500));
	zassert_equal(POLARITY_CC2, pdc_power_mgmt_pd_get_polarity(TEST_PORT));
#endif /* TODO_B_321749548 */
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_get_data_role)
{
	zassert_equal(
		PD_ROLE_DISCONNECTED,
		pdc_power_mgmt_pd_get_data_role(CONFIG_USB_PD_PORT_MAX_COUNT));

/* TODO(b/321749548) - Read connector status after its read from I2C bus
 * not after reading PING status.
 */
#ifdef TODO_B_321749548
	struct connector_status_t connector_status;

	connector_status.conn_partner_type = DFP_ATTACHED;
	emul_pdc_set_connector_status(emul, &connector_status);
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(500));
	zassert_equal(PD_ROLE_UFP, pdc_power_mgmt_pd_get_data_role(TEST_PORT));

	connector_status.conn_partner_type = UFP_ATTACHED;
	emul_pdc_set_connector_status(emul, &connector_status);
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(500));
	zassert_equal(PD_ROLE_DFP, pdc_power_mgmt_pd_get_data_role(TEST_PORT));
#endif /* TODO_B_321749548 */
}
