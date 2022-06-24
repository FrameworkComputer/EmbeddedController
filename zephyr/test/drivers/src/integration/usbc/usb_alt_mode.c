/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <zephyr/zephyr.h>
#include <ztest.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

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

struct usbc_alt_mode_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
};

struct usbc_alt_mode_dp_unsupported_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
};

static void connect_partner_to_port(struct usbc_alt_mode_fixture *fixture)
{
	const struct emul *tcpc_emul = fixture->tcpci_emul;
	struct tcpci_partner_data *partner_emul = &fixture->partner;

	/*
	 * TODO(b/221439302) Updating the TCPCI emulator registers, updating the
	 *   vbus, as well as alerting should all be a part of the connect
	 *   function.
	 */
	/* Set VBUS to vSafe0V initially. */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);
	tcpci_tcpc_alert(0);
	k_sleep(K_SECONDS(1));
	zassume_ok(tcpci_partner_connect_to_tcpci(partner_emul, tcpc_emul),
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

static void add_discovery_responses(struct tcpci_partner_data *partner)
{
	/* Add Discover Identity response */
	partner->identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT);
	partner->identity_vdm[VDO_INDEX_IDH] = VDO_IDH(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_AMA,
		/* modal operation */ true, USB_VID_GOOGLE);
	partner->identity_vdm[VDO_INDEX_CSTAT] = 0xabcdabcd;
	partner->identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(0x1234, 0x5678);
	/* Hardware version 1, firmware version 2 */
	partner->identity_vdm[VDO_INDEX_AMA] = 0x12000000;
	partner->identity_vdos = VDO_INDEX_AMA + 1;

	/* Add Discover Modes response */
	/* Support one mode for DisplayPort VID. Copied from Hoho. */
	partner->modes_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_MODES);
	partner->modes_vdm[VDO_INDEX_HDR + 1] = VDO_MODE_DP(
		0, MODE_DP_PIN_C, 1, CABLE_PLUG, MODE_DP_V13, MODE_DP_SNK);
	partner->modes_vdos = VDO_INDEX_HDR + 2;

	/* Add Discover SVIDs response */
	/* Support DisplayPort VID. */
	partner->svids_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_SVID);
	partner->svids_vdm[VDO_INDEX_HDR + 1] =
		VDO_SVID(USB_SID_DISPLAYPORT, 0);
	partner->svids_vdos = VDO_INDEX_HDR + 2;
}

static void add_displayport_mode_responses(struct tcpci_partner_data *partner)
{
	/* DisplayPort alt mode setup remains in the same suite as discovery
	 * setup because DisplayPort is picked from the Discovery VDOs offered.
	 */

	/* Add DisplayPort EnterMode response */
	partner->dp_enter_mode_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_ENTER_MODE);
	partner->dp_enter_mode_vdos = VDO_INDEX_HDR + 1;

	/* Add DisplayPort StatusUpdate response */
	partner->dp_status_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DP_STATUS);
	partner->dp_status_vdm[VDO_INDEX_HDR + 1] =
		/* Mainly copied from hoho */
		VDO_DP_STATUS(0, /* IRQ_HPD */
			      false, /* HPD_HI|LOW - Changed*/
			      0, /* request exit DP */
			      0, /* request exit USB */
			      0, /* MF pref */
			      true, /* DP Enabled */
			      0, /* power low e.g. normal */
			      0x2 /* Connected as Sink */);
	partner->dp_status_vdos = VDO_INDEX_HDR + 2;

	/* Add DisplayPort Configure Response */
	partner->dp_config_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DP_CONFIG);
	partner->dp_config_vdos = VDO_INDEX_HDR + 1;
}

static void *usbc_alt_mode_setup(void)
{
	static struct usbc_alt_mode_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_snk_emul_data *snk_ext = &fixture.snk_ext;

	tcpci_partner_init(partner, PD_REV20);
	partner->extensions = tcpci_snk_emul_init(snk_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(tcpci_emul)));
	fixture.charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));

	add_discovery_responses(partner);
	add_displayport_mode_responses(partner);

	/* Sink 5V 3A. */
	snk_ext->pdo[1] = PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &fixture;
}

static void *usbc_alt_mode_dp_unsupported_setup(void)
{
	static struct usbc_alt_mode_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_snk_emul_data *snk_ext = &fixture.snk_ext;

	tcpci_partner_init(partner, PD_REV20);
	partner->extensions = tcpci_snk_emul_init(snk_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(tcpci_emul)));
	/* The configured TCPCI rev must match the emulator's supported rev. */
	tcpc_config[TEST_PORT].flags |= TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(fixture.tcpci_emul, TCPCI_EMUL_REV2_0_VER1_1);
	fixture.charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));

	/*
	 * Respond to discovery REQs to indicate DisplayPort support, but do not
	 * respond to DisplayPort alt mode VDMs, including Enter Mode.
	 */
	add_discovery_responses(partner);

	/* Sink 5V 3A. */
	snk_ext->pdo[1] = PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

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
	zassert_equal(discovery->svids[0].svid, USB_SID_DISPLAYPORT,
		      "Expected SVID 0x%0000x, got 0x%0000x",
		      USB_SID_DISPLAYPORT, discovery->svids[0].svid);
	zassert_equal(discovery->svids[0].mode_count, 1,
		      "Expected 1 DP mode, got %d",
		      discovery->svids[0].mode_count);
	zassert_equal(discovery->svids[0].mode_vdo[0],
		      fixture->partner.modes_vdm[1],
		      "DP mode VDOs did not match");
}

ZTEST_F(usbc_alt_mode, verify_displayport_mode_entry)
{
	/* TODO(b/237553647): Test EC-driven mode entry (requires a separate
	 * config).
	 */
	host_cmd_typec_control(TEST_PORT,
	TYPEC_CONTROL_COMMAND_ENTER_MODE, TYPEC_MODE_DP); k_sleep(K_SECONDS(1));

	/* Verify host command when VDOs are present. */
	struct ec_params_usb_pd_get_mode_request params = {
		.port = TEST_PORT,
		.svid_idx = 0,
	};
	struct ec_params_usb_pd_get_mode_response response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_USB_PD_GET_AMODE, 0, response, params);

	zassume_ok(host_command_process(&args), NULL);
	zassume_ok(args.result, NULL);

	/* Response should be populated with a DisplayPort VDO */
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.svid, USB_SID_DISPLAYPORT, NULL);
	zassert_equal(response.vdo[0],
		      fixture->partner.modes_vdm[response.opos], NULL);

	/* DPM configures the partner on DP mode entry */
	/* Verify port partner thinks its configured for DisplayPort */
	zassert_true(fixture->partner.displayport_configured, NULL);
}

ZTEST_F(usbc_alt_mode, verify_displayport_mode_reentry)
{
	host_cmd_typec_control(TEST_PORT, TYPEC_CONTROL_COMMAND_ENTER_MODE,
			       TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	/* DPM configures the partner on DP mode entry */
	/* Verify port partner thinks its configured for DisplayPort */
	zassert_true(fixture->partner.displayport_configured, NULL);

	host_cmd_typec_control(TEST_PORT, TYPEC_CONTROL_COMMAND_EXIT_MODES, 0);
	k_sleep(K_SECONDS(1));
	zassert_false(fixture->partner.displayport_configured, NULL);

	host_cmd_typec_control(TEST_PORT, TYPEC_CONTROL_COMMAND_ENTER_MODE,
			       TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));
	zassert_true(fixture->partner.displayport_configured, NULL);

	/* Verify that DisplayPort is the active alternate mode. */
	/* TODO(b/235984702): Wrap EC_CMD_USB_PD_GET_AMODE in a function. */
	struct ec_params_usb_pd_get_mode_request params = {
		.port = TEST_PORT,
		.svid_idx = 0,
	};
	struct ec_params_usb_pd_get_mode_response response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_USB_PD_GET_AMODE, 0, response, params);

	zassume_ok(host_command_process(&args), NULL);
	zassume_ok(args.result, NULL);

	/* Response should be populated with a DisplayPort VDO */
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.svid, USB_SID_DISPLAYPORT, NULL);
	zassert_equal(response.vdo[0],
		      fixture->partner.modes_vdm[response.opos], NULL);

}

ZTEST_SUITE(usbc_alt_mode, drivers_predicate_post_main, usbc_alt_mode_setup,
	    usbc_alt_mode_before, usbc_alt_mode_after, NULL);

/*
 * When the partner advertises DP mode support but refuses to enter, discovery
 * should still work as if the partner were compliant.
 */
ZTEST_F(usbc_alt_mode_dp_unsupported, verify_discovery)
{
	host_cmd_typec_control(TEST_PORT, TYPEC_CONTROL_COMMAND_ENTER_MODE,
				   TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	uint8_t response_buffer[EC_LPC_HOST_PACKET_SIZE];
	struct ec_response_typec_discovery *discovery =
		(struct ec_response_typec_discovery *)response_buffer;
	host_cmd_typec_discovery(TEST_PORT, TYPEC_PARTNER_SOP,
			response_buffer, sizeof(response_buffer));

	/* The host command does not count the VDM header in identity_count. */
	zassert_equal(discovery->identity_count,
		      fixture->partner.identity_vdos - 1,
		      "Expected %d identity VDOs, got %d",
		      fixture->partner.identity_vdos - 1,
		      discovery->identity_count);
	zassert_mem_equal(discovery->discovery_vdo,
			  fixture->partner.identity_vdm + 1,
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
		      fixture->partner.modes_vdm[1],
		      "DP mode VDOs did not match");
}

/*
 * When the partner advertises DP support but refuses to enter DP mode, the TCPM
 * should try once and then give up.
 */
ZTEST_F(usbc_alt_mode_dp_unsupported, verify_displayport_mode_nonentry)
{
	host_cmd_typec_control(TEST_PORT, TYPEC_CONTROL_COMMAND_ENTER_MODE,
			       TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	zassert_false(fixture->partner.displayport_configured, NULL);
	int dp_attempts =
		atomic_get(&fixture->partner.displayport_enter_attempts);
	zassert_equal(dp_attempts, 1, "Expected 1 DP attempt, got %d",
		      dp_attempts);
}

ZTEST_SUITE(usbc_alt_mode_dp_unsupported, drivers_predicate_post_main,
	    usbc_alt_mode_dp_unsupported_setup, usbc_alt_mode_before,
	    usbc_alt_mode_after, NULL);
