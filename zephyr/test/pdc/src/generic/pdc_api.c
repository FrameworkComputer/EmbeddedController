/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "common.h"
#include "console.h"
#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc.h"
#include "i2c.h"
#include "include/ppm.h"
#include "pdc_trace_msg.h"
#include "zephyr/sys/util.h"
#include "zephyr/sys/util_macro.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_pdc_api, LOG_LEVEL_INF);

#define RTS5453P_NODE DT_NODELABEL(rts5453p_emul)
#define SLEEP_MS 120

static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);
static const struct device *dev = DEVICE_DT_GET(RTS5453P_NODE);
static bool test_cc_cb_called;
static union cci_event_t test_cc_cb_cci;

bool pdc_rts54xx_test_idle_wait(void);

void pdc_before_test(void *data)
{
	emul_pdc_reset(emul);
	emul_pdc_set_response_delay(emul, 0);
	if (IS_ENABLED(CONFIG_TEST_PDC_MESSAGE_TRACING)) {
		set_pdc_trace_msg_mocks();
	}

	zassert_true(pdc_rts54xx_test_idle_wait());

	test_cc_cb_called = false;
	test_cc_cb_cci.raw_value = 0;
}

ZTEST_SUITE(pdc_api, NULL, NULL, pdc_before_test, NULL, NULL);

ZTEST_USER(pdc_api, test_get_ucsi_version)
{
	uint16_t version = 0;

	zassert_not_ok(pdc_get_ucsi_version(dev, NULL));

	zassert_ok(pdc_get_ucsi_version(dev, &version));
	zassert_equal(version, UCSI_VERSION);
}

ZTEST_USER(pdc_api, test_reset)
{
	zassert_ok(pdc_reset(dev), "Failed to reset PDC");

	k_sleep(K_MSEC(500));
}

ZTEST_USER(pdc_api, test_connector_reset)
{
	union connector_reset_t in;
	union connector_reset_t out;

	in.raw_value = 0;
	out.raw_value = 0;

	in.reset_type = PD_DATA_RESET;

	emul_pdc_set_response_delay(emul, 50);
	zassert_ok(pdc_connector_reset(dev, in), "Failed to reset connector");

	k_sleep(K_MSEC(SLEEP_MS));
	emul_pdc_get_connector_reset(emul, &out);

	zassert_equal(in.reset_type, out.reset_type);
}

ZTEST_USER(pdc_api, test_get_capability)
{
	struct capability_t in, out;

	in.bcdBCVersion = 0x12;
	in.bcdPDVersion = 0x34;
	in.bcdUSBTypeCVersion = 0x56;

	zassert_ok(emul_pdc_set_capability(emul, &in));

	zassert_ok(pdc_get_capability(dev, &out), "Failed to get capability");

	k_sleep(K_MSEC(500));

	/* Verify versioning from emulator */
	zassert_equal(out.bcdBCVersion, in.bcdBCVersion);
	zassert_equal(out.bcdPDVersion, in.bcdPDVersion);
	zassert_equal(out.bcdUSBTypeCVersion, in.bcdUSBTypeCVersion);
}

ZTEST_USER(pdc_api, test_get_connector_capability)
{
	union connector_capability_t in, out;

	in.op_mode_rp_only = 1;
	in.op_mode_rd_only = 0;
	in.op_mode_usb2 = 1;
	zassert_ok(emul_pdc_set_connector_capability(emul, &in));

	zassert_ok(pdc_get_connector_capability(dev, &out),
		   "Failed to get connector capability");

	k_sleep(K_MSEC(SLEEP_MS));

	/* Verify data from emulator */
	zassert_equal(out.op_mode_rp_only, in.op_mode_rp_only);
	zassert_equal(out.op_mode_rd_only, in.op_mode_rd_only);
	zassert_equal(out.op_mode_usb2, in.op_mode_usb2);
}

ZTEST_USER(pdc_api, test_get_error_status)
{
	union error_status_t in, out;

	in.unrecognized_command = 1;
	in.contract_negotiation_failed = 0;
	in.invalid_command_specific_param = 1;
	zassert_ok(emul_pdc_set_error_status(emul, &in));

	zassert_ok(pdc_get_error_status(dev, &out),
		   "Failed to get connector capability");
	zassert_equal(pdc_get_error_status(dev, &out), -EBUSY);
	k_sleep(K_MSEC(SLEEP_MS));

	/* Verify data from emulator */
	zassert_equal(out.unrecognized_command, in.unrecognized_command);
	zassert_equal(out.contract_negotiation_failed,
		      in.contract_negotiation_failed);
	zassert_equal(out.invalid_command_specific_param,
		      in.invalid_command_specific_param);

	zassert_equal(pdc_get_error_status(dev, NULL), -EINVAL);
}

ZTEST_USER(pdc_api, test_get_connector_status)
{
	union connector_status_t in, out;
	union conn_status_change_bits_t in_conn_status_change_bits;
	union conn_status_change_bits_t out_conn_status_change_bits;

	in_conn_status_change_bits.external_supply_change = 1;
	in_conn_status_change_bits.connector_partner = 1;
	in_conn_status_change_bits.connect_change = 1;
	in.raw_conn_status_change_bits = in_conn_status_change_bits.raw_value;

	in.power_operation_mode = PD_OPERATION;
	in.connect_status = 1;
	in.power_direction = 0;
	in.conn_partner_flags = 1;
	in.conn_partner_type = UFP_ATTACHED;
	in.rdo = 0x01234567;

	zassert_ok(emul_pdc_set_connector_status(emul, &in));

	zassert_ok(pdc_get_connector_status(dev, &out),
		   "Failed to get connector capability");

	k_sleep(K_MSEC(SLEEP_MS));
	out_conn_status_change_bits.raw_value = out.raw_conn_status_change_bits;

	/* Verify data from emulator */
	zassert_equal(out_conn_status_change_bits.external_supply_change,
		      in_conn_status_change_bits.external_supply_change);
	zassert_equal(out_conn_status_change_bits.connector_partner,
		      in_conn_status_change_bits.connector_partner);
	zassert_equal(out_conn_status_change_bits.connect_change,
		      in_conn_status_change_bits.connect_change);
	zassert_equal(out.power_operation_mode, in.power_operation_mode);
	zassert_equal(out.connect_status, in.connect_status);
	zassert_equal(out.power_direction, in.power_direction);
	zassert_equal(out.conn_partner_flags, in.conn_partner_flags,
		      "out=0x%X != in=0x%X", out.conn_partner_flags,
		      in.conn_partner_flags);
	zassert_equal(out.conn_partner_type, in.conn_partner_type);
	zassert_equal(out.rdo, in.rdo);
}

ZTEST_USER(pdc_api, test_set_uor)
{
	union uor_t in, out;

	in.raw_value = 0;
	out.raw_value = 0;

	in.accept_dr_swap = 1;
	in.swap_to_ufp = 1;

	zassert_ok(pdc_set_uor(dev, in), "Failed to set uor");

	k_sleep(K_MSEC(SLEEP_MS));
	zassert_ok(emul_pdc_get_uor(emul, &out));

	zassert_equal(out.raw_value, in.raw_value);
}

ZTEST_USER(pdc_api, test_set_pdr)
{
	union pdr_t in, out;

	in.raw_value = 0;
	out.raw_value = 0;

	in.accept_pr_swap = 1;
	in.swap_to_src = 1;

	zassert_ok(pdc_set_pdr(dev, in), "Failed to set pdr");

	k_sleep(K_MSEC(SLEEP_MS));
	zassert_ok(emul_pdc_get_pdr(emul, &out));

	zassert_equal(out.raw_value, in.raw_value);
}

ZTEST_USER(pdc_api, test_rdo)
{
	uint32_t in, out = 0;

	in = BIT(25) | (BIT_MASK(9) & 0x55);
	zassert_ok(pdc_set_rdo(dev, in));

	k_sleep(K_MSEC(SLEEP_MS));
	zassert_ok(pdc_get_rdo(dev, &out));

	k_sleep(K_MSEC(SLEEP_MS));
	zassert_equal(in, out);
}

ZTEST_USER(pdc_api, test_set_power_level)
{
	int i;
	enum usb_typec_current_t out;
	enum usb_typec_current_t in[] = {
		TC_CURRENT_USB_DEFAULT,
		TC_CURRENT_1_5A,
		TC_CURRENT_3_0A,
	};

	zassert_equal(pdc_set_power_level(dev, TC_CURRENT_PPM_DEFINED),
		      -EINVAL);

	for (i = 0; i < ARRAY_SIZE(in); i++) {
		zassert_ok(pdc_set_power_level(dev, in[i]));

		k_sleep(K_MSEC(SLEEP_MS));
		emul_pdc_get_requested_power_level(emul, &out);
		zassert_equal(in[i], out);
	}
}

ZTEST_USER(pdc_api, test_get_bus_voltage)
{
	uint32_t mv_units = 50;
	uint32_t expected_voltage_mv = 5000;
	uint16_t out = 0;
	union connector_status_t in;

	in.voltage_scale = 10; /* 50 mv units*/
	in.voltage_reading = expected_voltage_mv / mv_units;
	emul_pdc_set_connector_status(emul, &in);

	zassert_ok(pdc_get_vbus_voltage(dev, &out));
	k_sleep(K_MSEC(SLEEP_MS));

	zassert_equal(out, expected_voltage_mv);

	zassert_equal(pdc_get_vbus_voltage(dev, NULL), -EINVAL);
}

ZTEST_USER(pdc_api, test_set_ccom)
{
	int i;
	enum ccom_t ccom_in[] = { CCOM_RP, CCOM_RD, CCOM_DRP };
	enum ccom_t ccom_out;

	k_sleep(K_MSEC(SLEEP_MS));

	for (i = 0; i < ARRAY_SIZE(ccom_in); i++) {
		zassert_ok(pdc_set_ccom(dev, ccom_in[i]));

		k_sleep(K_MSEC(SLEEP_MS));
		zassert_ok(emul_pdc_get_ccom(emul, &ccom_out));
		zassert_equal(ccom_in[i], ccom_out);
	}
}

ZTEST_USER(pdc_api, test_set_drp_mode)
{
	int i;
	enum drp_mode_t dm_in[] = { DRP_NORMAL, DRP_TRY_SRC, DRP_TRY_SNK };
	enum drp_mode_t dm_out;

	k_sleep(K_MSEC(SLEEP_MS));

	for (i = 0; i < ARRAY_SIZE(dm_in); i++) {
		zassert_ok(pdc_set_drp_mode(dev, dm_in[i]));

		k_sleep(K_MSEC(SLEEP_MS));
		zassert_ok(emul_pdc_get_drp_mode(emul, &dm_out));
		zassert_equal(dm_in[i], dm_out);
	}
}

ZTEST_USER(pdc_api, test_set_sink_path)
{
	int i;
	bool in[] = { true, false }, out;

	for (i = 0; i < ARRAY_SIZE(in); i++) {
		zassert_ok(pdc_set_sink_path(dev, in[i]));

		k_sleep(K_MSEC(SLEEP_MS));
		zassert_ok(emul_pdc_get_sink_path(emul, &out));

		zassert_equal(in[i], out);
	}
}

ZTEST_USER(pdc_api, test_reconnect)
{
	uint8_t expected, val;

	zassert_ok(pdc_reconnect(dev));

	k_sleep(K_MSEC(SLEEP_MS));
	zassert_ok(emul_pdc_get_reconnect_req(emul, &expected, &val));
	zassert_equal(expected, val);
}

/**
 * @brief Clears the cached PDC FW info struct inside the driver.
 */
void helper_clear_cached_chip_info(void)
{
	struct pdc_info_t zero = { 0 }, out;

	emul_pdc_set_info(emul, &zero);
	zassert_ok(pdc_get_info(dev, &out, true));
	k_sleep(K_MSEC(SLEEP_MS));
}

/* Two sets of chip info to test against */
static const struct pdc_info_t info_in1 = {
	.fw_version = 0x001a2b3c,
	.pd_version = 0xabcd,
	.pd_revision = 0x1234,
	.vid_pid = 0x12345678,
	.project_name = "ProjectName",
};

static const struct pdc_info_t info_in2 = {
	.fw_version = 0x002a3b4c,
	.pd_version = 0xef01,
	.pd_revision = 0x5678,
	.vid_pid = 0x9abcdef0,
	.project_name = "MyProj",
};

ZTEST_USER(pdc_api, test_get_info)
{
	struct pdc_info_t out = { 0 };

	/* Test output param NULL check */
	zassert_equal(-EINVAL, pdc_get_info(dev, NULL, true));

	/* Part 0: Cached read, but driver does not have valid cached info */

	helper_clear_cached_chip_info();
	zassert_equal(-EAGAIN, pdc_get_info(dev, &out, false));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Part 1: Live read -- Set `info_in1`, `out` should match `info_in1` */

	emul_pdc_set_info(emul, &info_in1);
	zassert_ok(pdc_get_info(dev, &out, true));
	k_sleep(K_MSEC(SLEEP_MS));

	zassert_equal(info_in1.fw_version, out.fw_version, "in=0x%X, out=0x%X",
		      info_in1.fw_version, out.fw_version);
	zassert_equal(info_in1.pd_version, out.pd_version);
	zassert_equal(info_in1.pd_revision, out.pd_revision);
	zassert_equal(info_in1.vid_pid, out.vid_pid, "in=0x%X, out=0x%X",
		      info_in1.vid_pid, out.vid_pid);
	zassert_mem_equal(info_in1.project_name, out.project_name,
			  sizeof(info_in1.project_name));

	/* Part 2: Cached read -- Set `info_in2`, `out` should match the cached
	 * `info_in1` again
	 */

	emul_pdc_set_info(emul, &info_in2);
	zassert_ok(pdc_get_info(dev, &out, false));
	k_sleep(K_MSEC(SLEEP_MS));

	zassert_equal(info_in1.fw_version, out.fw_version, "in=0x%X, out=0x%X",
		      info_in1.fw_version, out.fw_version);
	zassert_equal(info_in1.pd_version, out.pd_version);
	zassert_equal(info_in1.pd_revision, out.pd_revision);
	zassert_equal(info_in1.vid_pid, out.vid_pid, "in=0x%X, out=0x%X",
		      info_in1.vid_pid, out.vid_pid);
	zassert_mem_equal(info_in1.project_name, out.project_name,
			  sizeof(info_in1.project_name));

	/* Part 3: Live read -- Don't set emul, `out` should match `info_in2`
	 * this time
	 */

	zassert_ok(pdc_get_info(dev, &out, true));
	k_sleep(K_MSEC(SLEEP_MS));

	zassert_equal(info_in2.fw_version, out.fw_version, "in=0x%X, out=0x%X",
		      info_in2.fw_version, out.fw_version);
	zassert_equal(info_in2.pd_version, out.pd_version);
	zassert_equal(info_in2.pd_revision, out.pd_revision);
	zassert_equal(info_in2.vid_pid, out.vid_pid, "in=0x%X, out=0x%X",
		      info_in2.vid_pid, out.vid_pid);
	zassert_mem_equal(info_in2.project_name, out.project_name,
			  sizeof(info_in2.project_name));
}

/* PDO0 is reserved for a fixed PDO at 5V. */
ZTEST_USER(pdc_api, test_get_pdo)
{
	uint32_t fixed_pdo = 0;

	/* Test source fixed pdo. */
	zassert_ok(pdc_get_pdos(dev, SOURCE_PDO, PDO_OFFSET_0, 1, false,
				&fixed_pdo));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_equal(PDO_FIXED_GET_VOLT(fixed_pdo), 5000);

	/* Test sink fixed pdo. */
	fixed_pdo = 0;
	zassert_ok(pdc_get_pdos(dev, SINK_PDO, PDO_OFFSET_0, 1, false,
				&fixed_pdo));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_equal(PDO_FIXED_GET_VOLT(fixed_pdo), 5000);
}

ZTEST_USER(pdc_api, test_get_cable_property)
{
	/* Properties chosen to be spread throughout the bytes of the union. */
	const union cable_property_t property = {
		.b_current_capability = 50,
		.plug_end_type = USB_TYPE_C,
		.latency = 4,
	};
	union cable_property_t read_property;

	memset(&read_property, 0, sizeof(union cable_property_t));
	zassert_ok(emul_pdc_set_cable_property(emul, property));
	zassert_ok(emul_pdc_get_cable_property(emul, &read_property));
	zassert_ok(memcmp(&read_property, &property,
			  sizeof(union cable_property_t)));

	memset(&read_property, 0, sizeof(union cable_property_t));
	zassert_ok(pdc_get_cable_property(dev, &read_property));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_ok(memcmp(&read_property, &property,
			  sizeof(union cable_property_t)));
}

static void test_cc_cb(const struct device *dev,
		       const struct pdc_callback *callback,
		       union cci_event_t cci_event)
{
	test_cc_cb_called = true;
	test_cc_cb_cci = cci_event;
}

ZTEST_USER(pdc_api, test_execute_ucsi_cmd)
{
	struct ucsi_memory_region ucsi_data;
	struct ucsi_control *control = &ucsi_data.control;
	struct pdc_callback callback;
	union error_status_t in, *out;

	memset(&ucsi_data, 0, sizeof(ucsi_data));

	in.raw_value = 0;
	in.unrecognized_command = 1;
	zassert_ok(emul_pdc_set_error_status(emul, &in));

	control->command_specific[0] = 1;
	callback.handler = test_cc_cb;
	zassert_ok(pdc_execute_ucsi_cmd(dev, UCSI_CMD_GET_ERROR_STATUS, 1,
					control->command_specific,
					ucsi_data.message_in, &callback));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_true(test_cc_cb_called);
	zassert_true(test_cc_cb_cci.command_completed);

	out = (union error_status_t *)ucsi_data.message_in;
	zassert_equal(out->raw_value, in.raw_value);
}

ZTEST_USER(pdc_api, test_execute_ucsi_cmd_get_connector_status)
{
	struct ucsi_memory_region ucsi_data;
	struct ucsi_control *control = &ucsi_data.control;
	struct pdc_callback callback;
	union connector_status_t in;
	union connector_status_t *out =
		(union connector_status_t *)ucsi_data.message_in;

	memset(&ucsi_data, 0, sizeof(ucsi_data));
	memset(in.raw_value, 0, sizeof(in.raw_value));
	in.connect_status = 1;
	zassert_ok(emul_pdc_set_connector_status(emul, &in));

	/* Trigger IRQ to clear the cache. */
	emul_pdc_pulse_irq(emul);
	k_sleep(K_MSEC(SLEEP_MS));

	callback.handler = test_cc_cb;
	zassert_ok(pdc_execute_ucsi_cmd(dev, UCSI_CMD_GET_CONNECTOR_STATUS, 0,
					control->command_specific,
					ucsi_data.message_in, &callback));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_equal(out->connect_status, 1);

	/*
	 * Expect the command to ignore the emul status and return the previous
	 * status (from the cache).
	 */
	in.connect_status = 0;
	zassert_ok(emul_pdc_set_connector_status(emul, &in));
	zassert_ok(pdc_execute_ucsi_cmd(dev, UCSI_CMD_GET_CONNECTOR_STATUS, 0,
					control->command_specific,
					ucsi_data.message_in, &callback));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_equal(out->connect_status, 1);
}

/*
 * Suspended tests - ensure API calls behave correctly when PDC communication
 * is suspended.
 */

void *pdc_suspended_setup(void)
{
	struct pdc_info_t out;

	emul_pdc_reset(emul);
	emul_pdc_set_response_delay(emul, 0);
	if (IS_ENABLED(CONFIG_TEST_PDC_MESSAGE_TRACING)) {
		set_pdc_trace_msg_mocks();
	}

	/* Before suspending, force a read of chip info so the driver has
	 * something known cached.
	 */
	emul_pdc_set_info(emul, &info_in1);
	zassert_ok(pdc_get_info(dev, &out, true));
	k_sleep(K_MSEC(SLEEP_MS));

	/* Suspend chip communications */
	zassert_ok(pdc_set_comms_state(dev, false));

	return NULL;
}

void pdc_suspended_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_ok(pdc_set_comms_state(dev, true));
}

ZTEST_SUITE(pdc_api_suspended, NULL, pdc_suspended_setup, NULL, NULL,
	    pdc_suspended_teardown);

ZTEST_USER(pdc_api_suspended, test_get_info)
{
	struct pdc_info_t out;

	/* Live read should return busy because comms are blocked */
	zassert_equal(-EBUSY, pdc_get_info(dev, &out, true));

	/* Should still be able to get a cached read. */
	zassert_ok(pdc_get_info(dev, &out, false));

	/* Compare against the value we set in the suite setup function */
	zassert_equal(info_in1.fw_version, out.fw_version, "in=0x%X, out=0x%X",
		      info_in1.fw_version, out.fw_version);
	zassert_equal(info_in1.pd_version, out.pd_version);
	zassert_equal(info_in1.pd_revision, out.pd_revision);
	zassert_equal(info_in1.vid_pid, out.vid_pid, "in=0x%X, out=0x%X",
		      info_in1.vid_pid, out.vid_pid);
	zassert_mem_equal(info_in1.project_name, out.project_name,
			  sizeof(info_in1.project_name));
}
