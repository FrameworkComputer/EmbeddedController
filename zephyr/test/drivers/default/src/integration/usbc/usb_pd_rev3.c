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
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "hooks.h"
#include "host_command.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "util.h"

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/slist.h>
#include <zephyr/ztest.h>

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

	/* Set the partner's USB PD Revision to 3.1 */
	test_fixture.source_5v_3a.rmdo = 0x31000000;

	return &test_fixture;
}

static void usb_attach_5v_3a_pd_source_before(void *data)
{
	struct usb_attach_5v_3a_pd_source_rev3_fixture *fixture = data;

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	connect_source_to_port(&fixture->source_5v_3a, &fixture->src_ext, 1,
			       fixture->tcpci_emul, fixture->charger_emul);

	/* Clear Alert and Status receive checks */
	tcpci_src_emul_clear_alert_received(&fixture->src_ext);
	tcpci_src_emul_clear_status_received(&fixture->src_ext);
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);

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
	bool header_mismatch = false;
	enum tcpci_partner_msg_sender expected_senders[3] = {
		TCPCI_PARTNER_SENDER_PARTNER, TCPCI_PARTNER_SENDER_TCPM,
		TCPCI_PARTNER_SENDER_TCPM
	};
	uint16_t expected_headers[3] = { 0x0012, 0xb002, 0x1006 };
	struct tcpci_partner_log_msg *msg;

	SYS_SLIST_FOR_EACH_CONTAINER(&fixture->source_5v_3a.msg_log, msg, node)
	{
		uint16_t header = sys_get_le16(msg->buf);

		if (i >= 3)
			break;

		if (msg->sender != expected_senders[i] ||
		    PD_HEADER_EXT(header) !=
			    PD_HEADER_EXT(expected_headers[i]) ||
		    PD_HEADER_CNT(header) !=
			    PD_HEADER_CNT(expected_headers[i]) ||
		    PD_HEADER_TYPE(header) !=
			    PD_HEADER_TYPE(expected_headers[i])) {
			header_mismatch = true;
			break;
		}

		i++;
	}

	zassert_false(header_mismatch);
	zassert_true(fixture->src_ext.alert_received);
	zassert_true(fixture->src_ext.status_received);
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

	tcpci_partner_send_control_msg(&fixture->source_5v_3a,
				       PD_CTRL_GET_REVISION, 0);
	k_sleep(K_SECONDS(2));
	zassert_equal(fixture->source_5v_3a.rmdo, expected_rev,
		      "Expected RMDO %x, got %x", expected_rev,
		      fixture->source_5v_3a.rmdo);
}
