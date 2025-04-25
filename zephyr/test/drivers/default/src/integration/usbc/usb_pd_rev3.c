/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "chipset.h"
#include "ec_commands.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "hooks.h"
#include "host_command.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "util.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/slist.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_drivers_integration_usb_pd_rev3, LOG_LEVEL_INF);

#define TEST_PORT 0

struct usb_attach_5v_3a_pd_source_rev3_fixture {
	struct tcpci_partner_data source_5v_3a;
	struct tcpci_src_emul_data src_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void *usb_attach_5v_3a_pd_source_setup(void)
{
	static struct usb_attach_5v_3a_pd_source_rev3_fixture test_fixture;

	/* Get references for the emulators */
	test_fixture.tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	test_fixture.charger_emul = EMUL_GET_USBC_BINDING(0, chg);

	/* Initialized the charger to supply 5V and 3A */
	tcpci_partner_init(&test_fixture.source_5v_3a, PD_REV30);
	test_fixture.source_5v_3a.extensions = tcpci_src_emul_init(
		&test_fixture.src_ext, &test_fixture.source_5v_3a, NULL);
	test_fixture.src_ext.pdo[1] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &test_fixture;
}

static void usb_attach_5v_3a_pd_source_before(void *data)
{
	struct usb_attach_5v_3a_pd_source_rev3_fixture *fixture = data;

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	/* Set the partner's USB PD Revision to 3.1 */
	fixture->source_5v_3a.rmdo = 0x31000000;

	connect_source_to_port(&fixture->source_5v_3a, &fixture->src_ext, 1,
			       fixture->tcpci_emul, fixture->charger_emul);

	/* Clear Alert and Status receive checks; clear message log */
	tcpci_src_emul_clear_alert_received(&fixture->src_ext);
	tcpci_src_emul_clear_status_received(&fixture->src_ext);
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);
	tcpci_partner_common_clear_logged_msgs(&fixture->source_5v_3a);

	/* Initial check on power state */
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
}

static void usb_attach_5v_3a_pd_source_after(void *data)
{
	struct usb_attach_5v_3a_pd_source_rev3_fixture *fixture = data;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
}

ZTEST_SUITE(usb_attach_5v_3a_pd_source_rev3, drivers_predicate_post_main,
	    usb_attach_5v_3a_pd_source_setup, usb_attach_5v_3a_pd_source_before,
	    usb_attach_5v_3a_pd_source_after, NULL);

ZTEST_F(usb_attach_5v_3a_pd_source_rev3, test_batt_cap)
{
	int battery_index = 0;

	tcpci_partner_common_send_get_battery_capabilities(
		&fixture->source_5v_3a, battery_index);

	/* Allow some time for TCPC to process and respond */
	k_sleep(K_SECONDS(1));

	zassert_true(fixture->source_5v_3a.battery_capabilities
			     .have_response[battery_index],
		     "No battery capabilities response stored.");

	/* The response */
	struct pd_bcdb *bcdb =
		&fixture->source_5v_3a.battery_capabilities.bcdb[battery_index];

	zassert_equal(USB_VID_GOOGLE, bcdb->vid, "Incorrect battery VID");
	zassert_equal(CONFIG_USB_PID, bcdb->pid, "Incorrect battery PID");
	zassert_false((bcdb->battery_type) & BIT(0),
		      "Invalid battery ref bit should not be set");

	/* Verify the battery capacity and last full charge capacity. These
	 * fields require that the battery is present and that we can
	 * access information about the nominal voltage and capacity.
	 *
	 * TODO(b/237427945): Add test for case when battery is not present
	 */

	/* See pe_give_battery_cap_entry() in common/usbc/usb_pe_drp_sm.c */

	zassert_true(battery_is_present(), "Battery must be present");
	zassert_true(IS_ENABLED(HAS_TASK_HOSTCMD) &&
			     *host_get_memmap(EC_MEMMAP_BATTERY_VERSION) != 0,
		     "Cannot access battery data");

	/* Millivolts */
	int design_volt = *(int *)host_get_memmap(EC_MEMMAP_BATT_DVLT);

	/* Milliamphours */
	int design_cap = *(int *)host_get_memmap(EC_MEMMAP_BATT_DCAP);
	int full_cap = *(int *)host_get_memmap(EC_MEMMAP_BATT_LFCC);

	/* Multiply millivolts by milliamphours and scale to deciwatthours
	 * (0.1 Wh), the unit of energy used in the PD messages.
	 */

	int expected_design_cap =
		DIV_ROUND_NEAREST((design_cap * design_volt), 1000 * 1000 / 10);

	int expected_last_charge_cap =
		DIV_ROUND_NEAREST((design_cap * full_cap), 1000 * 1000 / 10);

	zassert_equal(expected_design_cap, bcdb->design_cap,
		      "Design capacity not correct. Expected %d but got %d",
		      expected_design_cap, bcdb->design_cap);
	zassert_equal(
		expected_last_charge_cap, bcdb->last_full_charge_cap,
		"Last full charge capacity not correct. Expected %d but got %d",
		expected_last_charge_cap, bcdb->last_full_charge_cap);
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3, test_batt_cap_invalid)
{
	/* Request data on a battery that does not exist. The PD stack only
	 * supports battery 0.
	 */

	int battery_index = 5;

	tcpci_partner_common_send_get_battery_capabilities(
		&fixture->source_5v_3a, battery_index);

	/* Allow some time for TCPC to process and respond */
	k_sleep(K_SECONDS(1));

	/* Ensure we get a response that says our battery index was invalid */

	zassert_true(fixture->source_5v_3a.battery_capabilities
			     .have_response[battery_index],
		     "No battery capabilities response stored.");
	zassert_true(
		(fixture->source_5v_3a.battery_capabilities.bcdb[battery_index]
			 .battery_type) &
			BIT(0),
		"Invalid battery ref bit should be set");
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3, test_typec_status_using_rmdo)
{
	struct ec_params_typec_status params = { .port = TEST_PORT };
	struct ec_response_typec_status response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_TYPEC_STATUS, 0, response, params);

	/* Check that the revision response in EC_CMD_TYPEC_STATUS matches
	 * bits 16-31 of the partner's RMDO
	 */
	zassert_ok(host_command_process(&args));
	zassert_equal(args.response_size, sizeof(response));
	zassert_equal(response.sop_revision, fixture->source_5v_3a.rmdo >> 16);
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3, test_alert_msg)
{
	zassert_equal(pd_broadcast_alert_msg(ADO_OTP_EVENT), EC_SUCCESS);

	k_sleep(K_SECONDS(2));
	zassert_true(fixture->src_ext.alert_received);
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3, test_alert_on_power_state_change)
{
	/* Suspend and check partner received Alert and Status messages */
	hook_notify(HOOK_CHIPSET_SUSPEND);
	k_sleep(K_SECONDS(2));
	zassert_true(fixture->src_ext.alert_received);
	zassert_true(fixture->src_ext.status_received);
	tcpci_src_emul_clear_alert_received(&fixture->src_ext);
	tcpci_src_emul_clear_status_received(&fixture->src_ext);
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);

	/* Shutdown and check partner received Alert and Status messages */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	k_sleep(K_SECONDS(2));
	zassert_true(fixture->src_ext.alert_received);
	zassert_true(fixture->src_ext.status_received);
	tcpci_src_emul_clear_alert_received(&fixture->src_ext);
	tcpci_src_emul_clear_status_received(&fixture->src_ext);
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);

	/* Startup and check partner received Alert and Status messages */
	hook_notify(HOOK_CHIPSET_STARTUP);
	k_sleep(K_SECONDS(2));
	zassert_true(fixture->src_ext.alert_received);
	zassert_true(fixture->src_ext.status_received);
	tcpci_src_emul_clear_alert_received(&fixture->src_ext);
	tcpci_src_emul_clear_status_received(&fixture->src_ext);
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);

	/* Resume and check partner received Alert and Status messages */
	hook_notify(HOOK_CHIPSET_RESUME);
	k_sleep(K_SECONDS(2));
	zassert_true(fixture->src_ext.alert_received);
	zassert_true(fixture->src_ext.status_received);
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3,
	test_simultaneous_alert_status_resolution)
{
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);

	tcpci_partner_common_enable_pd_logging(&fixture->source_5v_3a, true);
	zassert_equal(pd_broadcast_alert_msg(ADO_OTP_EVENT), EC_SUCCESS);
	tcpci_partner_send_control_msg(&fixture->source_5v_3a,
				       PD_CTRL_GET_STATUS, 0);
	k_sleep(K_SECONDS(2));
	tcpci_partner_common_enable_pd_logging(&fixture->source_5v_3a, false);

	/*
	 * The initial Alert message will be discarded, so the expected message
	 * order is Get_Status->Status->Alert. This will be followed by another
	 * Get_Status->Status transaction, but that is covered in other tests.
	 * This test only checks the first 3 messages.
	 */
	int i = 0;
	enum tcpci_partner_msg_sender expected_senders[3] = {
		TCPCI_PARTNER_SENDER_PARTNER, TCPCI_PARTNER_SENDER_TCPM,
		TCPCI_PARTNER_SENDER_TCPM
	};
	uint16_t expected_headers[3] = { 0x0012, 0xb002, 0x1006 };
	enum tcpci_partner_msg_sender actual_senders[3];
	uint16_t actual_headers[3] = { 0, 0, 0 };
	struct tcpci_partner_log_msg *msg;

	SYS_SLIST_FOR_EACH_CONTAINER(&fixture->source_5v_3a.msg_log, msg, node)
	{
		uint16_t header = sys_get_le16(msg->buf);

		if (i >= 3)
			break;

		actual_senders[i] = msg->sender;
		actual_headers[i] = header;

		i++;
	}
	zassert_true(i >= 3, "Expected 3 messages but recorded only %i", i);

	LOG_DBG("Checking expected Get_Status message");
	zexpect_equal(actual_senders[0], expected_senders[0],
		      "Expected message 0 sent from partner, got %u",
		      actual_senders[0]);
	uint16_t header = actual_headers[0];
	uint16_t exp_header = expected_headers[0];
	zexpect_equal(PD_HEADER_EXT(header), PD_HEADER_EXT(exp_header),
		      "Expected message 0 not extended, got header 0x%04x",
		      header);
	zexpect_equal(PD_HEADER_CNT(header), PD_HEADER_CNT(exp_header),
		      "Expected message 0 count %u, got header 0x%04x",
		      PD_HEADER_CNT(exp_header), header);
	zexpect_equal(PD_HEADER_TYPE(header), PD_HEADER_TYPE(exp_header),
		      "Expected message 0 type %u, got header 0x%04x",
		      PD_HEADER_TYPE(exp_header), header);

	LOG_DBG("Checking expected Status message");
	zexpect_equal(actual_senders[1], expected_senders[1],
		      "Expected message 1 sent from TCPM, got %u",
		      actual_senders[1]);
	header = actual_headers[1];
	exp_header = expected_headers[1];
	zexpect_equal(PD_HEADER_EXT(header), PD_HEADER_EXT(exp_header),
		      "Expected message 1 extended, got header 0x%04x", header);
	zexpect_equal(PD_HEADER_CNT(header), PD_HEADER_CNT(exp_header),
		      "Expected message 1 count %u, got header 0x%04x",
		      PD_HEADER_CNT(exp_header), header);
	zexpect_equal(PD_HEADER_TYPE(header), PD_HEADER_TYPE(exp_header),
		      "Expected message 1 type %u, got header 0x%04x",
		      PD_HEADER_TYPE(exp_header), header);

	LOG_DBG("Checking expected Alert message");
	zexpect_equal(actual_senders[2], expected_senders[2],
		      "Expected message 2 sent from TCPM, got %u",
		      actual_senders[2]);
	header = actual_headers[2];
	exp_header = expected_headers[2];
	zexpect_equal(PD_HEADER_EXT(header), PD_HEADER_EXT(exp_header),
		      "Expected message 2 not extended, got header 0x%04x",
		      header);
	zexpect_equal(PD_HEADER_CNT(header), PD_HEADER_CNT(exp_header),
		      "Expected message 2 count %u, got header 0x%04x",
		      PD_HEADER_CNT(exp_header), header);
	zexpect_equal(PD_HEADER_TYPE(header), PD_HEADER_TYPE(exp_header),
		      "Expected message 2 type %u, got header 0x%04x",
		      PD_HEADER_TYPE(exp_header), header);

	zexpect_true(fixture->src_ext.alert_received);
	zexpect_true(fixture->src_ext.status_received);
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3,
	test_inaction_on_pd_button_press_while_awake)
{
	uint32_t ado;

	/* While awake expect nothing on valid press */
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_PRESS;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(2));
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_RELEASE;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(2));
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3,
	test_inaction_on_invalid_pd_button_press)
{
	uint32_t ado;

	/* Shutdown device to test wake from USB PD power button */
	chipset_force_shutdown(CHIPSET_SHUTDOWN_BUTTON);
	k_sleep(K_SECONDS(10));

	/* Clear alert and status flags set during shutdown */
	tcpci_src_emul_clear_alert_received(&fixture->src_ext);
	tcpci_src_emul_clear_status_received(&fixture->src_ext);
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);
	zassert_true(chipset_in_state(CHIPSET_STATE_ANY_OFF));

	/* While in S5/G3 expect nothing on invalid (too long) press */
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_PRESS;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(10));
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_RELEASE;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(2));
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);
	zassert_true(chipset_in_state(CHIPSET_STATE_ANY_OFF));

	/* Wake device to setup for subsequent tests */
	chipset_power_on();
	k_sleep(K_SECONDS(10));
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3, test_startup_on_pd_button_press)
{
	uint32_t ado;

	/* Shutdown device to test wake from USB PD power button. Shutting down
	 * the device may involve a Hard Reset upon entry to G3 (10 seconds
	 * after S5). Wait long enough for that process to complete.
	 */
	chipset_force_shutdown(CHIPSET_SHUTDOWN_BUTTON);
	k_sleep(K_SECONDS(15));

	/* Clear alert and status flags set during shutdown */
	tcpci_src_emul_clear_alert_received(&fixture->src_ext);
	tcpci_src_emul_clear_status_received(&fixture->src_ext);
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);
	zassert_true(chipset_in_state(CHIPSET_STATE_ANY_OFF));

	/* While in S5/G3 expect Alert->Get_Status->Status on valid press */
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_PRESS;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(2));
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_RELEASE;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(2));
	zassert_true(fixture->src_ext.alert_received);
	zassert_true(fixture->src_ext.status_received);
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3, test_chipset_on_pd_button_behavior)
{
	uint32_t ado;

	/* Expect no power state change on short press */
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_PRESS;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(2));
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_RELEASE;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(2));
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));

	/* Expect no change on invalid button press while chipset is on */
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_PRESS;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(10));
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_RELEASE;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(2));
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));

	/*
	 * Expect no power state change on 6 second press->press->release due
	 * to the timers resetting on the second press.
	 */
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_PRESS;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(3));
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(3));
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_RELEASE;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(2));
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));

	/* Expect power state change on long press */
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_PRESS;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(6));
	ado = ADO_EXTENDED_ALERT_EVENT | ADO_POWER_BUTTON_RELEASE;
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_ALERT, &ado,
				    1, 0);
	k_sleep(K_SECONDS(2));
	zassert_true(fixture->src_ext.alert_received);
	zassert_true(fixture->src_ext.status_received);
	zassert_true(chipset_in_state(CHIPSET_STATE_ANY_OFF));

	/* Wake device to setup for subsequent tests */
	chipset_power_on();
	k_sleep(K_SECONDS(10));
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3, test_uvdm_not_supported)
{
	uint32_t vdm_header = VDO(USB_VID_GOOGLE, 0 /* unstructured */, 0);

	tcpci_partner_common_enable_pd_logging(&fixture->source_5v_3a, true);
	tcpci_partner_send_data_msg(&fixture->source_5v_3a, PD_DATA_VENDOR_DEF,
				    &vdm_header, 1, 0);
	k_sleep(K_SECONDS(1));
	tcpci_partner_common_enable_pd_logging(&fixture->source_5v_3a, false);

	bool not_supported_seen = false;
	struct tcpci_partner_log_msg *msg;

	/* The TCPM does not support any unstructured VDMs. In PD 3.0, it should
	 * respond with Not_Supported.
	 */

	SYS_SLIST_FOR_EACH_CONTAINER(&fixture->source_5v_3a.msg_log, msg, node)
	{
		uint16_t header = sys_get_le16(msg->buf);

		/* Ignore messages from the port partner. */
		if (msg->sender == TCPCI_PARTNER_SENDER_PARTNER) {
			continue;
		}

		if (msg->sender == TCPCI_PARTNER_SENDER_TCPM &&
		    PD_HEADER_GET_SOP(header) == TCPCI_MSG_SOP &&
		    PD_HEADER_CNT(header) == 0 && PD_HEADER_EXT(header) == 0 &&
		    PD_HEADER_TYPE(header) == PD_CTRL_NOT_SUPPORTED) {
			not_supported_seen = true;
			break;
		}
	}

	zassert_true(
		not_supported_seen,
		"Sent unstructured VDM to TCPM; did not receive Not_Supported");
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3, test_give_revision)
{
	const uint32_t expected_rev = 0x32100000;

	fixture->source_5v_3a.tcpm_rmdo = 0;
	tcpci_partner_send_control_msg(&fixture->source_5v_3a,
				       PD_CTRL_GET_REVISION, 0);
	k_sleep(K_SECONDS(2));
	zassert_equal(fixture->source_5v_3a.tcpm_rmdo, expected_rev,
		      "Expected RMDO 0x%04x, got 0x%04x", expected_rev,
		      fixture->source_5v_3a.tcpm_rmdo);
}

uint8_t get_status_data_size(sys_slist_t *msg_log)
{
	struct tcpci_partner_log_msg *msg;
	uint8_t data_size = 0;

	SYS_SLIST_FOR_EACH_CONTAINER(msg_log, msg, node)
	{
		uint16_t header = sys_get_le16(msg->buf);
		if (msg->sender != TCPCI_PARTNER_SENDER_TCPM ||
		    !PD_HEADER_EXT(header) ||
		    PD_HEADER_TYPE(header) != PD_EXT_STATUS) {
			continue;
		}

		uint16_t ext_header = sys_get_le16(msg->buf + sizeof(uint16_t));
		data_size = PD_EXT_HEADER_DATA_SIZE(ext_header);
		break;
	}

	return data_size;
}

/* The TCPM should respond to Get_Status with a 7-byte Status as defined in PD
 * r3.1 and newer, regardless of the partner's response (or non-response) to
 * Get_Revision. Test two cases to verify this. See b/366241394 for details.
 */
ZTEST_F(usb_attach_5v_3a_pd_source_rev3, test_get_status_response_pd31)
{
	tcpci_partner_common_enable_pd_logging(&fixture->source_5v_3a, true);
	tcpci_partner_send_control_msg(&fixture->source_5v_3a,
				       PD_CTRL_GET_STATUS, 0);
	k_sleep(K_SECONDS(2));
	tcpci_partner_common_enable_pd_logging(&fixture->source_5v_3a, false);

	/* When a partner supplies a Revision of PD r3.1 or greater, the TCPM
	 * should respond to Get_Status with a 7-byte Status Data Block, as
	 * defined in PD r3.1.
	 */
	zassert_equal(get_status_data_size(&fixture->source_5v_3a.msg_log), 7);
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3,
	test_get_status_response_pd_no_revision)
{
	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	/* Set the partner to respond to Get_Revision with Not_Supported */
	fixture->source_5v_3a.rmdo = 0x0;
	connect_source_to_port(&fixture->source_5v_3a, &fixture->src_ext, 1,
			       fixture->tcpci_emul, fixture->charger_emul);

	tcpci_partner_common_enable_pd_logging(&fixture->source_5v_3a, true);
	tcpci_partner_send_control_msg(&fixture->source_5v_3a,
				       PD_CTRL_GET_STATUS, 0);
	k_sleep(K_SECONDS(2));
	tcpci_partner_common_enable_pd_logging(&fixture->source_5v_3a, false);

	/* When a partner does not supply a Revision message, the TCPM should
	 * respond to Get_Status with a 7-byte Status Data Block, as defined in
	 * PD r3.1.
	 */
	zassert_equal(get_status_data_size(&fixture->source_5v_3a.msg_log), 7);
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3, test_give_sink_cap_ext)
{
	memset(&fixture->source_5v_3a.skedb, 0, sizeof(struct skedb));

	tcpci_partner_send_control_msg(&fixture->source_5v_3a,
				       PD_CTRL_GET_SINK_CAP_EXT, 0);
	k_sleep(K_SECONDS(2));
	/* check some fields to verify that Sink_Capabilities_Extended is
	 * received */
	zassert_equal(fixture->source_5v_3a.skedb.vid, USB_VID_GOOGLE);
	zassert_equal(fixture->source_5v_3a.skedb.pid, CONFIG_USB_PID);
	zassert_equal(fixture->source_5v_3a.skedb.battery_info, 1);
	zassert_equal(fixture->source_5v_3a.skedb.sink_modes,
		      SKEDB_SINK_VBUS_POWERED | SKEDB_SINK_BATTERY_POWERED);
	zassert_equal(fixture->source_5v_3a.skedb.sink_minimum_pdp, 15);
	zassert_equal(fixture->source_5v_3a.skedb.sink_maximum_pdp, 60);
}

ZTEST_F(usb_attach_5v_3a_pd_source_rev3, test_give_source_info)
{
	const union sido expected_sido = {
		.port_type = 0,
		.port_maximum_pdp = CONFIG_USB_PD_3A_PORTS > 0 ? 15 : 7,
		.port_present_pdp = CONFIG_USB_PD_3A_PORTS > 0 ? 15 : 7,
		.port_reported_pdp = 7,
	};

	tcpci_partner_send_control_msg(&fixture->source_5v_3a,
				       PD_CTRL_GET_SOURCE_INFO, 0);
	k_sleep(K_SECONDS(2));

	const union sido *actual_sido = &fixture->source_5v_3a.tcpm_sido;
	zexpect_equal(actual_sido->port_type, expected_sido.port_type,
		      "Unexpected port type %u", actual_sido->port_type);
	zexpect_equal(actual_sido->port_maximum_pdp,
		      expected_sido.port_maximum_pdp,
		      "Unexpected maximum PDP %u",
		      actual_sido->port_maximum_pdp);
	zexpect_equal(actual_sido->port_present_pdp,
		      expected_sido.port_present_pdp,
		      "Unexpected present PDP %u",
		      actual_sido->port_present_pdp);
	zexpect_equal(actual_sido->port_reported_pdp,
		      expected_sido.port_reported_pdp,
		      "Unexpected reported PDP %u",
		      actual_sido->port_reported_pdp);
}

struct usb_attach_pd_sink_rev3_fixture {
	struct tcpci_partner_data sink;
	struct tcpci_snk_emul_data snk_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void *usb_attach_pd_sink_setup(void)
{
	static struct usb_attach_pd_sink_rev3_fixture test_fixture;

	/* Get references for the emulators */
	test_fixture.tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	test_fixture.charger_emul = EMUL_GET_USBC_BINDING(0, chg);

	/* Initialized the charger to supply 5V and 3A */
	tcpci_partner_init(&test_fixture.sink, PD_REV30);
	test_fixture.sink.extensions = tcpci_snk_emul_init(
		&test_fixture.snk_ext, &test_fixture.sink, NULL);
	test_fixture.snk_ext.pdo[1] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &test_fixture;
}

static void usb_attach_pd_sink_before(void *data)
{
	struct usb_attach_pd_sink_rev3_fixture *fixture = data;

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	/* Set the partner's USB PD Revision to 3.1 */
	fixture->sink.rmdo = 0x31000000;

	connect_sink_to_port(&fixture->sink, fixture->tcpci_emul,
			     fixture->charger_emul);

	/* Clear Alert and Status receive checks; clear message log */
	tcpci_snk_emul_clear_alert_received(&fixture->snk_ext);
	zassert_false(fixture->snk_ext.alert_received);
	tcpci_partner_common_clear_logged_msgs(&fixture->sink);

	/* Initial check on power state */
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
}

static void usb_attach_pd_sink_after(void *data)
{
	struct usb_attach_pd_sink_rev3_fixture *fixture = data;

	disconnect_sink_from_port(fixture->tcpci_emul);
}

ZTEST_SUITE(usb_attach_pd_sink_rev3, drivers_predicate_post_main,
	    usb_attach_pd_sink_setup, usb_attach_pd_sink_before,
	    usb_attach_pd_sink_after, NULL);

ZTEST_F(usb_attach_pd_sink_rev3, test_give_source_info)
{
	const union sido expected_sido = {
		.port_type = 0,
		.port_maximum_pdp = CONFIG_USB_PD_3A_PORTS > 0 ? 15 : 7,
		.port_present_pdp = CONFIG_USB_PD_3A_PORTS > 0 ? 15 : 7,
		.port_reported_pdp = 7,
	};

	tcpci_partner_send_control_msg(&fixture->sink, PD_CTRL_GET_SOURCE_INFO,
				       0);
	k_sleep(K_SECONDS(2));

	const union sido *actual_sido = &fixture->sink.tcpm_sido;
	zexpect_equal(actual_sido->port_type, expected_sido.port_type,
		      "Unexpected port type %u", actual_sido->port_type);
	zexpect_equal(actual_sido->port_maximum_pdp,
		      expected_sido.port_maximum_pdp,
		      "Unexpected maximum PDP %u",
		      actual_sido->port_maximum_pdp);
	zexpect_equal(actual_sido->port_present_pdp,
		      expected_sido.port_present_pdp,
		      "Unexpected present PDP %u",
		      actual_sido->port_present_pdp);
	zexpect_equal(actual_sido->port_reported_pdp,
		      expected_sido.port_reported_pdp,
		      "Unexpected reported PDP %u",
		      actual_sido->port_reported_pdp);
}
