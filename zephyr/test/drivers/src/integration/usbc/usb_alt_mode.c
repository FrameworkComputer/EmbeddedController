/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <zephyr.h>
#include <ztest.h>
#include <drivers/gpio/gpio_emul.h>

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

struct usbc_alt_mode_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_snk_emul partner_emul;
};

static void connect_partner_to_port(struct usbc_alt_mode_fixture *fixture)
{
	const struct emul *tcpc_emul = fixture->tcpci_emul;
	struct tcpci_snk_emul *partner_emul = &fixture->partner_emul;

	/* Set VBUS to vSafe0V initially. */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);
	tcpci_tcpc_alert(0);
	zassume_ok(tcpci_snk_emul_connect_to_tcpci(
			   &partner_emul->data, &partner_emul->common_data,
			   &partner_emul->ops, tcpc_emul),
		   NULL);

	/* Wait for PD negotiation and current ramp. */
	k_sleep(K_SECONDS(10));
}

static void disconnect_partner_from_port(struct usbc_alt_mode_fixture *fixture)
{
	zassume_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul), NULL);
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	k_sleep(K_SECONDS(1));
}

static void *usbc_alt_mode_setup(void)
{
	static struct usbc_alt_mode_fixture fixture;
	struct tcpci_partner_data *partner_common =
		&fixture.partner_emul.common_data;

	tcpci_snk_emul_init(&fixture.partner_emul);

	/* Get references for the emulators */
	fixture.tcpci_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(tcpci_emul)));
	/* The configured TCPCI rev must match the emulator's supported rev. */
	tcpc_config[0].flags |= TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(fixture.tcpci_emul, TCPCI_EMUL_REV2_0_VER1_1);
	fixture.charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));

	/* Set up SOP discovery responses for DP adapter. */
	partner_common->identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT);
	partner_common->identity_vdm[VDO_INDEX_IDH] = VDO_IDH(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_AMA,
		/* modal operation */ true, USB_VID_GOOGLE);
	partner_common->identity_vdm[VDO_INDEX_CSTAT] = 0xabcdabcd;
	partner_common->identity_vdm[VDO_INDEX_PRODUCT] =
		VDO_PRODUCT(0x1234, 0x5678);
	/* Hardware version 1, firmware version 2 */
	partner_common->identity_vdm[VDO_INDEX_AMA] = 0x12000000;
	partner_common->identity_vdos = VDO_INDEX_AMA + 1;

	/* Support DisplayPort VID. */
	partner_common->svids_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_SVID);
	partner_common->svids_vdm[VDO_INDEX_HDR + 1] =
		VDO_SVID(USB_SID_DISPLAYPORT, 0);
	partner_common->svids_vdos = VDO_INDEX_HDR + 2;

	/* Support one mode for DisplayPort VID. Copied from Hoho. */
	partner_common->modes_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_MODES);
	partner_common->modes_vdm[VDO_INDEX_HDR + 1] = VDO_MODE_DP(
		0, MODE_DP_PIN_C, 1, CABLE_PLUG, MODE_DP_V13, MODE_DP_SNK);
	partner_common->modes_vdos = VDO_INDEX_HDR + 2;

	/* Sink 5V 3A. */
	fixture.partner_emul.data.pdo[1] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &fixture;
}

static void usbc_alt_mode_before(void *data)
{
	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	connect_partner_to_port((struct usbc_alt_mode_fixture *)data);
}

static void usbc_alt_mode_after(void *data)
{
	disconnect_partner_from_port((struct usbc_alt_mode_fixture *)data);
}

ZTEST_F(usbc_alt_mode, verify_discovery)
{
	uint8_t response_buffer[EC_LPC_HOST_PACKET_SIZE];
	struct ec_response_typec_discovery *discovery =
		(struct ec_response_typec_discovery *)response_buffer;
	host_cmd_typec_discovery(USBC_PORT_C0, TYPEC_PARTNER_SOP,
			response_buffer, sizeof(response_buffer));

	/* The host command does not count the VDM header in identity_count. */
	zassert_equal(discovery->identity_count,
		      this->partner_emul.common_data.identity_vdos - 1,
		      "Expected %d identity VDOs, got %d",
		      this->partner_emul.common_data.identity_vdos - 1,
		      discovery->identity_count);
	zassert_mem_equal(discovery->discovery_vdo,
			  this->partner_emul.common_data.identity_vdm + 1,
			  discovery->identity_count *
				  sizeof(*discovery->discovery_vdo),
			  "Discovered SOP identity ACK did not match");
	zassert_equal(discovery->svid_count, 1, "Expected 1 SVID, got %d",
		      discovery->svid_count);
	zassert_equal(discovery->svids[0].svid, USB_SID_DISPLAYPORT,
		      "Expected SVID 0x%0000x, got 0x%0000x",
		      USB_SID_DISPLAYPORT, discovery->svids[0].svid);
	zassert_equal(discovery->svids[0].mode_count, 1,
		      "Expected 1 DP mode, got %d",
		      discovery->svids[0].mode_count);
	zassert_equal(discovery->svids[0].mode_vdo[0],
		      this->partner_emul.common_data.modes_vdm[1],
		      "DP mode VDOs did not match");
}

ZTEST_SUITE(usbc_alt_mode, drivers_predicate_post_main, usbc_alt_mode_setup,
	    usbc_alt_mode_before, usbc_alt_mode_after, NULL);
