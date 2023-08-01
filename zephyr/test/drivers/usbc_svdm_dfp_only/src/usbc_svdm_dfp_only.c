/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This test exercises the SVDM_RSP_DFP_ONLY option, causing the device to
 * respond appropriately to SVDM Discover Identity requests when operating as
 * DFP.
 *
 * The tests correspond to TEST.PD.PVDM.SRC.1 Discovery Process and Enter Mode
 * as defined by the USB Power Delivery Compliance Test Specification.
 */
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd.h"

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

struct usbc_svdm_dfp_only_fixture {
	const struct emul *tcpci_emul;
	struct tcpci_partner_data partner;
	const struct emul *charger_emul;
	struct tcpci_src_emul_data src_emul_data;
};

struct identity_response {
	uint16_t header;
	uint32_t n_vdos;
	uint32_t vdos[5];
};

/**
 * Send a Discover Identity SVDM request from the emulated partner, returning
 * the response and failing if the response has an unexpected type or size, or
 * if too few PD messages are sent.
 */
static struct identity_response
get_identity_response(struct usbc_svdm_dfp_only_fixture *fixture)
{
	uint32_t discover_identity[] = {
		VDO(USB_SID_PD, 1,
		    VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0) | CMD_DISCOVER_IDENT),
	};

	/* Send a discover identity command from the partner */
	tcpci_partner_common_clear_logged_msgs(&fixture->partner);
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	tcpci_partner_send_data_msg(&fixture->partner, PD_DATA_VENDOR_DEF,
				    discover_identity,
				    ARRAY_SIZE(discover_identity), 0);
	k_sleep(K_SECONDS(1));
	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);

	tcpci_partner_common_print_logged_msgs(&fixture->partner);

	/* First message is the one we sent */
	zassert_not_null(sys_slist_get(&fixture->partner.msg_log),
			 "should have logged Discover_Identity request");
	/* Second should be the response */
	sys_snode_t *response_node = sys_slist_get(&fixture->partner.msg_log);

	zassert_not_null(response_node, "should have logged a PD response");

	struct tcpci_partner_log_msg *msg =
		SYS_SLIST_CONTAINER(response_node, msg, node);
	zassert_equal(msg->sop, TCPCI_MSG_SOP);

	struct identity_response out;

	out.header = sys_get_le16(msg->buf);
	out.n_vdos = (msg->cnt - sizeof(out.header)) / sizeof(*out.vdos);
	/*
	 * Header size must agree with the actual message size and fit in the
	 * vdos buffer.
	 */
	zassert_equal(PD_HEADER_CNT(out.header), out.n_vdos);
	zassert_true(out.n_vdos <= ARRAY_SIZE(out.vdos),
		     "response containing %d VDOs is too large", out.n_vdos);

	for (int i = 0; i < out.n_vdos; i++) {
		out.vdos[i] = sys_get_le32(
			&msg->buf[sizeof(*out.vdos) * i + sizeof(out.header)]);
	}

	return out;
}

/**
 * Verify that the header of the provided response describes a VDM response
 * for the given PD version.
 */
static void verify_response_header(const struct identity_response response,
				   enum pd_rev_type pd_rev)
{
	zassert_equal(PD_HEADER_TYPE(response.header), PD_DATA_VENDOR_DEF);
	zassert_equal(PD_HEADER_REV(response.header), pd_rev);
}

ZTEST_F(usbc_svdm_dfp_only, test_verify_identity)
{
	fixture->partner.rev = PD_REV30;
	connect_source_to_port(&fixture->partner, &fixture->src_emul_data, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	struct identity_response response = get_identity_response(fixture);

	verify_response_header(response, PD_REV30);
	zassert_equal(response.n_vdos, 5);

	/* SVDM header: ACKing Discover_Identity */
	zassert_equal(response.vdos[0],
		      VDO(USB_SID_PD, 1,
			  VDO_SVDM_VERS_MAJOR(1) | VDO_OPOS(0) |
				  VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT),
		      "VDM Header value unexpected: %#x", response.vdos[0]);
	/* ID Header VDO per PD 3.0 */
	zassert_equal(response.vdos[1],
		      VDO_IDH_REV30(1 /* is a USB host */,
				    0 /* not a USB device */,
				    IDH_PTYPE_UNDEF /* not a UFP */,
				    0 /* no modes supported */,
				    IDH_PTYPE_DFP_HOST /* PDUSB host */,
				    USB_TYPEC_RECEPTACLE, CONFIG_USB_VID));
	/* Cert Stat VDO */
	zassert_equal(response.vdos[2], CONFIG_USB_PD_XID,
		      "Cert Stat VDO value unexpected: %#x", response.vdos[2]);
	/* Product VDO */
	zassert_equal(response.vdos[3],
		      (CONFIG_USB_PID << 16) | CONFIG_USB_BCD_DEV,
		      "Product VDO value unexpected: %#x", response.vdos[3]);
	/* DFP Product Type VDO: version 1.1, USB3.2 capable, receptacle */
	zassert_equal(response.vdos[4], 0x22800000,
		      "DFP VDO had unexpected value %#x", response.vdos[4]);
}

ZTEST_F(usbc_svdm_dfp_only, test_verify_pd20_nak)
{
	fixture->partner.rev = PD_REV20;
	connect_source_to_port(&fixture->partner, &fixture->src_emul_data, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	struct identity_response response = get_identity_response(fixture);

	verify_response_header(response, PD_REV20);
	/* In PD 2.0 DFPs are required to nack a Discover Identity request */
	zassert_equal(response.n_vdos, 1);
	zassert_equal(response.vdos[0],
		      VDO(USB_SID_PD, 1,
			  VDO_SVDM_VERS_MAJOR(0) | VDO_OPOS(0) |
				  VDO_CMDT(CMDT_RSP_NAK) | CMD_DISCOVER_IDENT),
		      "VDM Header value unexpected: %#x", response.vdos[0]);
}

static void *usbc_svdm_dfp_only_setup()
{
	static struct usbc_svdm_dfp_only_fixture fixture = {
		.tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul)),
		.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul)),
	};

	tcpci_partner_init(&fixture.partner, PD_REV30);
	fixture.partner.extensions = tcpci_src_emul_init(
		&fixture.src_emul_data, &fixture.partner, NULL);
	fixture.src_emul_data.pdo[0] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	/* The first connect_source_to_port() fails if we don't do this */
	test_set_chipset_to_s0();

	return &fixture;
}

static void usbc_svdm_dfp_only_after(void *data)
{
	struct usbc_svdm_dfp_only_fixture *fixture = data;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	tcpci_partner_common_clear_logged_msgs(&fixture->partner);
}

ZTEST_SUITE(usbc_svdm_dfp_only, drivers_predicate_post_main,
	    usbc_svdm_dfp_only_setup, NULL, usbc_svdm_dfp_only_after, NULL);
