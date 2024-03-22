/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_dp_alt_mode.h"
#include "usb_mux.h"
#include "usb_pd_vdo.h"

#include <stdint.h>

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <gpio.h>

#define TEST_PORT USBC_PORT_C0

struct ap_vdm_control_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_src_emul_data src_ext;
};

struct tcpci_cable_data passive_usb3 = {
	.identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0),
	.identity_vdm[VDO_INDEX_IDH] = VDO_IDH(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_PCABLE,
		/* modal operation */ false, USB_VID_GOOGLE),
	.identity_vdm[VDO_INDEX_CSTAT] = 0,
	.identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(0x1234, 0xABCD),
	.identity_vdm[VDO_INDEX_PTYPE_CABLE1] =
		VDO_REV30_PASSIVE(USB_R30_SS_U32_U40_GEN2, USB_VBUS_CUR_3A,
				  USB_REV30_LATENCY_1m, USB_REV30_TYPE_C),
	.identity_vdos = VDO_INDEX_PTYPE_CABLE1 + 1,

};

static void add_dp_discovery(struct tcpci_partner_data *partner)
{
	/* Add Discover Identity response */
	partner->identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	partner->identity_vdm[VDO_INDEX_IDH] = VDO_IDH(
		/* USB host */ false, /* USB device */ true, IDH_PTYPE_HUB,
		/* modal operation */ true, USB_VID_GOOGLE);
	partner->identity_vdm[VDO_INDEX_CSTAT] = 0;
	partner->identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(0x1234, 0x5678);
	/* Hardware version 1, firmware version 2 */
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] = VDO_UFP1(
		(VDO_UFP1_CAPABILITY_USB20 | VDO_UFP1_CAPABILITY_USB32),
		USB_TYPEC_RECEPTACLE, VDO_UFP1_ALT_MODE_RECONFIGURE,
		USB_R30_SS_U32_U40_GEN2);
	partner->identity_vdos = VDO_INDEX_PTYPE_UFP1_VDO + 1;

	/* Add Discover Modes response */
	/* Support one mode for DisplayPort VID.*/
	partner->modes_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_MODES) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	partner->modes_vdm[VDO_INDEX_HDR + 1] =
		VDO_MODE_DP(MODE_DP_PIN_C | MODE_DP_PIN_D, 0, 1,
			    CABLE_RECEPTACLE, MODE_DP_V13, MODE_DP_SNK);
	partner->modes_vdos = VDO_INDEX_HDR + 2;

	/* Add Discover SVIDs response */
	/* Support DisplayPort VID. */
	partner->svids_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_SVID) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	partner->svids_vdm[VDO_INDEX_HDR + 1] =
		VDO_SVID(USB_SID_DISPLAYPORT, 0);
	partner->svids_vdos = VDO_INDEX_HDR + 2;
}

static void add_displayport_mode_responses(struct tcpci_partner_data *partner)
{
	/* Add DisplayPort EnterMode response */
	partner->enter_mode_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_ENTER_MODE) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	partner->enter_mode_vdos = VDO_INDEX_HDR + 1;

	/* Add DisplayPort StatusUpdate response */
	partner->dp_status_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DP_STATUS) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	partner->dp_status_vdm[VDO_INDEX_HDR + 1] =
		VDO_DP_STATUS(0, /* IRQ_HPD */
			      false, /* HPD_HI|LOW - Changed*/
			      0, /* request exit DP */
			      0, /* request exit USB */
			      1, /* MF pref */
			      true, /* DP Enabled */
			      0, /* power low e.g. normal */
			      0x2 /* Connected as Sink */);
	partner->dp_status_vdos = VDO_INDEX_HDR + 2;

	/* Add DisplayPort Configure Response */
	partner->dp_config_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DP_CONFIG) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	partner->dp_config_vdos = VDO_INDEX_HDR + 1;
}

static void verify_vdm_req(struct ap_vdm_control_fixture *fixture,
			   struct typec_vdm_req *req)
{
	struct tcpci_partner_log_msg *msg;
	bool message_seen = 0;

	SYS_SLIST_FOR_EACH_CONTAINER(&fixture->partner.msg_log, msg, node)
	{
		uint16_t header = sys_get_le16(msg->buf);

		/* Ignore messages from ourselves */
		if (msg->sender == TCPCI_PARTNER_SENDER_PARTNER)
			continue;

		/*
		 * Control messages, non-VDMs, and extended messages are not of
		 * interest
		 */
		if ((PD_HEADER_CNT(header) == 0) ||
		    (PD_HEADER_TYPE(header) != PD_DATA_VENDOR_DEF) ||
		    (PD_HEADER_EXT(header) != 0)) {
			continue;
		}

		if (req->partner_type != msg->sop)
			continue;

		/* We have a VDM, check entry we're interested in */
		if (memcmp(msg->buf + 2, req->vdm_data,
			   req->vdm_data_objects * sizeof(uint32_t)) == 0) {
			message_seen = true;
			break;
		}
	}

	zassert_true(message_seen, "Expected message not found");
}

static void verify_no_vdms(struct ap_vdm_control_fixture *fixture)
{
	struct tcpci_partner_log_msg *msg;

	/* LCOV_EXCL_START */
	/*
	 * Code is not expected to be reached, but this check is
	 * written to be tolerant of unrelated messages coming through
	 * during the test run to avoid needlessly brittle test code.
	 */
	SYS_SLIST_FOR_EACH_CONTAINER(&fixture->partner.msg_log, msg, node)
	{
		uint16_t header = sys_get_le16(msg->buf);

		/* Ignore messages from ourselves */
		if (msg->sender == TCPCI_PARTNER_SENDER_PARTNER)
			continue;

		/*
		 * Control messages, non-VDMs, and extended messages are not of
		 * interest
		 */
		if ((PD_HEADER_CNT(header) == 0) ||
		    (PD_HEADER_TYPE(header) != PD_DATA_VENDOR_DEF) ||
		    (PD_HEADER_EXT(header) != 0)) {
			continue;
		}

		zassert_unreachable();
	}
	/* LCOV_EXCL_STOP */
}

static void *ap_vdm_control_setup(void)
{
	static struct ap_vdm_control_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_src_emul_data *src_ext = &fixture.src_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_src_emul_init(src_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul));
	fixture.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul));

	return &fixture;
}

static void ap_vdm_control_before(void *data)
{
	struct ap_vdm_control_fixture *fix = data;
	struct tcpci_partner_data *partner = &fix->partner;

	/* Set chipset on so the "AP" is on to give us commands */
	test_set_chipset_to_s0();

	/* Set up the partner as DP-capable with a passive cable */
	add_dp_discovery(partner);
	partner->cable = &passive_usb3;
	add_displayport_mode_responses(partner);

	/* Connect our port partner */
	connect_source_to_port(&fix->partner, &fix->src_ext, 0, fix->tcpci_emul,
			       fix->charger_emul);
}

static void ap_vdm_control_after(void *data)
{
	struct ap_vdm_control_fixture *fix = data;

	disconnect_source_from_port(fix->tcpci_emul, fix->charger_emul);
	tcpci_partner_common_clear_logged_msgs(&fix->partner);
}

ZTEST_SUITE(ap_vdm_control, drivers_predicate_post_main, ap_vdm_control_setup,
	    ap_vdm_control_before, ap_vdm_control_after, NULL);

ZTEST_F(ap_vdm_control, test_feature_present)
{
	struct ec_response_get_features feat = host_cmd_get_features();

	zassert_true(feat.flags[1] &
			     EC_FEATURE_MASK_1(EC_FEATURE_TYPEC_AP_VDM_SEND),
		     "Failed to see feature present");
}

/* TYPEC_CONTROL_COMMAND_SEND_VDM_REQ tests */
ZTEST_F(ap_vdm_control, test_send_vdm_req_bad_port)
{
	struct ec_params_typec_control params = {
		.port = 85,
		.command = TYPEC_CONTROL_COMMAND_SEND_VDM_REQ,
		.vdm_req_params = {
			.vdm_data = { 0 },
			.vdm_data_objects = 1,
			.partner_type = TYPEC_PARTNER_SOP,
		},
	};

	zassert_equal(ec_cmd_typec_control(NULL, &params), EC_RES_INVALID_PARAM,
		      "Failed to see port error");
}

ZTEST_F(ap_vdm_control, test_send_vdm_req_bad_type)
{
	struct ec_params_typec_control params = {
		.port = TEST_PORT,
		.command = TYPEC_CONTROL_COMMAND_SEND_VDM_REQ,
		.vdm_req_params = {
			.vdm_data = { 0 },
			.vdm_data_objects = 1,
			.partner_type = TYPEC_PARTNER_SOP_PRIME_PRIME + 1,
		},
	};

	zassert_equal(ec_cmd_typec_control(NULL, &params), EC_RES_INVALID_PARAM,
		      "Failed to see port error");
}

ZTEST_F(ap_vdm_control, test_send_vdm_req_bad_count)
{
	struct ec_params_typec_control params = {
		.port = TEST_PORT,
		.command = TYPEC_CONTROL_COMMAND_SEND_VDM_REQ,
		.vdm_req_params = {
			.vdm_data = { 0 },
			.vdm_data_objects = 0,
			.partner_type = TYPEC_PARTNER_SOP,
		},
	};

	zassert_equal(ec_cmd_typec_control(NULL, &params), EC_RES_INVALID_PARAM,
		      "Failed to see port error");
}

ZTEST_F(ap_vdm_control, test_send_vdm_sop_req_valid)
{
	uint32_t vdm_req_header = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT) |
				  VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	struct typec_vdm_req req = {
		.vdm_data = { vdm_req_header },
		.vdm_data_objects = 1,
		.partner_type = TYPEC_PARTNER_SOP,
	};

	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_vdm_req(TEST_PORT, req);
	k_sleep(K_SECONDS(1));

	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);

	/* Look for our REQ */
	verify_vdm_req(fixture, &req);
}

ZTEST_F(ap_vdm_control, test_send_vdm_sop_prime_req_valid)
{
	uint32_t vdm_req_header = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT) |
				  VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	struct typec_vdm_req req = {
		.vdm_data = { vdm_req_header },
		.vdm_data_objects = 1,
		.partner_type = TYPEC_PARTNER_SOP_PRIME,
	};

	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_vdm_req(TEST_PORT, req);
	k_sleep(K_SECONDS(1));

	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);

	/* Look for our REQ */
	verify_vdm_req(fixture, &req);
}

ZTEST_F(ap_vdm_control, test_send_vdm_sop_attention_bad)
{
	uint32_t vdm_req_header = VDO(USB_SID_DISPLAYPORT, 1, CMD_ATTENTION) |
				  VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	struct ec_params_typec_control params = {
		.port = TEST_PORT,
		.command = TYPEC_CONTROL_COMMAND_SEND_VDM_REQ,
		.vdm_req_params = {
			.vdm_data = { vdm_req_header },
			.vdm_data_objects = 5,
			.partner_type = TYPEC_PARTNER_SOP,
		},
	};

	zassert_equal(ec_cmd_typec_control(NULL, &params), EC_RES_INVALID_PARAM,
		      "Failed to see port error");
}

ZTEST_F(ap_vdm_control, test_send_vdm_req_in_progress)
{
	uint32_t vdm_req_header = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT) |
				  VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	struct ec_params_typec_control params = {
		.port = TEST_PORT,
		.command = TYPEC_CONTROL_COMMAND_SEND_VDM_REQ,
		.vdm_req_params = {
			.vdm_data = { vdm_req_header },
			.vdm_data_objects = 1,
			.partner_type = TYPEC_PARTNER_SOP,
		},
	};

	/*
	 * First command should succeed, but given no time to process the second
	 * should return busy
	 */
	zassert_equal(ec_cmd_typec_control(NULL, &params), EC_RES_SUCCESS,
		      "Failed to send successful request");
	zassert_equal(ec_cmd_typec_control(NULL, &params), EC_RES_BUSY,
		      "Failed to see busy");
}

/* EC_CMD_TYPEC_VDM_RESPONSE tests */
ZTEST_F(ap_vdm_control, test_vdm_response_ack)
{
	struct ec_response_typec_status status;
	struct ec_response_typec_vdm_response vdm_resp;
	uint32_t vdm_req_header = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT) |
				  VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	struct typec_vdm_req req = {
		.vdm_data = { vdm_req_header },
		.vdm_data_objects = 1,
		.partner_type = TYPEC_PARTNER_SOP,
	};

	host_cmd_typec_control_vdm_req(TEST_PORT, req);
	k_sleep(K_SECONDS(1));

	/* Look for our notification and reply */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_true(status.events & PD_STATUS_EVENT_VDM_REQ_REPLY,
		     "Failed to see VDM ACK event");

	vdm_resp = host_cmd_typec_vdm_response(TEST_PORT);
	zassert_equal(vdm_resp.vdm_response_err, EC_RES_SUCCESS);
	zassert_equal(vdm_resp.partner_type, req.partner_type,
		      "Failed to see correct partner");
	zassert_equal(vdm_resp.vdm_data_objects, fixture->partner.identity_vdos,
		      "Failed to see correct VDO num");
	zassert_equal(memcmp(vdm_resp.vdm_response,
			     fixture->partner.identity_vdm,
			     vdm_resp.vdm_data_objects * sizeof(uint32_t)),
		      0, "Failed to see correct VDM contents");
}

ZTEST_F(ap_vdm_control, test_vdm_request_nak)
{
	struct ec_response_typec_status status;
	struct ec_response_typec_vdm_response vdm_resp;
	uint32_t vdm_req_header = VDO(USB_SID_DISPLAYPORT, 1, CMD_ENTER_MODE) |
				  VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	struct typec_vdm_req req = {
		.vdm_data = { vdm_req_header },
		.vdm_data_objects = 1,
		.partner_type = TYPEC_PARTNER_SOP,
	};

	/* Add DisplayPort EnterMode NAK */
	fixture->partner.enter_mode_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_NAK) | CMD_ENTER_MODE) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	fixture->partner.enter_mode_vdos = VDO_INDEX_HDR + 1;

	host_cmd_typec_control_vdm_req(TEST_PORT, req);
	k_sleep(K_SECONDS(1));

	/* Look for our notification and reply */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_true(status.events & PD_STATUS_EVENT_VDM_REQ_REPLY,
		     "Failed to see VDM NAK event");

	vdm_resp = host_cmd_typec_vdm_response(TEST_PORT);
	zassert_equal(vdm_resp.vdm_response_err, EC_RES_SUCCESS);
	zassert_equal(vdm_resp.partner_type, req.partner_type,
		      "Failed to see correct partner");
	zassert_equal(vdm_resp.vdm_data_objects,
		      fixture->partner.enter_mode_vdos,
		      "Failed to see correct VDO num");
	zassert_equal(memcmp(vdm_resp.vdm_response,
			     fixture->partner.enter_mode_vdm,
			     vdm_resp.vdm_data_objects * sizeof(uint32_t)),
		      0, "Failed to see correct VDM contents");
}

ZTEST_F(ap_vdm_control, test_vdm_request_failed)
{
	struct ec_response_typec_status status;
	struct ec_response_typec_vdm_response vdm_resp;

	uint32_t vdm_req_header = VDO(USB_SID_DISPLAYPORT, 1, CMD_ENTER_MODE) |
				  VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	struct typec_vdm_req req = {
		.vdm_data = { vdm_req_header },
		.vdm_data_objects = 1,
		.partner_type = TYPEC_PARTNER_SOP,
	};

	/* Do not advertise an EnterMode response */
	fixture->partner.enter_mode_vdos = 0;

	host_cmd_typec_control_vdm_req(TEST_PORT, req);
	k_sleep(K_SECONDS(1));

	/* Look for our notification and lack of reply */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_true(status.events & PD_STATUS_EVENT_VDM_REQ_FAILED,
		     "Failed to see notice of no reply");

	vdm_resp = host_cmd_typec_vdm_response(TEST_PORT);
	zassert_equal(vdm_resp.vdm_response_err, EC_RES_UNAVAILABLE,
		      "Failed to get unavailable");
}

ZTEST_F(ap_vdm_control, test_vdm_request_bad_port)
{
	struct ec_response_typec_vdm_response vdm_resp;
	struct ec_params_typec_vdm_response params = { .port = 88 };

	zassert_equal(ec_cmd_typec_vdm_response(NULL, &params, &vdm_resp),
		      EC_RES_INVALID_PARAM, "Failed to see bad port");
}

ZTEST_F(ap_vdm_control, test_vdm_request_in_progress)
{
	struct ec_response_typec_vdm_response vdm_resp;

	uint32_t vdm_req_header = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT) |
				  VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	struct typec_vdm_req req = {
		.vdm_data = { vdm_req_header },
		.vdm_data_objects = 1,
		.partner_type = TYPEC_PARTNER_SOP,
	};

	host_cmd_typec_control_vdm_req(TEST_PORT, req);

	/* Give no processing time and immediately ask for our result */
	vdm_resp = host_cmd_typec_vdm_response(TEST_PORT);
	zassert_equal(vdm_resp.vdm_response_err, EC_RES_BUSY,
		      "Failed to get busy");
}

ZTEST_F(ap_vdm_control, test_vdm_request_no_send)
{
	struct ec_response_typec_vdm_response vdm_resp;

	/* Check for an error on a fresh connection with no VDM REQ sent */
	vdm_resp = host_cmd_typec_vdm_response(TEST_PORT);
	zassert_equal(vdm_resp.vdm_response_err, EC_RES_UNAVAILABLE,
		      "Failed to see no message ready");
}

ZTEST_F(ap_vdm_control, test_vdm_response_disconnect_clear)
{
	struct ec_response_typec_vdm_response vdm_resp;
	uint32_t vdm_req_header = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT) |
				  VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	struct typec_vdm_req req = {
		.vdm_data = { vdm_req_header },
		.vdm_data_objects = 1,
		.partner_type = TYPEC_PARTNER_SOP,
	};

	host_cmd_typec_control_vdm_req(TEST_PORT, req);
	k_sleep(K_SECONDS(1));

	/* Now disconnect and verify there's nothing to see here */
	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);

	vdm_resp = host_cmd_typec_vdm_response(TEST_PORT);
	zassert_equal(vdm_resp.vdm_response_err, EC_RES_UNAVAILABLE,
		      "Failed to see reply cleared");
	zassert_equal(vdm_resp.vdm_data_objects, 0,
		      "Failed to see no VDOs available");
}

/* Tests for the DP entry flow and related requirements */
static void verify_expected_reply(enum typec_partner_type type,
				  int expected_num_objects, uint32_t *contents)
{
	struct ec_response_typec_status status;
	struct ec_response_typec_vdm_response vdm_resp;

	status = host_cmd_typec_status(TEST_PORT);
	zassert_true(status.events & PD_STATUS_EVENT_VDM_REQ_REPLY,
		     "Failed to see VDM ACK event");

	vdm_resp = host_cmd_typec_vdm_response(TEST_PORT);
	zassert_equal(vdm_resp.partner_type, type,
		      "Failed to see correct partner");
	zassert_equal(vdm_resp.vdm_data_objects, expected_num_objects,
		      "Failed to see correct number of objects");
	zassert_equal(memcmp(vdm_resp.vdm_response, contents,
			     expected_num_objects * sizeof(uint32_t)),
		      0, "Failed to see correct VDM contents");
}

static void run_verify_dp_entry(struct ap_vdm_control_fixture *fixture,
				int opos)
{
	/*
	 * Test the full flow of DP entry and configure, to set up for
	 * further test cases.
	 */
	struct typec_vdm_req req = {
		.vdm_data = { VDO(USB_SID_DISPLAYPORT, 1,
				  CMD_ENTER_MODE | VDO_OPOS(opos)) |
			      VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0) },
		.vdm_data_objects = 1,
		.partner_type = TYPEC_PARTNER_SOP,
	};

	/* Step 1: EnterMode */
	host_cmd_typec_control_vdm_req(TEST_PORT, req);
	k_sleep(K_MSEC(100));

	verify_expected_reply(req.partner_type,
			      fixture->partner.enter_mode_vdos,
			      fixture->partner.enter_mode_vdm);

	/* Step 2: DP Status */
	req.vdm_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_STATUS | VDO_OPOS(opos)) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	req.vdm_data[1] = VDO_DP_STATUS(0, /* HPD IRQ  ... not applicable */
					0, /* HPD level ... not applicable */
					0, /* exit DP? ... no */
					0, /* usb mode? ... no */
					0, /* multi-function ... no */
					0, /* currently enabled ... no */
					0, /* power low? ... no */
					1 /* DP source connected */);
	req.vdm_data_objects = 2;
	req.partner_type = TYPEC_PARTNER_SOP;

	host_cmd_typec_control_vdm_req(TEST_PORT, req);
	k_sleep(K_MSEC(100));

	verify_expected_reply(req.partner_type, fixture->partner.dp_status_vdos,
			      fixture->partner.dp_status_vdm);

	/* Step 3: DP Configure */
	req.vdm_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_CONFIG | VDO_OPOS(opos)) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	req.vdm_data[1] = VDO_DP_CFG(MODE_DP_PIN_D, /* pin mode */
				     1, /* DPv1.3 signaling */
				     2); /* Set that partner should be DP sink
					  */
	req.vdm_data_objects = 2;
	req.partner_type = TYPEC_PARTNER_SOP;

	host_cmd_typec_control_vdm_req(TEST_PORT, req);
	k_sleep(K_MSEC(100));

	verify_expected_reply(req.partner_type, fixture->partner.dp_config_vdos,
			      fixture->partner.dp_config_vdm);
}

ZTEST_F(ap_vdm_control, test_vdm_attention_none)
{
	struct ec_response_typec_vdm_response vdm_resp;
	int opos = 1;

	run_verify_dp_entry(fixture, opos);

	/* Check that we have no Attention messages and none in the queue */
	vdm_resp = host_cmd_typec_vdm_response(TEST_PORT);
	zassert_equal(vdm_resp.vdm_attention_objects, 0,
		      "Failed to see empty message");
	zassert_equal(vdm_resp.vdm_attention_left, 0,
		      "Failed to see no more messages");
}

ZTEST_F(ap_vdm_control, test_vdm_attention_one)
{
	uint32_t vdm_attention_data[2];
	int opos = 1;
	struct ec_response_typec_status status;
	struct ec_response_typec_vdm_response vdm_resp;

	run_verify_dp_entry(fixture, opos);

	/* Test that we see our Attention message */
	vdm_attention_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1,
		    VDO_OPOS(opos) | VDO_CMDT(CMDT_INIT) | CMD_ATTENTION) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	vdm_attention_data[1] = VDO_DP_STATUS(1, /* IRQ_HPD */
					      true, /* HPD_HI|LOW - Changed*/
					      0, /* request exit DP */
					      0, /* request exit USB */
					      0, /* MF pref */
					      true, /* DP Enabled */
					      0, /* power low e.g. normal */
					      0x2 /* Connected as Sink */);
	tcpci_partner_send_data_msg(&fixture->partner, PD_DATA_VENDOR_DEF,
				    vdm_attention_data, 2, 0);

	k_sleep(K_MSEC(100));
	/*
	 * Verify the event and the contents of our Attention
	 */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_true(status.events & PD_STATUS_EVENT_VDM_ATTENTION,
		     "Failed to see VDM Attention event");

	vdm_resp = host_cmd_typec_vdm_response(TEST_PORT);
	zassert_equal(vdm_resp.vdm_attention_objects, 2,
		      "Failed to see correct number of objects");
	zassert_equal(vdm_resp.vdm_attention_left, 0,
		      "Failed to see 0 more in queue");
	zassert_equal(memcmp(vdm_resp.vdm_attention, vdm_attention_data,
			     vdm_resp.vdm_attention_objects * sizeof(uint32_t)),
		      0, "Failed to see correct Attention VDM contents");
}

ZTEST_F(ap_vdm_control, test_vdm_attention_two)
{
	uint32_t vdm_attention_first[2];
	uint32_t vdm_attention_second[2];
	int opos = 1;
	struct ec_response_typec_status status;
	struct ec_response_typec_vdm_response vdm_resp;

	run_verify_dp_entry(fixture, opos);

	/* Test that we see our first Attention message followed by second */
	vdm_attention_first[0] =
		VDO(USB_SID_DISPLAYPORT, 1,
		    VDO_OPOS(opos) | VDO_CMDT(CMDT_INIT) | CMD_ATTENTION) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	vdm_attention_first[1] = VDO_DP_STATUS(0, /* IRQ_HPD */
					       false, /* HPD_HI|LOW - Changed*/
					       0, /* request exit DP */
					       0, /* request exit USB */
					       0, /* MF pref */
					       true, /* DP Enabled */
					       0, /* power low e.g. normal */
					       0x2 /* Connected as Sink */);
	tcpci_partner_send_data_msg(&fixture->partner, PD_DATA_VENDOR_DEF,
				    vdm_attention_first, 2, 0);

	k_sleep(K_MSEC(100));

	/* Number two time */
	vdm_attention_second[0] =
		VDO(USB_SID_DISPLAYPORT, 1,
		    VDO_OPOS(opos) | VDO_CMDT(CMDT_INIT) | CMD_ATTENTION) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	vdm_attention_second[1] = VDO_DP_STATUS(1, /* IRQ_HPD */
						true, /* HPD_HI|LOW - Changed*/
						0, /* request exit DP */
						0, /* request exit USB */
						0, /* MF pref */
						true, /* DP Enabled */
						0, /* power low e.g. normal */
						0x2 /* Connected as Sink */);
	tcpci_partner_send_data_msg(&fixture->partner, PD_DATA_VENDOR_DEF,
				    vdm_attention_second, 2, 0);

	k_sleep(K_MSEC(100));
	/*
	 * Verify the event and the contents of our Attention from each in
	 * the proper order
	 */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_true(status.events & PD_STATUS_EVENT_VDM_ATTENTION,
		     "Failed to see VDM Attention event");

	vdm_resp = host_cmd_typec_vdm_response(TEST_PORT);
	zassert_equal(vdm_resp.vdm_attention_objects, 2,
		      "Failed to see correct number of objects");
	zassert_equal(vdm_resp.vdm_attention_left, 1,
		      "Failed to see 1 more in queue");
	zassert_equal(memcmp(vdm_resp.vdm_attention, vdm_attention_first,
			     vdm_resp.vdm_attention_objects * sizeof(uint32_t)),
		      0, "Failed to see correct first Attention VDM contents");

	vdm_resp = host_cmd_typec_vdm_response(TEST_PORT);
	zassert_equal(vdm_resp.vdm_attention_objects, 2,
		      "Failed to see correct number of objects");
	zassert_equal(vdm_resp.vdm_attention_left, 0,
		      "Failed to see 0 more in queue");
	zassert_equal(memcmp(vdm_resp.vdm_attention, vdm_attention_second,
			     vdm_resp.vdm_attention_objects * sizeof(uint32_t)),
		      0, "Failed to see correct second Attention VDM contents");
}

ZTEST_F(ap_vdm_control, test_vdm_attention_disconnect_clear)
{
	uint32_t vdm_attention_data[2];
	int opos = 1;
	struct ec_response_typec_vdm_response vdm_resp;

	run_verify_dp_entry(fixture, opos);

	/* Send an Attention message */
	vdm_attention_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1,
		    VDO_OPOS(opos) | VDO_CMDT(CMDT_INIT) | CMD_ATTENTION);
	vdm_attention_data[0] |= VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	vdm_attention_data[1] = VDO_DP_STATUS(1, /* IRQ_HPD */
					      true, /* HPD_HI|LOW - Changed*/
					      0, /* request exit DP */
					      0, /* request exit USB */
					      0, /* MF pref */
					      true, /* DP Enabled */
					      0, /* power low e.g. normal */
					      0x2 /* Connected as Sink */);
	tcpci_partner_send_data_msg(&fixture->partner, PD_DATA_VENDOR_DEF,
				    vdm_attention_data, 2, 0);

	k_sleep(K_SECONDS(1));
	/*
	 * Disconnect and verify no messages are reported
	 */
	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);

	vdm_resp = host_cmd_typec_vdm_response(TEST_PORT);
	zassert_equal(vdm_resp.vdm_attention_objects, 0,
		      "Failed to see empty message");
	zassert_equal(vdm_resp.vdm_attention_left, 0,
		      "Failed to see no more messages");
}

ZTEST_F(ap_vdm_control, test_no_ec_dp_enter)
{
	struct ec_params_typec_control params = {
		.port = TEST_PORT,
		.command = TYPEC_CONTROL_COMMAND_ENTER_MODE,
		.mode_to_enter = TYPEC_MODE_DP,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	/*
	 * Confirm that the EC doesn't try to send EnterMode messages for DP on
	 * its own through the EC DPM logic
	 */
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);
	k_sleep(K_SECONDS(1));

	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);

	verify_no_vdms(fixture);
}

ZTEST_F(ap_vdm_control, test_no_ec_dp_exit)
{
	/*
	 * Confirm that the EC won't try to exit DP mode on its own through the
	 * EC's DPM logic
	 */
	run_verify_dp_entry(fixture, 1);

	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_exit_modes(TEST_PORT);
	k_sleep(K_SECONDS(1));

	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);

	verify_no_vdms(fixture);
}

ZTEST_F(ap_vdm_control, test_dp_stub_returns)
{
	int temp;
	uint32_t data[2];

	/*
	 * Confirm that the DP stubs return what we expect them to without
	 * the EC running its DP module
	 */
	run_verify_dp_entry(fixture, 1);

	zassert_false(dp_is_active(TEST_PORT));
	zassert_true(dp_is_idle(TEST_PORT));
	zassert_false(dp_entry_is_done(TEST_PORT));
	zassert_equal(dp_setup_next_vdm(TEST_PORT, &temp, data),
		      MSG_SETUP_ERROR);
}

ZTEST_F(ap_vdm_control, test_no_ec_dp_mode)
{
	struct ec_response_typec_status status;
	struct ec_response_usb_pd_control_v2 legacy_status;
	struct ec_params_usb_pd_control params = {
		.port = TEST_PORT,
		.role = USB_PD_CTRL_ROLE_NO_CHANGE,
		.mux = USB_PD_CTRL_MUX_NO_CHANGE,
		.swap = USB_PD_CTRL_SWAP_NONE
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_USB_PD_CONTROL, 2, legacy_status, params);

	/*
	 * Confirm that neither old nor new APIs see the EC selecting a DP pin
	 * mode
	 */
	run_verify_dp_entry(fixture, 1);

	zassert_ok(host_command_process(&args));
	zassert_equal(legacy_status.dp_mode, 0);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal(status.dp_pin, 0);
}

ZTEST_F(ap_vdm_control, test_vdm_hpd_level)
{
	uint32_t vdm_attention_data[2];
	int opos = 1;
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_hpd);

	/* HPD GPIO should be low before the test */
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 0);

	run_verify_dp_entry(fixture, opos);

	/* Now send Attention to change HPD */
	vdm_attention_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1,
		    VDO_OPOS(opos) | VDO_CMDT(CMDT_INIT) | CMD_ATTENTION) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	vdm_attention_data[1] = VDO_DP_STATUS(1, /* IRQ_HPD */
					      true, /* HPD_HI|LOW - Changed*/
					      0, /* request exit DP */
					      0, /* request exit USB */
					      0, /* MF pref */
					      true, /* DP Enabled */
					      0, /* power low e.g. normal */
					      0x2 /* Connected as Sink */);
	tcpci_partner_send_data_msg(&fixture->partner, PD_DATA_VENDOR_DEF,
				    vdm_attention_data, 2, 0);

	k_sleep(K_MSEC(100));
	/*
	 * Verify the HPD GPIO is set now
	 */
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);
}

ZTEST_F(ap_vdm_control, test_vdm_hpd_irq_ignored)
{
	uint32_t vdm_attention_data[2];
	int opos = 1;
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_hpd);

	/* HPD GPIO should be low before the test */
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 0);

	run_verify_dp_entry(fixture, opos);

	/* Send our bad Attention message */
	vdm_attention_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1,
		    VDO_OPOS(opos) | VDO_CMDT(CMDT_INIT) | CMD_ATTENTION) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	vdm_attention_data[1] = VDO_DP_STATUS(1, /* IRQ_HPD */
					      false, /* HPD_HI|LOW - Changed*/
					      0, /* request exit DP */
					      0, /* request exit USB */
					      0, /* MF pref */
					      true, /* DP Enabled */
					      0, /* power low e.g. normal */
					      0x2 /* Connected as Sink */);
	tcpci_partner_send_data_msg(&fixture->partner, PD_DATA_VENDOR_DEF,
				    vdm_attention_data, 2, 0);

	k_sleep(K_MSEC(100));
	/*
	 * Verify the HPD IRQ was rejected since HPD is low
	 */
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 0);
}

ZTEST_F(ap_vdm_control, test_vdm_status_hpd)
{
	int opos = 1;
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_hpd);

	/* HPD GPIO should be low before the test */
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 0);

	/* Set up our slightly different DP Status */
	fixture->partner.dp_status_vdm[VDO_INDEX_HDR + 1] =
		VDO_DP_STATUS(0, /* IRQ_HPD */
			      true, /* HPD_HI|LOW - Changed*/
			      0, /* request exit DP */
			      0, /* request exit USB */
			      1, /* MF pref */
			      true, /* DP Enabled */
			      0, /* power low e.g. normal */
			      0x2 /* Connected as Sink */);

	/* Run Entry step by step to check HPD at each point */
	struct typec_vdm_req req = {
		.vdm_data = { VDO(USB_SID_DISPLAYPORT, 1,
				  CMD_ENTER_MODE | VDO_OPOS(opos)) |
			      VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0) },
		.vdm_data_objects = 1,
		.partner_type = TYPEC_PARTNER_SOP,
	};

	/* Step 1: EnterMode */
	host_cmd_typec_control_vdm_req(TEST_PORT, req);
	k_sleep(K_MSEC(100));

	verify_expected_reply(req.partner_type,
			      fixture->partner.enter_mode_vdos,
			      fixture->partner.enter_mode_vdm);

	/* Step 2: DP Status */
	req.vdm_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_STATUS | VDO_OPOS(opos)) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	req.vdm_data[1] = VDO_DP_STATUS(0, /* HPD IRQ  ... not applicable */
					0, /* HPD level ... not applicable */
					0, /* exit DP? ... no */
					0, /* usb mode? ... no */
					0, /* multi-function ... no */
					0, /* currently enabled ... no */
					0, /* power low? ... no */
					1 /* DP source connected */);
	req.vdm_data_objects = 2;
	req.partner_type = TYPEC_PARTNER_SOP;

	host_cmd_typec_control_vdm_req(TEST_PORT, req);
	k_sleep(K_MSEC(100));

	verify_expected_reply(req.partner_type, fixture->partner.dp_status_vdos,
			      fixture->partner.dp_status_vdm);
	/* Wait for it... */
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 0);

	/* Step 3: DP Configure */
	req.vdm_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_CONFIG | VDO_OPOS(opos)) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	req.vdm_data[1] = VDO_DP_CFG(MODE_DP_PIN_D, /* pin mode */
				     1, /* DPv1.3 signaling */
				     2); /* Set that partner should be DP sink
					  */
	req.vdm_data_objects = 2;
	req.partner_type = TYPEC_PARTNER_SOP;

	host_cmd_typec_control_vdm_req(TEST_PORT, req);
	k_sleep(K_MSEC(100));

	verify_expected_reply(req.partner_type, fixture->partner.dp_config_vdos,
			      fixture->partner.dp_config_vdm);
	/* Now! */
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);
}

ZTEST_F(ap_vdm_control, test_vdm_hpd_disconnect_clear)
{
	uint32_t vdm_attention_data[2];
	int opos = 1;
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_hpd);

	run_verify_dp_entry(fixture, opos);

	/* Test that we see our Attention message */
	vdm_attention_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1,
		    VDO_OPOS(opos) | VDO_CMDT(CMDT_INIT) | CMD_ATTENTION) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	vdm_attention_data[1] = VDO_DP_STATUS(1, /* IRQ_HPD */
					      true, /* HPD_HI|LOW - Changed*/
					      0, /* request exit DP */
					      0, /* request exit USB */
					      0, /* MF pref */
					      true, /* DP Enabled */
					      0, /* power low e.g. normal */
					      0x2 /* Connected as Sink */);
	tcpci_partner_send_data_msg(&fixture->partner, PD_DATA_VENDOR_DEF,
				    vdm_attention_data, 2, 0);

	k_sleep(K_MSEC(100));
	/*
	 * Verify the HPD GPIO is set now
	 */
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);

	/* And disconnect */
	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 0);
}

ZTEST_F(ap_vdm_control, test_vdm_wake_on_dock)
{
	uint32_t vdm_attention_data[2];
	int opos = 1;
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_hpd);

	/* HPD GPIO should be low before the test */
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 0);

	run_verify_dp_entry(fixture, opos);

	/* Now put the AP to "sleep" */
	test_set_chipset_to_power_level(POWER_S3);

	/* Drain the MKBP event queue first */
	struct host_cmd_handler_args args;
	struct ec_response_get_next_event event;

	args.version = 0;
	args.command = EC_CMD_GET_NEXT_EVENT;
	args.params = NULL;
	args.params_size = 0;
	args.response = &event;
	args.response_max = sizeof(event);
	args.response_size = 0;

	while (host_command_process(&args) == EC_RES_SUCCESS) {
	}

	/* Test that we see our Attention message cause an event */
	vdm_attention_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1,
		    VDO_OPOS(opos) | VDO_CMDT(CMDT_INIT) | CMD_ATTENTION) |
		VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0);
	vdm_attention_data[1] = VDO_DP_STATUS(1, /* IRQ_HPD */
					      true, /* HPD_HI|LOW - Changed*/
					      0, /* request exit DP */
					      0, /* request exit USB */
					      0, /* MF pref */
					      true, /* DP Enabled */
					      0, /* power low e.g. normal */
					      0x2 /* Connected as Sink */);
	tcpci_partner_send_data_msg(&fixture->partner, PD_DATA_VENDOR_DEF,
				    vdm_attention_data, 2, 0);

	k_sleep(K_MSEC(100));

	/* Look for our MKBP event */
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);
	zassert_equal(event.event_type, EC_MKBP_EVENT_DP_ALT_MODE_ENTERED);
}
