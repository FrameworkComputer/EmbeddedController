/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "host_command.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "test_usbc_ctvpd.h"
#include "usb_pd_vdo.h"

#include <stdint.h>

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0
BUILD_ASSERT(TEST_PORT == USBC_PORT_C0);

struct tcpci_cable_data charge_through_vpd = {
	.identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT),
	.identity_vdm[VDO_INDEX_IDH] = VDO_IDH(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_VPD,
		/* modal operation */ false, USB_VID_GOOGLE),
	.identity_vdm[VDO_INDEX_CSTAT] = 0xabcdabcd,
	.identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(0x1234, 0xabcd),
	/* Hardware version 1, firmware version 2 */
	.identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] =
		VDO_VPD(1, 2, VPD_MAX_VBUS_20V, VPD_CT_CURRENT_3A,
			VPD_VBUS_IMP(10), VPD_GND_IMP(10), VPD_CTS_SUPPORTED),
	.identity_vdos = VDO_INDEX_PTYPE_UFP1_VDO + 1,
};

static void disconnect_partner_from_port(const struct emul *tcpc_emul,
					 const struct emul *charger_emul)
{
	zassert_ok(tcpci_emul_disconnect_partner(tcpc_emul));
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	k_sleep(K_SECONDS(1));
}

static void add_discovery_responses(struct tcpci_partner_data *partner)
{
	partner->cable = &charge_through_vpd;
	struct tcpci_cable_data *cable = partner->cable;

	/* Add Discover Identity response */
	cable->identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT);
	cable->identity_vdm[VDO_INDEX_IDH] = VDO_IDH(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_VPD,
		/* modal operation */ false, USB_VID_GOOGLE);
	cable->identity_vdm[VDO_INDEX_CSTAT] = 0xabcdabcd;
	cable->identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(0x1234, 0x5678);
	/* Hardware version 1, firmware version 2 */
	cable->identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] =
		VDO_VPD(1, 2, VPD_MAX_VBUS_20V, VPD_CT_CURRENT_3A,
			VPD_VBUS_IMP(10), VPD_GND_IMP(10), VPD_CTS_SUPPORTED);
	cable->identity_vdos = VDO_INDEX_PTYPE_UFP1_VDO + 1;
}

static void common_before(struct common_fixture *common)
{
	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	connect_sink_to_port(&common->partner, common->tcpci_emul,
			     common->charger_emul);
}

static void common_after(struct common_fixture *common)
{
	disconnect_partner_from_port(common->tcpci_emul, common->charger_emul);
}

static void *usbc_ctvpd_setup(void)
{
	static struct usbc_ctvpd_fixture fixture;
	struct common_fixture *common = &fixture.common;
	struct tcpci_partner_data *partner = &common->partner;
	struct tcpci_vpd_emul_data *vpd_ext = &common->vpd_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_vpd_emul_init(vpd_ext, partner, NULL);

	/* Get references for the emulators */
	common->tcpci_emul = EMUL_GET_USBC_BINDING(TEST_PORT, tcpc);
	common->charger_emul = EMUL_GET_USBC_BINDING(TEST_PORT, chg);

	add_discovery_responses(partner);

	return &fixture;
}

static void usbc_ctvpd_before(void *data)
{
	struct usbc_ctvpd_fixture *fixture = data;

	common_before(&fixture->common);
}

static void usbc_ctvpd_after(void *data)
{
	struct usbc_ctvpd_fixture *fixture = data;

	common_after(&fixture->common);
}

ZTEST_SUITE(usbc_ctvpd, drivers_predicate_post_main, usbc_ctvpd_setup,
	    usbc_ctvpd_before, usbc_ctvpd_after, NULL);

ZTEST_USER_F(usbc_ctvpd, test_discovery)
{
	uint8_t response_buffer[EC_LPC_HOST_PACKET_SIZE];
	struct ec_response_typec_discovery *discovery =
		(struct ec_response_typec_discovery *)response_buffer;
	struct common_fixture *common = &fixture->common;

	host_cmd_typec_discovery(TEST_PORT, TYPEC_PARTNER_SOP_PRIME,
				 response_buffer, sizeof(response_buffer));

	/* The host command does not count the VDM header in identity_count. */
	zassert_equal(discovery->identity_count,
		      common->partner.cable->identity_vdos - 1,
		      "Expected %d identity VDOs, got %d",
		      common->partner.cable->identity_vdos - 1,
		      discovery->identity_count);
	zassert_mem_equal(discovery->discovery_vdo,
			  common->partner.cable->identity_vdm + 1,
			  discovery->identity_count *
				  sizeof(*discovery->discovery_vdo),
			  "Discovered SOP identity ACK did not match");
}

ZTEST_USER_F(usbc_ctvpd, test_no_vconn_swap)
{
	struct ec_response_typec_status status =
		host_cmd_typec_status(TEST_PORT);
	enum pd_vconn_role initial_vconn_role = status.vconn_role;

	/* Upon initial attachment to the host port of a CT-VPD, the host (TCPM)
	 * should be Source and thus VCONN Source. After entry into
	 * CTAttached.SNK, the host should remain VCONN Source.
	 */
	zassert_equal(initial_vconn_role, PD_ROLE_VCONN_SRC);

	/* The TCPM should refuse to VCONN Swap while in CTAttached.SNK. */
	zassert_ok(tcpci_partner_send_control_msg(&fixture->common.partner,
						  PD_CTRL_VCONN_SWAP, 0));
	k_sleep(K_SECONDS(1));
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal(status.vconn_role, initial_vconn_role);
}
