/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_ps8xxx.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "host_command.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd_vdo.h"

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/slist.h>
#include <zephyr/ztest.h>

#define TEST_PORT USBC_PORT_C0
/* Remove polarity for any mux checks */
#define USB_MUX_CHECK_MASK ~USB_PD_MUX_POLARITY_INVERTED

struct usbc_tbt_mode_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
};

/* Passive USB3 cable */
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

static void add_sop_vdm_responses(struct tcpci_partner_data *partner)
{
	/* Add Discover Identity response */
	partner->identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT);
	partner->identity_vdm[VDO_INDEX_IDH] = VDO_IDH(
		/* USB host */ false, /* USB device */ true, IDH_PTYPE_DFP_HUB,
		/* modal operation */ true, USB_VID_GOOGLE);
	partner->identity_vdm[VDO_INDEX_CSTAT] = 0;
	partner->identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(0x1234, 0x5678);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] = VDO_UFP1(
		(VDO_UFP1_CAPABILITY_USB20 | VDO_UFP1_CAPABILITY_USB32),
		USB_TYPEC_RECEPTACLE, VDO_UFP1_ALT_MODE_TBT3,
		USB_R30_SS_U40_GEN3);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP2_VDO] = 0;
	partner->identity_vdos = VDO_INDEX_PTYPE_UFP2_VDO + 1;

	/* Add Discover SVIDs response */
	/* Support TBT (Intel) VID. */
	partner->svids_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_SVID);
	partner->svids_vdm[VDO_INDEX_HDR + 1] = VDO_SVID(USB_VID_INTEL, 0);
	partner->svids_vdos = VDO_INDEX_HDR + 2;

	/* Add Discover Modes response */
	/* Support one mode for TBT (Intel) VID */
	partner->modes_vdm[VDO_INDEX_HDR] =
		VDO(USB_VID_INTEL, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_MODES);
	partner->modes_vdm[VDO_INDEX_HDR + 1] = TBT_ALTERNATE_MODE;
	partner->modes_vdos = VDO_INDEX_HDR + 2;

	/* Add affirmative mode entry */
	partner->enter_mode_vdm[VDO_INDEX_HDR] =
		VDO(USB_VID_INTEL, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_ENTER_MODE);
	partner->enter_mode_vdos = VDO_INDEX_HDR + 1;
}

/* How many EnterModes were we expecting? */
enum msg_check {
	NO_MSG,
	SOP_EXPECTED,
};

static void verify_vdm_messages(struct usbc_tbt_mode_fixture *fixture,
				enum msg_check check, int cmd_type)
{
	struct tcpci_partner_log_msg *msg;
	enum tcpci_msg_type types_seen[3];
	int messages_seen = 0;

	SYS_SLIST_FOR_EACH_CONTAINER(&fixture->partner.msg_log, msg, node)
	{
		uint16_t header = sys_get_le16(msg->buf);

		/* Ignore messages from ourselves */
		if (msg->sender == TCPCI_PARTNER_SENDER_PARTNER) {
			continue;
		}

		/*
		 * Control messages, non-VDMs, and extended messages are not of
		 * interest
		 */
		if ((PD_HEADER_CNT(header) == 0) ||
		    (PD_HEADER_TYPE(header) != PD_DATA_VENDOR_DEF) ||
		    (PD_HEADER_EXT(header) != 0)) {
			continue;
		}

		/* We have a VDM, check entry we're interested in */
		uint32_t vdm_header = sys_get_le32(msg->buf + sizeof(header));

		if (PD_VDO_CMD(vdm_header) != cmd_type) {
			continue;
		}

		types_seen[messages_seen] = PD_HEADER_GET_SOP(header);
		messages_seen++;
	}

	/*
	 * Processing done, now verify message ordering.  See Type-C
	 * specification 6.7 Active Cables That Support Alternate Modes
	 */
	if (check == NO_MSG) {
		zassert_equal(messages_seen, 0,
			      "Unexpected messages (cmd %d, num %d)", cmd_type,
			      messages_seen);
	} else if (check == SOP_EXPECTED) {
		zassert_equal(messages_seen, 1,
			      "Unexpected messages (cmd %d, num %d)", cmd_type,
			      messages_seen);
		zassert_equal(types_seen[0], TCPCI_MSG_SOP,
			      "Unexpected SOP type: %d", types_seen[0]);
	}
}

static void verify_cable_found(struct tcpci_cable_data *cable)
{
	uint8_t response_buffer[EC_LPC_HOST_PACKET_SIZE];
	struct ec_response_typec_discovery *discovery =
		(struct ec_response_typec_discovery *)response_buffer;

	host_cmd_typec_discovery(TEST_PORT, TYPEC_PARTNER_SOP_PRIME,
				 response_buffer, sizeof(response_buffer));

	/* The host command does not count the VDM header in identity_count. */
	zassert_equal(discovery->identity_count, cable->identity_vdos - 1,
		      "Expected %d identity VDOs, got %d",
		      cable->identity_vdos - 1, discovery->identity_count);
	zassert_mem_equal(discovery->discovery_vdo, cable->identity_vdm + 1,
			  discovery->identity_count *
				  sizeof(*discovery->discovery_vdo),
			  "Discovered SOP' identity ACK did not match");
}

static void *usbc_tbt_mode_setup(void)
{
	static struct usbc_tbt_mode_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_snk_emul_data *snk_ext = &fixture.snk_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_snk_emul_init(snk_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul));
	fixture.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul));

	add_sop_vdm_responses(partner);
	/* Note: cable behavior will vary by test case */

	/* Sink 5V 3A. */
	snk_ext->pdo[0] = PDO_FIXED(5000, 3000, PDO_FIXED_COMM_CAP);

	return &fixture;
}

static void usbc_tbt_mode_before(void *data)
{
	ARG_UNUSED(data);

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));
}

static void usbc_tbt_mode_after(void *data)
{
	struct usbc_tbt_mode_fixture *fix = data;

	disconnect_sink_from_port(fix->tcpci_emul);
	tcpci_partner_common_clear_logged_msgs(&fix->partner);
}

ZTEST_F(usbc_tbt_mode, test_discovery)
{
	uint8_t response_buffer[EC_LPC_HOST_PACKET_SIZE];
	struct ec_response_typec_discovery *discovery =
		(struct ec_response_typec_discovery *)response_buffer;

	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	host_cmd_typec_discovery(TEST_PORT, TYPEC_PARTNER_SOP, response_buffer,
				 sizeof(response_buffer));

	/* The host command does not count the VDM header in identity_count. */
	zassert_equal(discovery->identity_count,
		      fixture->partner.identity_vdos - 1,
		      "Expected %d identity VDOs, got %d",
		      fixture->partner.identity_vdos - 1,
		      discovery->identity_count);
	zassert_mem_equal(
		discovery->discovery_vdo, fixture->partner.identity_vdm + 1,
		discovery->identity_count * sizeof(*discovery->discovery_vdo),
		"Discovered SOP identity ACK did not match");
	zassert_equal(discovery->svid_count, 1, "Expected 1 SVID, got %d",
		      discovery->svid_count);
	zassert_equal(discovery->svids[0].svid, USB_VID_INTEL,
		      "Expected SVID 0x%04x, got 0x%04x", USB_VID_INTEL,
		      discovery->svids[0].svid);
	zassert_equal(discovery->svids[0].mode_count, 1,
		      "Expected 1 TBT mode, got %d",
		      discovery->svids[0].mode_count);
	zassert_equal(discovery->svids[0].mode_vdo[0],
		      fixture->partner.modes_vdm[1],
		      "TBT mode VDOs did not match");
}

/* Without an e-marked cable, TBT mode cannot be entered */
ZTEST_F(usbc_tbt_mode, test_tbt_entry_fail)
{
	struct ec_response_typec_status status;

	fixture->partner.cable = NULL;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Unexpected starting mux: 0x%02x",
		      status.mux_state);

	/* TODO(b/237553647): Test EC-driven mode entry (requires a separate
	 * config).
	 */
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_TBT);
	k_sleep(K_SECONDS(1));

	/*
	 * TODO(b/168030639): Notify the AP that the enter mode request
	 * failed.
	 */

	/* Verify we refrained from sending TBT EnterMode. */
	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);
	verify_vdm_messages(fixture, NO_MSG, CMD_ENTER_MODE);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Failed to see USB still set");
	zassert_not_equal((status.mux_state & USB_MUX_CHECK_MASK),
			  USB_PD_MUX_TBT_COMPAT_ENABLED,
			  "Unexpected TBT mode set");
}

/* With passive e-marked cable, TBT mode can be entered on SOP only */
ZTEST_F(usbc_tbt_mode, test_tbt_passive_entry_exit)
{
	struct ec_response_typec_status status;

	fixture->partner.cable = &passive_usb3;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	verify_cable_found(fixture->partner.cable);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Unexpected starting mux: 0x%02x",
		      status.mux_state);

	/* TODO(b/237553647): Test EC-driven mode entry (requires a separate
	 * config).
	 */
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_TBT);
	k_sleep(K_SECONDS(1));

	/*
	 * TODO(b/168030639): Notify the AP that the enter mode request
	 * succeeded.
	 */

	/* Verify we sent a single TBT SOP EnterMode. */
	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);
	verify_vdm_messages(fixture, SOP_EXPECTED, CMD_ENTER_MODE);
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_TBT_COMPAT_ENABLED, "Failed to see TBT set");

	/* Exit modes now */
	tcpci_partner_common_clear_logged_msgs(&fixture->partner);
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_exit_modes(TEST_PORT);
	k_sleep(K_SECONDS(1));

	/* Verify we sent a single TBT SOP ExitMode. */
	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);
	verify_vdm_messages(fixture, SOP_EXPECTED, CMD_EXIT_MODE);
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Failed to see USB set");
}

ZTEST_SUITE(usbc_tbt_mode, drivers_predicate_post_main, usbc_tbt_mode_setup,
	    usbc_tbt_mode_before, usbc_tbt_mode_after, NULL);
