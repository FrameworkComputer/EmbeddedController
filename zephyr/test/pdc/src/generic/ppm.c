/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "ec_commands.h"
#include "emul/emul_pdc.h"
#include "ppm_common.h"
#include "usbc/pdc_power_mgmt.h"
#include "usbc/ppm.h"
#include "usbc/utils.h"
#include "zephyr/logging/log.h"

#include <stdbool.h>

#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(ucsi_ppm_test, LOG_LEVEL_DBG);

#define DT_PPM_DRV DT_INST(0, ucsi_ppm)
#define NUM_PORTS DT_PROP_LEN(DT_PPM_DRV, lpm)
#define PDC_WAIT_FOR_ITERATIONS 30
#define PDC_EMUL_NODE DT_NODELABEL(pdc_emul1)
#define PDC_EMUL_PORT USBC_PORT_FROM_DRIVER_NODE(PDC_EMUL_NODE, pdc)
#define PPM_CONNECTOR_NUM (PDC_EMUL_PORT + 1)

static struct ucsi_ppm_device *ppm_dev;
static const struct emul *emul = EMUL_DT_GET(PDC_EMUL_NODE);

static const struct ucsi_control_t enable_all_notifications = {
	.command = UCSI_SET_NOTIFICATION_ENABLE,
	.data_length = 0,
	.command_specific = { 0xff, 0xff, 0x1, 0x0, 0x0, 0x0 },
};

static void host_cmd_pdc_reset(void *fixture)
{
	const struct device *pdc;
	const struct ucsi_pd_driver *drv;

	pdc = DEVICE_DT_GET(DT_INST(0, ucsi_ppm));
	drv = pdc->api;
	ppm_dev = drv->get_ppm_dev(pdc);
	emul_pdc_reset(emul);
}

ZTEST_SUITE(ucsi_ppm, NULL, NULL, host_cmd_pdc_reset, NULL, NULL);

static int write_command(const struct ucsi_control_t *control)
{
	return ucsi_ppm_write(ppm_dev, UCSI_CONTROL_OFFSET,
			      (const void *)control,
			      sizeof(struct ucsi_control_t));
}

static int write_ack_command(bool connector_change_ack,
			     bool command_complete_ack)
{
	struct ucsi_control_t control = { .command = UCSI_ACK_CC_CI,
					  .data_length = 0 };
	union ack_cc_ci_t ack_data = {
		.connector_change_ack = connector_change_ack,
		.command_complete_ack = command_complete_ack
	};
	memcpy(control.command_specific, &ack_data, sizeof(ack_data));
	return write_command(&control);
}

/**
 * Return true if commands are no longer pending.
 */
static bool wait_for_cmd_to_process(void)
{
	/*
	 * After calling write, the command will be pending and will trigger the
	 * main loop. Try reading the pending state a few times to see if it
	 * clears.
	 */
	for (int i = 0; i < PDC_WAIT_FOR_ITERATIONS; ++i) {
		if (ppm_test_is_cmd_pending(ppm_dev)) {
			k_msleep(1);
		} else {
			return true;
		}
	}

	return false;
}

static bool reset_to_idle_notify(void)
{
	struct ucsi_control_t ctrl = {};

	LOG_INF("Sending UCSI_PPM_RESET");
	ctrl.command = UCSI_PPM_RESET;
	ctrl.data_length = 0;
	if (write_command(&ctrl)) {
		LOG_ERR("Failed to write command");
		return false;
	}
	if (!wait_for_cmd_to_process()) {
		LOG_ERR("Timeout waiting for command process)");
		return false;
	}

	LOG_INF("Sending SET_NOTIFICATION_ENABLE");
	if (write_command(&enable_all_notifications)) {
		LOG_ERR("Failed to write command");
		return false;
	}
	if (!wait_for_cmd_to_process()) {
		LOG_ERR("Timeout waiting for command process)");
		return false;
	}

	LOG_INF("Acking SET_NOTIFICATION_ENABLE");
	if (write_ack_command(false, true)) {
		LOG_ERR("Failed to ack command");
		return false;
	}
	if (!wait_for_cmd_to_process()) {
		LOG_ERR("Timeout waiting for command process)");
		return false;
	}

	return true;
}

static bool read_cci(union cci_event_t *cci)
{
	return ucsi_ppm_read(ppm_dev, UCSI_CCI_OFFSET, (void *)cci,
			     sizeof(*cci)) == sizeof(*cci);
}

static bool read_message_in(void *data, size_t len)
{
	return ucsi_ppm_read(ppm_dev, UCSI_MESSAGE_IN_OFFSET, data, len) == len;
}

ZTEST(ucsi_ppm, test_set_notification_enable)
{
	zassert_true(reset_to_idle_notify());
}

ZTEST(ucsi_ppm, test_invalid_conn)
{
	struct ucsi_control_t ctrl = {};
	union cci_event_t cci;

	zassert_true(reset_to_idle_notify());

	/*
	 * Test conn=0 using CONNECTOR_RESET.
	 */
	LOG_INF("Sending CONNECTOR_RESET");
	ctrl.command = UCSI_CONNECTOR_RESET;
	ctrl.command_specific[0] = 0;
	zassert_ok(write_command(&ctrl));
	zassert_true(wait_for_cmd_to_process());

	zassert_true(read_cci(&cci));
	zassert_equal(cci.error, 1);
	zassert_equal(cci.command_completed, 1);

	LOG_INF("Acking CONNECTOR_RESET");
	zassert_ok(write_ack_command(false, true));
	zassert_true(wait_for_cmd_to_process());

	/*
	 * Test conn=3 using CONNECTOR_RESET.
	 */
	LOG_INF("Sending CONNECTOR_RESET");
	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.command = UCSI_CONNECTOR_RESET;
	ctrl.command_specific[0] = NUM_PORTS + 1;
	zassert_ok(write_command(&ctrl));
	zassert_true(wait_for_cmd_to_process());

	zassert_true(read_cci(&cci));
	zassert_equal(cci.error, 1);
	zassert_equal(cci.command_completed, 1);

	LOG_INF("Acking CONNECTOR_RESET");
	zassert_equal(write_ack_command(false, true), 0);
	zassert_true(wait_for_cmd_to_process());
}

ZTEST(ucsi_ppm, test_get_connector_capability)
{
	struct ucsi_control_t ctrl = {};
	union cci_event_t cci;

	zassert_true(reset_to_idle_notify());

	LOG_INF("Sending GET_CONNECTOR_CAPABILITY");
	ctrl.command = UCSI_GET_CONNECTOR_CAPABILITY;
	ctrl.data_length = 0;
	ctrl.command_specific[0] = PPM_CONNECTOR_NUM;
	zassert_equal(write_command(&ctrl), 0);
	zassert_true(wait_for_cmd_to_process());

	zassert_true(read_cci(&cci));
	zassert_equal(cci.command_completed, 1);
	zassert_true(cci.error != 1);
	zassert_equal(cci.data_len, 0x04);
}

ZTEST(ucsi_ppm, test_get_capability)
{
	struct ucsi_control_t ctrl = {};
	union cci_event_t cci;
	struct capability_t caps = {}, ecaps = {};

	zassert_true(reset_to_idle_notify());

	/*
	 * Set numConns to a wrong number. PPM should ignore it and get the
	 * right value from the device tree.
	 */
	ecaps.bNumConnectors = NUM_PORTS + 1;
	emul_pdc_set_capability(emul, &ecaps);

	LOG_INF("Sending GET_CAPABILITY");
	ctrl.command = UCSI_GET_CAPABILITY;
	zassert_equal(write_command(&ctrl), 0);
	zassert_true(wait_for_cmd_to_process());

	zassert_true(read_cci(&cci));
	zassert_equal(cci.command_completed, 1);
	zassert_true(cci.error != 1);
	zassert_equal(cci.data_len, 0x10);

	LOG_INF("Acking GET_CAPABILITY");
	zassert_ok(write_ack_command(false, true));
	zassert_true(wait_for_cmd_to_process());

	zassert_true(read_message_in((uint8_t *)&caps, sizeof(caps)));
	zassert_equal(caps.bNumConnectors, NUM_PORTS,
		      "%d (#ports from PPM) != %d (#ports from DT)",
		      caps.bNumConnectors, NUM_PORTS);
}

ZTEST(ucsi_ppm, test_get_connector_status)
{
	struct ucsi_control_t ctrl = {};
	union cci_event_t cci;
	union connector_status_t csts = {};

	zassert_true(reset_to_idle_notify());

	LOG_INF("Sending GET_CONNECTOR_STATUS");
	ctrl.command = UCSI_GET_CONNECTOR_STATUS;
	ctrl.command_specific[0] = PPM_CONNECTOR_NUM;
	zassert_ok(write_command(&ctrl));
	zassert_true(wait_for_cmd_to_process());

	zassert_true(read_cci(&cci));
	zassert_equal(cci.command_completed, 1);
	zassert_true(cci.error != 1);
	zassert_equal(cci.data_len, 0x13);

	LOG_INF("Acking GET_CONNECTOR_STATUS");
	zassert_ok(write_ack_command(false, true));
	zassert_true(wait_for_cmd_to_process());

	zassert_true(read_message_in((uint8_t *)&csts, sizeof(csts)));
	zassert_equal(csts.connect_status, 0);

	LOG_INF("Connecting a partner");
	csts.power_operation_mode = USB_TC_CURRENT_1_5A;
	emul_pdc_connect_partner(emul, &csts);
	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(PDC_EMUL_PORT));

	LOG_INF("Sending GET_CONNECTOR_STATUS");
	zassert_ok(write_command(&ctrl));
	zassert_true(wait_for_cmd_to_process());

	LOG_INF("Acking GET_CONNECTOR_STATUS");
	zassert_ok(write_ack_command(false, true));
	zassert_true(wait_for_cmd_to_process());

	zassert_true(read_message_in((uint8_t *)&csts, sizeof(csts)));
	zassert_equal(csts.connect_status, 1);
}

ZTEST(ucsi_ppm, test_set_sink_path)
{
	struct ucsi_control_t ctrl = {};
	union cci_event_t cci;
	union connector_status_t csts = {};
	union set_sink_path_t *sp;

	zassert_true(reset_to_idle_notify());

	/*
	 * Test SET_SINK_PATH when sink is disconnected.
	 */
	emul_pdc_disconnect(emul);
	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(PDC_EMUL_PORT));

	LOG_INF("Sending SET_SINK_PATH while disconnected");
	ctrl.command = UCSI_SET_SINK_PATH;
	sp = (union set_sink_path_t *)&ctrl.command_specific[0];
	sp->connector_number = PPM_CONNECTOR_NUM;
	sp->sink_path_enable = 1;
	zassert_ok(write_command(&ctrl));
	zassert_true(wait_for_cmd_to_process());

	zassert_true(read_cci(&cci));
	zassert_true(cci.command_completed);
	zassert_true(cci.error);
	zassert_equal(cci.data_len, 0);

	LOG_INF("Acking SET_SINK_PATH");
	zassert_ok(write_ack_command(false, true));
	zassert_true(wait_for_cmd_to_process());

	/*
	 * Test SET_SINK_PATH when sink is connected.
	 */
	emul_pdc_configure_snk(emul, &csts);
	emul_pdc_connect_partner(emul, &csts);
	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(PDC_EMUL_PORT));

	LOG_INF("Sending SET_SINK_PATH as a sink");
	zassert_ok(write_command(&ctrl));
	zassert_true(wait_for_cmd_to_process());

	zassert_true(read_cci(&cci));
	zassert_true(cci.command_completed);
	zassert_false(cci.error);
	zassert_equal(cci.data_len, 0);

	LOG_INF("Acking SET_SINK_PATH");
	zassert_ok(write_ack_command(false, true));
	zassert_true(wait_for_cmd_to_process());

	/*
	 * Test SET_SINK_PATH when source is connected.
	 */
	emul_pdc_configure_src(emul, &csts);
	emul_pdc_connect_partner(emul, &csts);
	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(PDC_EMUL_PORT));

	LOG_INF("Sending SET_SINK_PATH to a source");
	zassert_ok(write_command(&ctrl));
	zassert_true(wait_for_cmd_to_process());

	zassert_true(read_cci(&cci));
	zassert_true(cci.command_completed);
	zassert_true(cci.error);

	LOG_INF("Acking SET_SINK_PATH");
	zassert_ok(write_ack_command(false, true));
	zassert_true(wait_for_cmd_to_process());
}
