/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_dp_alt_mode.h"
#include "usb_mux.h"
#include "usb_pd_vdo.h"

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define TEST_PORT USBC_PORT_C0

struct console_cmd_mfallow_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_src_emul_data src_ext;
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
			      1, /* MF pref - must be 1 for this test */
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

static uint32_t dp_config_extract(struct console_cmd_mfallow_fixture *fixture)
{
	struct tcpci_partner_log_msg *msg;

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

		uint32_t vdm_header = sys_get_le32(msg->buf + 2);

		/* We have a VDM, return if it's DP:Configure */
		if (PD_VDO_SVDM(vdm_header) &&
		    (PD_VDO_CMD(vdm_header) == CMD_DP_CONFIG))
			return sys_get_le32(msg->buf + 6);
	}

	zassert_unreachable();
	return 0;
}

static void *console_cmd_mfallow_setup(void)
{
	static struct console_cmd_mfallow_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_src_emul_data *src_ext = &fixture.src_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_src_emul_init(src_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul));
	fixture.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul));

	return &fixture;
}

static void console_cmd_mfallow_before(void *data)
{
	struct console_cmd_mfallow_fixture *fix = data;
	struct tcpci_partner_data *partner = &fix->partner;

	/* Set chipset on so the "AP" is on to give us commands */
	test_set_chipset_to_s0();

	/* Set up the partner as DP-capable with pin modes C and D */
	add_dp_discovery(partner);
	add_displayport_mode_responses(partner);

	/* Connect our port partner */
	connect_source_to_port(&fix->partner, &fix->src_ext, 0, fix->tcpci_emul,
			       fix->charger_emul);
}

static void console_cmd_mfallow_after(void *data)
{
	struct console_cmd_mfallow_fixture *fix = data;

	disconnect_source_from_port(fix->tcpci_emul, fix->charger_emul);
	tcpci_partner_common_clear_logged_msgs(&fix->partner);
}

ZTEST_SUITE(console_cmd_mfallow, drivers_predicate_post_main,
	    console_cmd_mfallow_setup, console_cmd_mfallow_before,
	    console_cmd_mfallow_after, NULL);

ZTEST_F(console_cmd_mfallow, test_mfallow_bad_arg_num)
{
	int rv = shell_execute_cmd(get_ec_shell(), "mfallow");

	zassert_equal(EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_F(console_cmd_mfallow, test_mfallow_bad_port)
{
	int rv = shell_execute_cmd(get_ec_shell(), "mfallow fish true");

	zassert_equal(EC_ERROR_PARAM1, rv);
}

ZTEST_F(console_cmd_mfallow, test_mfallow_bad_boolean)
{
	int rv = shell_execute_cmd(get_ec_shell(), "mfallow 0 sardine");

	zassert_equal(EC_ERROR_PARAM2, rv);
}

ZTEST_F(console_cmd_mfallow, test_mfallow_true)
{
	int rv = shell_execute_cmd(get_ec_shell(), "mfallow 0 true");

	zassert_equal(EC_SUCCESS, rv);

	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_MSEC(500));
	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);

	uint32_t config_vdo = dp_config_extract(fixture);

	zassert_equal(PD_DP_CFG_PIN(config_vdo), MODE_DP_PIN_D);
}

ZTEST_F(console_cmd_mfallow, test_mfallow_false)
{
	int rv = shell_execute_cmd(get_ec_shell(), "mfallow 0 false");

	zassert_equal(EC_SUCCESS, rv);

	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_MSEC(500));
	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);

	uint32_t config_vdo = dp_config_extract(fixture);

	zassert_equal(PD_DP_CFG_PIN(config_vdo), MODE_DP_PIN_C);
}
