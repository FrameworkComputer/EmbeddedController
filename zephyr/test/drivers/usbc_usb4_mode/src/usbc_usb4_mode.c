/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "ec_tasks.h"
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
#include <zephyr/ztest.h>

#define TEST_PORT USBC_PORT_C0
/* Remove polarity for any mux checks */
#define USB_MUX_CHECK_MASK ~USB_PD_MUX_POLARITY_INVERTED

struct usbc_usb4_mode_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
};

/* Passive USB4 cable */
struct tcpci_cable_data passive_usb4 = {
	.identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT),
	.identity_vdm[VDO_INDEX_IDH] = VDO_IDH(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_PCABLE,
		/* modal operation */ false, USB_VID_GOOGLE),
	.identity_vdm[VDO_INDEX_CSTAT] = 0,
	.identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(0x1234, 0xABCD),
	.identity_vdm[VDO_INDEX_PTYPE_CABLE1] =
		VDO_REV30_PASSIVE(USB_R30_SS_U40_GEN3, USB_VBUS_CUR_3A,
				  USB_REV30_LATENCY_1m, USB_REV30_TYPE_C),
	.identity_vdos = VDO_INDEX_PTYPE_CABLE1 + 1,

};

static void add_sop_vdm_responses(struct tcpci_partner_data *partner)
{
	/* Add Discover Identity response */
	partner->identity_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_IDENT);
	partner->identity_vdm[VDO_INDEX_IDH] = VDO_IDH_REV30(
		/* USB host */ false, /* USB device */ true,
		/* ptype_u */ IDH_PTYPE_HUB, /* modal */ false,
		/* ptype_d */ IDH_PTYPE_UNDEF, /* ctype */ USB_TYPEC_RECEPTACLE,
		USB_VID_GOOGLE);
	partner->identity_vdm[VDO_INDEX_CSTAT] = 0;
	partner->identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(0x1234, 0x5678);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] = VDO_UFP1(
		/* capability */ (VDO_UFP1_CAPABILITY_USB20 |
				  VDO_UFP1_CAPABILITY_USB32 |
				  VDO_UFP1_CAPABILITY_USB4),
		/* ctype */ USB_TYPEC_RECEPTACLE,
		/* alt modes */ VDO_UFP1_ALT_MODE_TBT3,
		/* speed */ USB_R30_SS_U40_GEN3);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP2_VDO] = 0;
	partner->identity_vdos = VDO_INDEX_PTYPE_UFP2_VDO + 1;

	/* Add Discover SVIDs response */
	/*
	 * TODO(b/260095516): USB4 entry does not depend on the contents of
	 * Discover SVIDs, but a valid Discover SVID response needs to to exist
	 * to ensure that discovery completes as that's a dependency in the DPM
	 * module to attempt either Enter_USB or DATA_RESET.
	 */
	partner->svids_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_PD, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_SVID);
	partner->svids_vdm[VDO_INDEX_HDR + 1] = VDO_SVID(USB_VID_INTEL, 0);
	partner->svids_vdos = VDO_INDEX_HDR + 2;

	/* Add Discover Modes response */
	/*
	 * TODO(b/260095516): USB4 entry does not depend on the contents of
	 * Discover Modes, but a valid Discover Modes response needs to to exist
	 * to ensure that discovery completes as that's a dependency in the DPM
	 * module to attempt either Enter_USB or DATA_RESET.
	 */
	/* Support one mode for TBT (Intel) VID */
	partner->modes_vdm[VDO_INDEX_HDR] =
		VDO(USB_VID_INTEL, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_DISCOVER_MODES);
	partner->modes_vdm[VDO_INDEX_HDR + 1] = TBT_ALTERNATE_MODE;
	partner->modes_vdos = VDO_INDEX_HDR + 2;
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

static void *usbc_usb4_mode_setup(void)
{
	static struct usbc_usb4_mode_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_snk_emul_data *snk_ext = &fixture.snk_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_snk_emul_init(snk_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul));
	fixture.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul));

	add_sop_vdm_responses(partner);
	/* Note: cable behavior will vary by test case */

	return &fixture;
}

static void usbc_usb4_mode_before(void *data)
{
	struct usbc_usb4_mode_fixture *fix = data;

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give TCPM time to spin */
	k_sleep(K_SECONDS(1));

	/* Enable message logging after TCPM spin */
	tcpci_partner_common_enable_pd_logging(&fix->partner, true);

	/* Initialize parter port Enter_USB msg accept/reject state */
	fix->partner.enter_usb_accept = false;
}

static void usbc_usb4_mode_after(void *data)
{
	struct usbc_usb4_mode_fixture *fix = data;

	disconnect_sink_from_port(fix->tcpci_emul);
	tcpci_partner_common_enable_pd_logging(&fix->partner, false);
	tcpci_partner_common_clear_logged_msgs(&fix->partner);
}

ZTEST_F(usbc_usb4_mode, test_discovery)
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
}

/* Without an e-marked cable, USB4 mode cannot be entered */
ZTEST_F(usbc_usb4_mode, test_usb4_entry_fail)
{
	struct ec_response_typec_status status;

	fixture->partner.cable = NULL;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Unexpected starting mux: 0x%02x",
		      status.mux_state);

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_USB4);
	k_sleep(K_SECONDS(1));

	/*
	 * TODO(b/260095516): Notify the AP that the enter mode request
	 * failed.
	 */

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Failed to see USB still set");
	zassert_not_equal((status.mux_state & USB_MUX_CHECK_MASK),
			  USB_PD_MUX_USB4_ENABLED, "Unexpected USB4 mode set");
}

/* With passive e-marked cable, USB4 mode can be entered on SOP only */
ZTEST_F(usbc_usb4_mode, test_usb4_passive_entry_exit)
{
	struct ec_response_typec_status status;

	fixture->partner.cable = &passive_usb4;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	/* Instruct partner port to accept Enter_USB message */
	fixture->partner.enter_usb_accept = true;

	/* Verify that we properly identify a USB4 capable passive cable */
	verify_cable_found(fixture->partner.cable);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Unexpected starting mux: 0x%02x",
		      status.mux_state);

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_USB4);
	k_sleep(K_SECONDS(2));

	/*
	 * TODO(b/260095516): Notify the AP that the enter mode request
	 * succeeded.
	 */

	/* Verify we entered USB4 mode */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB4_ENABLED, "Failed to see USB4 set");

	/* Exit modes now */
	host_cmd_typec_control_exit_modes(TEST_PORT);
	k_sleep(K_SECONDS(1));

	/* Verify that USB4 mode was exited by checking current mux state. */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Failed to see USB set");
}

/* If the partner claims to support USB4, but communication is only PD 2.0, the
 * EC should disregard a request to enter USB4 from the host.
 */
ZTEST_F(usbc_usb4_mode, test_usb4_pd2_no_entry)
{
	struct ec_response_typec_status status;

	tcpci_partner_init(&fixture->partner, PD_REV20);
	fixture->partner.cable = &passive_usb4;
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	/* Instruct partner port to accept Enter_USB message */
	fixture->partner.enter_usb_accept = true;

	/* Verify that we properly identify a USB4 capable passive cable */
	verify_cable_found(fixture->partner.cable);

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Unexpected starting mux: 0x%02x",
		      status.mux_state);

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_USB4);
	k_sleep(K_SECONDS(1));

	/* PD 2.0 doesn't include Enter_USB, so it's not possible to enter USB4
	 * mode. A Discover Identity ACK indicating support for USB4 isn't even
	 * valid under PD 2.0. If the host nevertheless commands the EC to enter
	 * USB4, the EC should not attempt to do so.
	 */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_MUX_CHECK_MASK),
		      USB_PD_MUX_USB_ENABLED, "Failed to see USB still set");
	zassert_not_equal((status.mux_state & USB_MUX_CHECK_MASK),
			  USB_PD_MUX_USB4_ENABLED, "Unexpected USB4 mode set");
}

/*
 * TODO(b/260095516): This test suite is only testing the default good case, and
 * one error case where the cable doesn't support USB4. This suite needs to be
 * expanded to cover cases where the port partner rejects Enter_USB along with
 * active cable cases.
 */
ZTEST_SUITE(usbc_usb4_mode, drivers_predicate_post_main, usbc_usb4_mode_setup,
	    usbc_usb4_mode_before, usbc_usb4_mode_after, NULL);
