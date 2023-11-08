/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_ps8xxx.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "host_command.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "test_usbc_alt_mode.h"

#include <stdint.h>

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <gpio.h>

#define TEST_PORT 0

/* Arbitrary */
#define PARTNER_PRODUCT_ID 0x1234
#define PARTNER_DEV_BINARY_CODED_DECIMAL 0x5678

BUILD_ASSERT(TEST_PORT == USBC_PORT_C0);

void connect_partner_to_port(const struct emul *tcpc_emul,
			     const struct emul *charger_emul,
			     struct tcpci_partner_data *partner_emul,
			     const struct tcpci_src_emul_data *src_ext)
{
	/*
	 * TODO(b/221439302) Updating the TCPCI emulator registers, updating the
	 *   vbus, as well as alerting should all be a part of the connect
	 *   function.
	 */
	set_ac_enabled(true);
	zassert_ok(tcpci_partner_connect_to_tcpci(partner_emul, tcpc_emul),
		   NULL);

	isl923x_emul_set_adc_vbus(charger_emul,
				  PDO_FIXED_GET_VOLT(src_ext->pdo[0]));

	/* Wait for PD negotiation and current ramp. */
	k_sleep(K_SECONDS(10));
}

void disconnect_partner_from_port(const struct emul *tcpc_emul,
				  const struct emul *charger_emul)
{
	zassert_ok(tcpci_emul_disconnect_partner(tcpc_emul));
	isl923x_emul_set_adc_vbus(charger_emul, 0);
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
	partner->identity_vdm[VDO_INDEX_PRODUCT] = VDO_PRODUCT(
		PARTNER_PRODUCT_ID, PARTNER_DEV_BINARY_CODED_DECIMAL);
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
	partner->enter_mode_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_ENTER_MODE);
	partner->enter_mode_vdos = VDO_INDEX_HDR + 1;

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

static void add_displayport_mode_responses__minus_configure_responses(
	struct tcpci_partner_data *partner)
{
	/*
	 * This is the same function as add_displayport_mode_responses() but
	 * does not include the configure response so as to induce a failure to
	 * enter dp alt mode
	 */

	/* Add DisplayPort EnterMode response */
	partner->enter_mode_vdm[VDO_INDEX_HDR] =
		VDO(USB_SID_DISPLAYPORT, /* structured VDM */ true,
		    VDO_CMDT(CMDT_RSP_ACK) | CMD_ENTER_MODE);
	partner->enter_mode_vdos = VDO_INDEX_HDR + 1;

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

	/* NO DisplayPort Configure Response */
}

static void *usbc_alt_mode_setup(void)
{
	static struct usbc_alt_mode_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_src_emul_data *src_ext = &fixture.src_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_src_emul_init(src_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_PORT, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_PORT, chg);

	add_discovery_responses(partner);

	return &fixture;
}

static void usbc_alt_mode_before(void *data)
{
	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	struct usbc_alt_mode_fixture *fixture = data;

	/* Re-populate our usual responses in case a test overrode them */
	add_displayport_mode_responses(&fixture->partner);
	connect_partner_to_port(fixture->tcpci_emul, fixture->charger_emul,
				&fixture->partner, &fixture->src_ext);
}

static void usbc_alt_mode_after(void *data)
{
	struct usbc_alt_mode_fixture *fixture = data;

	disconnect_partner_from_port(fixture->tcpci_emul,
				     fixture->charger_emul);
}

ZTEST_F(usbc_alt_mode, test_discovery)
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

ZTEST_F(usbc_alt_mode, test_discovery_params_too_small)
{
	struct ec_response_typec_discovery discovery;

	/* The expected size of the response buffer is larger than struct
	 * ec_response_typec_discovery. With only that amount of space, the
	 * command should succeed but not return any of the discovered SVIDs.
	 */
	host_cmd_typec_discovery(TEST_PORT, TYPEC_PARTNER_SOP, &discovery,
				 sizeof(discovery));
	zassert_equal(discovery.svid_count, 0);
}

void verify_data_reset_msg(struct tcpci_partner_data *partner, bool want)
{
	struct tcpci_partner_log_msg *msg;
	bool sent_data_reset = false;

	SYS_SLIST_FOR_EACH_CONTAINER(&partner->msg_log, msg, node)
	{
		uint16_t header = sys_get_le16(msg->buf);

		/* Ignore messages from partner */
		if (msg->sender == TCPCI_PARTNER_SENDER_PARTNER)
			continue;

		/* Data messages and extended messages are not of interest. */
		if ((PD_HEADER_CNT(header) != 0) ||
		    (PD_HEADER_EXT(header) != 0)) {
			continue;
		}

		if (want) {
			if (PD_HEADER_TYPE(header) == PD_CTRL_DATA_RESET)
				sent_data_reset = true;
		} else {
			zassert_not_equal(PD_HEADER_TYPE(header),
					  PD_CTRL_DATA_RESET);
		}
	}

	if (want)
		zassert_true(sent_data_reset);
}

ZTEST_F(usbc_alt_mode, test_displayport_mode_entry)
{
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_hpd);

	if (IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		tcpci_partner_common_clear_logged_msgs(&fixture->partner);
		tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
		host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
		k_sleep(K_SECONDS(1));
	}
	/*
	 * For SOP Product Type == Alt Mode Adapter (typical legacy DP adapter)
	 * as simulated here, the TCPM should not issue a Data Reset.
	 */
	verify_data_reset_msg(&fixture->partner, false);

	/* Verify host command when VDOs are present. */
	struct ec_response_typec_status status;

	/* DPM configures the partner on DP mode entry */
	/* Verify port partner thinks its configured for DisplayPort */
	zassert_true(fixture->partner.displayport_configured);
	/* Verify we also set up DP on our mux */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_PD_MUX_DP_ENABLED),
		      USB_PD_MUX_DP_ENABLED, "Failed to see DP set in mux");

	/*
	 * DP alt mode partner sends HPD through VDM:Attention, which uses the
	 * same format as the DP Status data
	 */
	uint32_t vdm_attention_data[2];

	vdm_attention_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1,
		    VDO_OPOS(1) | VDO_CMDT(CMDT_INIT) | CMD_ATTENTION);
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
	/* Verify the board's HPD notification triggered */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_PD_MUX_HPD_LVL),
		      USB_PD_MUX_HPD_LVL, "Failed to set HPD level in mux");
	zassert_equal((status.mux_state & USB_PD_MUX_HPD_IRQ),
		      USB_PD_MUX_HPD_IRQ, "Failed to set HPD IRQ in mux");
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);
}

ZTEST_F(usbc_alt_mode, test_bad_hpd_irq_reject)
{
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_hpd);

	if (IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
		k_sleep(K_SECONDS(1));
	}

	/*
	 * Compose a bad Attention packet which sets IRQ with HPD low
	 */
	uint32_t vdm_attention_data[2];

	vdm_attention_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1,
		    VDO_OPOS(1) | VDO_CMDT(CMDT_INIT) | CMD_ATTENTION);
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
	k_sleep(K_SECONDS(1));

	/* Verify that no HPD notification triggered */
	struct ec_response_typec_status status;

	status = host_cmd_typec_status(TEST_PORT);
	zassert_not_equal((status.mux_state & USB_PD_MUX_HPD_LVL),
			  USB_PD_MUX_HPD_LVL);
	zassert_not_equal((status.mux_state & USB_PD_MUX_HPD_IRQ),
			  USB_PD_MUX_HPD_IRQ);
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 0);
}

ZTEST_F(usbc_alt_mode, test_hpd_clear)
{
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_hpd);

	if (IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
		k_sleep(K_SECONDS(1));
	}

	/*
	 * Set our HPD to high and then low, ensuring our HPD indicators
	 * track this
	 */
	uint32_t vdm_attention_data[2];

	vdm_attention_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1,
		    VDO_OPOS(1) | VDO_CMDT(CMDT_INIT) | CMD_ATTENTION);
	vdm_attention_data[1] = VDO_DP_STATUS(0, /* IRQ_HPD */
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

	/* Verify that HPD notification triggered */
	struct ec_response_typec_status status;

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_PD_MUX_HPD_LVL),
		      USB_PD_MUX_HPD_LVL);
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);

	vdm_attention_data[1] = VDO_DP_STATUS(0, /* IRQ_HPD */
					      false, /* HPD_HI|LOW - Changed*/
					      0, /* request exit DP */
					      0, /* request exit USB */
					      0, /* MF pref */
					      true, /* DP Enabled */
					      0, /* power low e.g. normal */
					      0x2 /* Connected as Sink */);
	tcpci_partner_send_data_msg(&fixture->partner, PD_DATA_VENDOR_DEF,
				    vdm_attention_data, 2, 0);

	k_sleep(K_SECONDS(1));
	/* Verify that HPD cleared */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_not_equal((status.mux_state & USB_PD_MUX_HPD_LVL),
			  USB_PD_MUX_HPD_LVL);
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 0);
}

ZTEST_F(usbc_alt_mode, test_hpd_irq_set)
{
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_hpd);

	if (IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
		k_sleep(K_SECONDS(1));
	}

	/*
	 * Set our HPD to high and toggle the IRQ low to high
	 */
	uint32_t vdm_attention_data[2];

	vdm_attention_data[0] =
		VDO(USB_SID_DISPLAYPORT, 1,
		    VDO_OPOS(1) | VDO_CMDT(CMDT_INIT) | CMD_ATTENTION);
	vdm_attention_data[1] = VDO_DP_STATUS(0, /* IRQ_HPD */
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

	/* Verify that HPD notification triggered */
	struct ec_response_typec_status status;

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_PD_MUX_HPD_LVL),
		      USB_PD_MUX_HPD_LVL);
	zassert_not_equal((status.mux_state & USB_PD_MUX_HPD_IRQ),
			  USB_PD_MUX_HPD_IRQ);
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);

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
	/* Verify that HPD IRQ set now */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_PD_MUX_HPD_LVL),
		      USB_PD_MUX_HPD_LVL);
	zassert_equal((status.mux_state & USB_PD_MUX_HPD_IRQ),
		      USB_PD_MUX_HPD_IRQ);
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);
}

ZTEST_F(usbc_alt_mode, test_discovery_via_pd_host_cmd)
{
	struct ec_params_usb_pd_info_request params = { .port = TEST_PORT };
	struct ec_params_usb_pd_discovery_entry response;

	struct host_cmd_handler_args args;

	zassert_ok(ec_cmd_usb_pd_discovery(&args, &params, &response));
	zassert_equal(args.response_size, sizeof(response));
	zassert_equal(response.ptype, IDH_PTYPE_AMA);
	zassert_equal(response.vid, USB_VID_GOOGLE);
	zassert_equal(response.pid, PARTNER_PRODUCT_ID);
}

ZTEST_SUITE(usbc_alt_mode, drivers_predicate_post_main, usbc_alt_mode_setup,
	    usbc_alt_mode_before, usbc_alt_mode_after, NULL);

static void *usbc_alt_mode_custom_discovery_setup(void)
{
	static struct usbc_alt_mode_custom_discovery_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_src_emul_data *src_ext = &fixture.src_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_src_emul_init(src_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_PORT, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_PORT, chg);

	return &fixture;
}

static void usbc_alt_mode_custom_discovery_before(void *data)
{
	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	struct usbc_alt_mode_custom_discovery_fixture *fixture = data;

	/* Re-populate our usual responses in case a test overrode them */
	add_discovery_responses(&fixture->partner);
	add_displayport_mode_responses(&fixture->partner);
	/* Do not connect to the partner to allow the test to override discovery
	 * responses.
	 */
}

static void usbc_alt_mode_custom_discovery_after(void *data)
{
	struct usbc_alt_mode_custom_discovery_fixture *fixture = data;

	disconnect_partner_from_port(fixture->tcpci_emul,
				     fixture->charger_emul);
}

ZTEST_SUITE(usbc_alt_mode_custom_discovery, drivers_predicate_post_main,
	    usbc_alt_mode_custom_discovery_setup,
	    usbc_alt_mode_custom_discovery_before,
	    usbc_alt_mode_custom_discovery_after, NULL);

static void *usbc_alt_mode_dp_unsupported_setup(void)
{
	static struct usbc_alt_mode_dp_unsupported_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_src_emul_data *src_ext = &fixture.src_ext;

	tcpci_partner_init(partner, PD_REV20);
	partner->extensions = tcpci_src_emul_init(src_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_PORT, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_PORT, chg);

	/*
	 * Respond to discovery REQs to indicate DisplayPort support, but do not
	 * respond to DisplayPort alt mode VDMs, including Enter Mode.
	 */
	add_discovery_responses(partner);

	return &fixture;
}

static void usbc_alt_mode_dp_unsupported_before(void *data)
{
	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	struct usbc_alt_mode_dp_unsupported_fixture *fixture = data;

	connect_partner_to_port(fixture->tcpci_emul, fixture->charger_emul,
				&fixture->partner, &fixture->src_ext);
}

static void usbc_alt_mode_dp_unsupported_after(void *data)
{
	struct usbc_alt_mode_dp_unsupported_fixture *fixture = data;

	disconnect_partner_from_port(fixture->tcpci_emul,
				     fixture->charger_emul);
}

/*
 * When the partner advertises DP mode support but refuses to enter, discovery
 * should still work as if the partner were compliant.
 */
ZTEST_F(usbc_alt_mode_dp_unsupported, test_discovery)
{
	if (IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
		k_sleep(K_SECONDS(1));
	}

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

/*
 * When the partner advertises DP support but refuses to enter DP mode, the TCPM
 * should try once and then give up.
 */
ZTEST_F(usbc_alt_mode_dp_unsupported, test_displayport_mode_nonentry)
{
	if (IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
		k_sleep(K_SECONDS(1));
	}

	zassert_false(fixture->partner.displayport_configured);
	int dp_attempts = atomic_get(&fixture->partner.mode_enter_attempts);
	zassert_equal(dp_attempts, 1, "Expected 1 DP attempt, got %d",
		      dp_attempts);
}

ZTEST_SUITE(usbc_alt_mode_dp_unsupported, drivers_predicate_post_main,
	    usbc_alt_mode_dp_unsupported_setup,
	    usbc_alt_mode_dp_unsupported_before,
	    usbc_alt_mode_dp_unsupported_after, NULL);

static void *usbc_alt_mode_minus_dp_configure_setup(void)
{
	static struct usbc_alt_mode_minus_dp_configure_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_src_emul_data *src_ext = &fixture.src_ext;

	tcpci_partner_init(partner, PD_REV20);
	partner->extensions = tcpci_src_emul_init(src_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_PORT, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_PORT, chg);

	add_discovery_responses(partner);
	add_displayport_mode_responses__minus_configure_responses(partner);

	return &fixture;
}

static void usbc_alt_mode_minus_dp_configure_before(void *data)
{
	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	struct usbc_alt_mode_minus_dp_configure_fixture *fixture = data;

	connect_partner_to_port(fixture->tcpci_emul, fixture->charger_emul,
				&fixture->partner, &fixture->src_ext);
}

static void usbc_alt_mode_minus_dp_configure_after(void *data)
{
	struct usbc_alt_mode_fixture *fixture = data;

	disconnect_partner_from_port(fixture->tcpci_emul,
				     fixture->charger_emul);
}

ZTEST_F(usbc_alt_mode_minus_dp_configure, test_dp_mode_entry_minus_config)
{
	if (IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
		k_sleep(K_SECONDS(1));
	}

	/* Verify host command when VDOs are present. */
	struct ec_response_typec_status status;

	/* DPM configures the partner on DP mode entry */
	/* Verify port partner thinks it's *NOT* configured for DisplayPort */
	zassert_false(fixture->partner.displayport_configured);
	/* Also verify DP config is missing from mux */
	status = host_cmd_typec_status(TEST_PORT);
	zassert_not_equal((status.mux_state & USB_PD_MUX_DP_ENABLED),
			  USB_PD_MUX_DP_ENABLED,
			  "Failed to *NOT* see DP set in mux");
}

ZTEST_SUITE(usbc_alt_mode_minus_dp_configure, drivers_predicate_post_main,
	    usbc_alt_mode_minus_dp_configure_setup,
	    usbc_alt_mode_minus_dp_configure_before,
	    usbc_alt_mode_minus_dp_configure_after, NULL);

/* Set up the partner to refuse to swap to UFP, preventing discovery in PD 2.0.
 * Configure DP alt mode responses to try to catch the TCPM entering DP mode
 * anyway.
 */
static void *usbc_alt_mode_no_drs_setup(void)
{
	static struct usbc_alt_mode_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_src_emul_data *src_ext = &fixture.src_ext;

	tcpci_partner_init(partner, PD_REV20);
	partner->extensions = tcpci_src_emul_init(src_ext, partner, NULL);
	tcpci_partner_set_drs_support(partner, /*drs_to_ufp_supported=*/false,
				      /*drs_to_dfp_supported=*/true);

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_PORT, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_PORT, chg);

	add_discovery_responses(partner);

	return &fixture;
}

ZTEST_F(usbc_discovery_no_drs, test_no_drs_no_discovery)
{
	/* Verify host command when VDOs are present. */
	struct ec_response_typec_status status =
		host_cmd_typec_status(TEST_PORT);
	uint8_t response_buffer[EC_LPC_HOST_PACKET_SIZE];
	struct ec_response_typec_discovery *discovery =
		(struct ec_response_typec_discovery *)response_buffer;

	/* Verify port partner does not think it's configured for DisplayPort */
	zassert_false(fixture->partner.displayport_configured);

	/* Verify TCPM reports discovery done with no data from partner. */
	zassert_true(status.events & PD_STATUS_EVENT_SOP_DISC_DONE);
	zassert_true(status.events & PD_STATUS_EVENT_SOP_PRIME_DISC_DONE);

	host_cmd_typec_discovery(TEST_PORT, TYPEC_PARTNER_SOP, response_buffer,
				 sizeof(response_buffer));
	zassert_equal(discovery->identity_count, 0,
		      "Expected 0 identity VDOs, got %d",
		      discovery->identity_count);

	/* Verify TCPM does not notify AP of discovery done again. */
	host_cmd_typec_control_clear_events(
		TEST_PORT, PD_STATUS_EVENT_SOP_DISC_DONE |
				   PD_STATUS_EVENT_SOP_PRIME_DISC_DONE);
	k_sleep(K_MSEC(100));
	status = host_cmd_typec_status(TEST_PORT);
	zassert_false(status.events & (PD_STATUS_EVENT_SOP_DISC_DONE |
				       PD_STATUS_EVENT_SOP_PRIME_DISC_DONE));
}

ZTEST_SUITE(usbc_discovery_no_drs, drivers_predicate_post_main,
	    usbc_alt_mode_no_drs_setup, usbc_alt_mode_before,
	    usbc_alt_mode_after, NULL);
