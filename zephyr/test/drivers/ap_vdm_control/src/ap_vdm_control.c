/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_mux.h"
#include "usb_pd_vdo.h"

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

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
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT),
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
		if (memcmp(msg->buf, req->vdm_data,
			   req->vdm_data_objects * sizeof(uint32_t))) {
			message_seen = true;
			break;
		}
	}

	zassert_true(message_seen, "Expected message not found");
}

static void *ap_vdm_control_setup(void)
{
	static struct ap_vdm_control_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_src_emul_data *src_ext = &fixture.src_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_src_emul_init(src_ext, partner, NULL);
	partner->cable = &passive_usb3;

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul));
	fixture.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul));

	return &fixture;
}

static void ap_vdm_control_before(void *data)
{
	struct ap_vdm_control_fixture *fix = data;

	/* Set chipset on so the "AP" is on to give us commands */
	test_set_chipset_to_s0();

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
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM,
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
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM,
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
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM,
		      "Failed to see port error");
}

ZTEST_F(ap_vdm_control, test_send_vdm_sop_req_valid)
{
	uint32_t vdm_req_header = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT);
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
	uint32_t vdm_req_header = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT);
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
	uint32_t vdm_req_header = VDO(USB_SID_DISPLAYPORT, 1, CMD_ATTENTION);
	struct ec_params_typec_control params = {
		.port = TEST_PORT,
		.command = TYPEC_CONTROL_COMMAND_SEND_VDM_REQ,
		.vdm_req_params = {
			.vdm_data = { vdm_req_header },
			.vdm_data_objects = 5,
			.partner_type = TYPEC_PARTNER_SOP,
		},
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM,
		      "Failed to see port error");
}

ZTEST_F(ap_vdm_control, test_send_vdm_req_in_progress)
{
	uint32_t vdm_req_header = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT);
	struct ec_params_typec_control params = {
		.port = TEST_PORT,
		.command = TYPEC_CONTROL_COMMAND_SEND_VDM_REQ,
		.vdm_req_params = {
			.vdm_data = { vdm_req_header },
			.vdm_data_objects = 1,
			.partner_type = TYPEC_PARTNER_SOP,
		},
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	/*
	 * First command should succeed, but given no time to process the second
	 * should return busy
	 */
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS,
		      "Failed to send successful request");
	zassert_equal(host_command_process(&args), EC_RES_BUSY,
		      "Failed to see busy");
}
