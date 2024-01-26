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

	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(2000));
	zassert_true(pdc_power_mgmt_is_connected(TEST_PORT));
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_get_polarity)
{
	struct connector_status_t connector_status;

	zassert_equal(POLARITY_COUNT, pdc_power_mgmt_pd_get_polarity(
					      CONFIG_USB_PD_PORT_MAX_COUNT));

	connector_status.orientation = 1;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_equal(POLARITY_CC2, pdc_power_mgmt_pd_get_polarity(TEST_PORT));

	connector_status.orientation = 0;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_equal(POLARITY_CC1, pdc_power_mgmt_pd_get_polarity(TEST_PORT));
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_get_data_role)
{
	struct connector_status_t connector_status;

	zassert_equal(
		PD_ROLE_DISCONNECTED,
		pdc_power_mgmt_pd_get_data_role(CONFIG_USB_PD_PORT_MAX_COUNT));

	connector_status.conn_partner_type = DFP_ATTACHED;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_equal(PD_ROLE_UFP, pdc_power_mgmt_pd_get_data_role(TEST_PORT));

	connector_status.conn_partner_type = UFP_ATTACHED;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_equal(PD_ROLE_DFP, pdc_power_mgmt_pd_get_data_role(TEST_PORT));
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_get_power_role)
{
	struct connector_status_t connector_status;
	zassert_equal(PD_ROLE_SINK, pdc_power_mgmt_get_power_role(
					    CONFIG_USB_PD_PORT_MAX_COUNT));

	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_equal(PD_ROLE_SOURCE, pdc_power_mgmt_get_power_role(TEST_PORT));

	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_equal(PD_ROLE_SINK, pdc_power_mgmt_get_power_role(TEST_PORT));
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_get_task_cc_state)
{
	zassert_equal(PD_CC_NONE, pdc_power_mgmt_get_task_cc_state(
					  CONFIG_USB_PD_PORT_MAX_COUNT));

/* TODO(b/321749548) - Read connector status after its read from I2C bus
 * not after reading PING status.
 */
#ifdef TODO_B_321749548
	int i;
	struct {
		enum conn_partner_type_t in;
		enum pd_cc_states out;
	} test[] = {
		{ .in = DFP_ATTACHED, .out = PD_CC_DFP_ATTACHED },
		{ .in = UFP_ATTACHED, .out = PD_CC_UFP_ATTACHED },
		{ .in = POWERED_CABLE_NO_UFP_ATTACHED, .out = PD_CC_NONE },
		{ .in = POWERED_CABLE_UFP_ATTACHED, .out = PD_CC_UFP_ATTACHED },
		{ .in = DEBUG_ACCESSORY_ATTACHED, .out = PD_CC_UFP_DEBUG_ACC },
		{ .in = AUDIO_ADAPTER_ACCESSORY_ATTACHED,
		  .out = PD_CC_UFP_AUDIO_ACC },
	};

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		struct connector_status_t connector_status;

		connector_status.conn_partner_type = test[i].in;
		emul_pdc_set_connector_status(emul, &connector_status);
		emul_pdc_pulse_irq(emul);
		k_sleep(K_MSEC(500));
		zassert_equal(test[i].out,
			      pdc_power_mgmt_get_task_cc_state(TEST_PORT));
	}
#endif /* TODO_B_321749548 */
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_capable)
{
	zassert_equal(false,
		      pdc_power_mgmt_pd_capable(CONFIG_USB_PD_PORT_MAX_COUNT));

/* TODO(b/321749548) - Read connector status after its read from I2C bus
 * not after reading PING status.
 */
#ifdef TODO_B_321749548
	struct connector_status_t connector_status;

	connector_status.connect_status = 0;
	emul_pdc_set_connector_status(emul, &connector_status);
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(500));
	zassert_equal(false, pdc_power_mgmt_pd_capable(TEST_PORT));

	connector_status.connect_status = 1;
	connector_status.power_operation_mode = USB_DEFAULT_OPERATION;
	emul_pdc_set_connector_status(emul, &connector_status);
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(500));
	zassert_equal(false, pdc_power_mgmt_pd_capable(TEST_PORT));

	connector_status.connect_status = 1;
	connector_status.power_operation_mode = PD_OPERATION;
	emul_pdc_set_connector_status(emul, &connector_status);
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(500));
	zassert_equal(true, pdc_power_mgmt_pd_capable(TEST_PORT));
#endif /* TODO_B_321749548 */
}

ZTEST_USER(pdc_power_mgmt_api, test_get_partner_usb_comm_capable)
{
	zassert_false(pdc_power_mgmt_get_partner_usb_comm_capable(
		CONFIG_USB_PD_PORT_MAX_COUNT));

/* TODO(b/321749548) - Read connector status after its read from I2C bus
 * not after reading PING status.
 */
#ifdef TODO_B_321749548
	struct connector_status_t connector_status;
	union connector_capability_t ccaps;

	connector_status.connect_status = 1;
	emul_pdc_set_connector_status(emul, &connector_status);

	ccaps.raw_value = 0;
	emul_pdc_set_connector_capability(emul, &ccaps);
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(500));
	zassert_false(pdc_power_mgmt_get_partner_usb_comm_capable(TEST_PORT));

	ccaps.raw_value = 0;
	ccaps.op_mode_usb2 = 1;
	emul_pdc_set_connector_capability(emul, &ccaps);
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(500));
	zassert_true(pdc_power_mgmt_get_partner_usb_comm_capable(TEST_PORT));

	ccaps.raw_value = 0;
	ccaps.op_mode_usb3 = 1;
	emul_pdc_set_connector_capability(emul, &ccaps);
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(500));
	zassert_true(pdc_power_mgmt_get_partner_usb_comm_capable(TEST_PORT));

	ccaps.raw_value = 0;
	ccaps.ext_op_mode_usb4_gen2 = 1;
	emul_pdc_set_connector_capability(emul, &ccaps);
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(500));
	zassert_true(pdc_power_mgmt_get_partner_usb_comm_capable(TEST_PORT));

	ccaps.raw_value = 0;
	ccaps.ext_op_mode_usb4_gen3 = 1;
	emul_pdc_set_connector_capability(emul, &ccaps);
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(500));
	zassert_true(pdc_power_mgmt_get_partner_usb_comm_capable(TEST_PORT));

	ccaps.raw_value = 0;
	ccaps.ext_op_mode_usb4_gen4 = 1;
	emul_pdc_set_connector_capability(emul, &ccaps);
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(500));
	zassert_true(pdc_power_mgmt_get_partner_usb_comm_capable(TEST_PORT));
#endif /* TODO_B_321749548 */
}
