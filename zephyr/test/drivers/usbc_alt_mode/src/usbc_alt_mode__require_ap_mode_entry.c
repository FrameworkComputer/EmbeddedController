/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "test/drivers/utils.h"
#include "test_usbc_alt_mode.h"
#include "usb_pd_vdo.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <gpio.h>

/* Tests that require CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY enabled */

ZTEST_F(usbc_alt_mode, test_verify_displayport_mode_reentry)
{
	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		ztest_test_skip();
	}

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	/* DPM configures the partner on DP mode entry */
	/* Verify port partner thinks its configured for DisplayPort */
	zassert_true(fixture->partner.displayport_configured);

	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_exit_modes(TEST_PORT);
	k_sleep(K_SECONDS(1));
	zassert_false(fixture->partner.displayport_configured);
	/*
	 * As with initial entry, for an AMA partner, the TCPM should not issue
	 * a Data Reset.
	 */
	verify_data_reset_msg(&fixture->partner, false);

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));
	zassert_true(fixture->partner.displayport_configured);

	/*
	 * Verify that DisplayPort is the active alternate mode by checking our
	 * MUX settings
	 */
	struct ec_response_typec_status status;

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_PD_MUX_DP_ENABLED),
		      USB_PD_MUX_DP_ENABLED, "Failed to see DP mux set");
}

ZTEST_F(usbc_alt_mode, test_verify_mode_exit_via_pd_host_cmd)
{
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_hpd);

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		ztest_test_skip();
	}

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	/*
	 * Set HPD so we can see it clear on Exit
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
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);

	host_cmd_typec_control_exit_modes(TEST_PORT);
	k_sleep(K_SECONDS(1));
	zassert_false(fixture->partner.displayport_configured);
	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 0);

	/*
	 * Verify that DisplayPort is no longer active by checking our
	 * MUX settings
	 */
	struct ec_response_typec_status status;

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_PD_MUX_DP_ENABLED), 0);
}

ZTEST_F(usbc_alt_mode, test_verify_early_status_hpd_set)
{
	const struct gpio_dt_spec *gpio =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_hpd);

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		ztest_test_skip();
	}

	/*
	 * Tweak our DP:Status reply to set HPD and ensure it's transmitted
	 * through our HPD GPIO
	 */
	fixture->partner.dp_status_vdm[VDO_INDEX_HDR + 1] =
		/* Mainly copied from hoho */
		VDO_DP_STATUS(0, /* IRQ_HPD */
			      true, /* HPD_HI|LOW - Changed*/
			      0, /* request exit DP */
			      0, /* request exit USB */
			      0, /* MF pref */
			      true, /* DP Enabled */
			      0, /* power low e.g. normal */
			      0x2 /* Connected as Sink */);

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	zassert_equal(gpio_emul_output_get(gpio->port, gpio->pin), 1);
}

ZTEST_F(usbc_alt_mode_custom_discovery, test_hub_no_usb4_no_alt_mode)
{
	struct tcpci_partner_data *partner = &fixture->partner;

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		ztest_test_skip();
	}

	/* If the partner has Hub product type but does not set the USB4 device
	 * capability or Alternate Modes field in its Discover Identity
	 * response, the TCPM should not send Data Reset during mode entry.
	 */
	partner->identity_vdm[VDO_INDEX_IDH] = VDO_IDH_REV30(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_HUB,
		/* modal operation */ true, /* DFP type */ 0,
		/* connector type */ 3, USB_VID_GOOGLE);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] = VDO_UFP1(
		/* Capability */ VDO_UFP1_CAPABILITY_USB32,
		/* connector type */ 0, /* alternate modes */ 0, /* speed */ 1);
	connect_partner_to_port(fixture->tcpci_emul, fixture->charger_emul,
				&fixture->partner, &fixture->src_ext);

	tcpci_partner_common_clear_logged_msgs(&fixture->partner);
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	verify_data_reset_msg(&fixture->partner, false);
}

ZTEST_F(usbc_alt_mode_custom_discovery, test_hub_no_ufp_vdo)
{
	struct tcpci_partner_data *partner = &fixture->partner;

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		ztest_test_skip();
	}

	partner->identity_vdm[VDO_INDEX_IDH] = VDO_IDH_REV30(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_HUB,
		/* modal operation */ true, /* DFP type */ 0,
		/* connector type */ 3, USB_VID_GOOGLE);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] = VDO_UFP1(
		/* Capability */ VDO_UFP1_CAPABILITY_USB4,
		/* connector type */ 0,
		/* alternate modes */ VDO_UFP1_ALT_MODE_RECONFIGURE,
		/* speed */ 1);
	connect_partner_to_port(fixture->tcpci_emul, fixture->charger_emul,
				&fixture->partner, &fixture->src_ext);
	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));
	disconnect_partner_from_port(fixture->tcpci_emul,
				     fixture->charger_emul);
	k_sleep(K_SECONDS(1));

	/* If the partner has Hub product type but does not send a UFP VDO at
	 * all, the TCPM should not send Data Reset during mode entry. This
	 * should be true even if a partner supporting Data Reset was previously
	 * connected. See b/304935541.
	 */
	partner->identity_vdm[VDO_INDEX_IDH] = VDO_IDH_REV30(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_HUB,
		/* modal operation */ true, /* DFP type */ 0,
		/* connector type */ 3, USB_VID_GOOGLE);
	partner->identity_vdos = VDO_INDEX_PRODUCT + 1;
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] = VDO_UFP1(
		/* Capability */ VDO_UFP1_CAPABILITY_USB32,
		/* connector type */ 0, /* alternate modes */ 0, /* speed */ 1);
	connect_partner_to_port(fixture->tcpci_emul, fixture->charger_emul,
				&fixture->partner, &fixture->src_ext);

	tcpci_partner_common_clear_logged_msgs(&fixture->partner);
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	verify_data_reset_msg(&fixture->partner, false);
}

ZTEST_F(usbc_alt_mode_custom_discovery, test_hub_usb4_no_alt_mode)
{
	struct tcpci_partner_data *partner = &fixture->partner;

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		ztest_test_skip();
	}

	/* If the partner has Hub product type and USB4 device capability in its
	 * Discover Identity response, the TCPM should send Data Reset during
	 * mode entry.
	 */
	partner->identity_vdm[VDO_INDEX_IDH] = VDO_IDH_REV30(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_HUB,
		/* modal operation */ true, /* DFP type */ 0,
		/* connector type */ 3, USB_VID_GOOGLE);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] = VDO_UFP1(
		/* Capability */ VDO_UFP1_CAPABILITY_USB4,
		/* connector type */ 0, /* alternate modes */ 0, /* speed */ 1);
	connect_partner_to_port(fixture->tcpci_emul, fixture->charger_emul,
				&fixture->partner, &fixture->src_ext);

	tcpci_partner_common_clear_logged_msgs(&fixture->partner);
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	verify_data_reset_msg(&fixture->partner, true);
}

ZTEST_F(usbc_alt_mode_custom_discovery, test_hub_alt_mode_no_usb4)
{
	struct tcpci_partner_data *partner = &fixture->partner;

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		ztest_test_skip();
	}

	/* If the partner has Hub product type and Alternate Modes support in
	 * its Discover Identity response, the TCPM should send Data Reset
	 * during mode entry.
	 */
	partner->identity_vdm[VDO_INDEX_IDH] = VDO_IDH_REV30(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_HUB,
		/* modal operation */ true, /* DFP type */ 0,
		/* connector type */ 3, USB_VID_GOOGLE);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] = VDO_UFP1(
		/* Capability */ VDO_UFP1_CAPABILITY_USB32,
		/* connector type */ 0,
		/* alternate modes */ VDO_UFP1_ALT_MODE_RECONFIGURE,
		/* speed */ 1);
	connect_partner_to_port(fixture->tcpci_emul, fixture->charger_emul,
				&fixture->partner, &fixture->src_ext);

	tcpci_partner_common_clear_logged_msgs(&fixture->partner);
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	verify_data_reset_msg(&fixture->partner, true);
}

ZTEST_F(usbc_alt_mode_custom_discovery, test_peripheral_usb4_no_alt_mode)
{
	struct tcpci_partner_data *partner = &fixture->partner;

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		ztest_test_skip();
	}

	/* If the partner has Peripheral product type and USB4 device capability
	 * in its Discover Identity response, the TCPM should send Data Reset
	 * during mode entry.
	 */
	partner->identity_vdm[VDO_INDEX_IDH] = VDO_IDH_REV30(
		/* USB host */ false, /* USB device */ false, IDH_PTYPE_PERIPH,
		/* modal operation */ true, /* DFP type */ 0,
		/* connector type */ 3, USB_VID_GOOGLE);
	partner->identity_vdm[VDO_INDEX_PTYPE_UFP1_VDO] = VDO_UFP1(
		/* Capability */ VDO_UFP1_CAPABILITY_USB4,
		/* connector type */ 0, /* alternate modes */ 0, /* speed */ 1);
	connect_partner_to_port(fixture->tcpci_emul, fixture->charger_emul,
				&fixture->partner, &fixture->src_ext);

	tcpci_partner_common_clear_logged_msgs(&fixture->partner);
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	verify_data_reset_msg(&fixture->partner, true);
}
