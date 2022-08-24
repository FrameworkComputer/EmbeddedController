/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <zephyr/zephyr.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_ps8xxx.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "host_command.h"
#include "test/drivers/stubs.h"
#include "tcpm/tcpci.h"
#include "test/drivers/utils.h"
#include "test/drivers/test_state.h"

#define TEST_PORT USBC_PORT_C0
/* Remove polarity for any mux checks */
#define USB_MUX_CHECK_MASK ~USB_PD_MUX_POLARITY_INVERTED

struct usbc_tbt_mode_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
};

static void add_sop_discovery_responses(struct tcpci_partner_data *partner)
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
}

static void *usbc_tbt_mode_setup(void)
{
	static struct usbc_tbt_mode_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_snk_emul_data *snk_ext = &fixture.snk_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_snk_emul_init(snk_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(tcpci_emul)));
	fixture.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul));

	add_sop_discovery_responses(partner);
	/* Note: cable behavior will vary by test case */

	/* Sink 5V 3A. */
	snk_ext->pdo[1] = PDO_FIXED(5000, 3000, PDO_FIXED_COMM_CAP);

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

ZTEST_F(usbc_tbt_mode, verify_discovery)
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
ZTEST_F(usbc_tbt_mode, verify_tbt_entry_fail)
{
	struct tcpci_partner_log_msg *msg;
	struct ec_response_typec_status status;

	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	status = host_cmd_typec_status(TEST_PORT);
	zassume_equal((status.mux_state & USB_MUX_CHECK_MASK),
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
	SYS_SLIST_FOR_EACH_CONTAINER(&fixture->partner.msg_log, msg, node)
	{
		/* We may have gotten some messages, but no EnterModes */
		uint16_t header = sys_get_le16(msg->buf);

		if ((PD_HEADER_CNT(header) != 0) &&
		    (PD_HEADER_TYPE(header) == PD_DATA_VENDOR_DEF)) {
			uint16_t vdm_header = sys_get_le16(msg->buf);

			zassert_not_equal(PD_VDO_CMD(vdm_header),
					  CMD_ENTER_MODE, "Unexpected Enter");
		}
	}
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Failed to see USB still set");
	zassert_not_equal((status.mux_state & USB_MUX_CHECK_MASK),
			  USB_PD_MUX_TBT_COMPAT_ENABLED,
			  "Unexpected TBT mode set");
}

ZTEST_SUITE(usbc_tbt_mode, drivers_predicate_post_main, usbc_tbt_mode_setup,
	    usbc_tbt_mode_before, usbc_tbt_mode_after, NULL);
