/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>
#include <drivers/gpio/gpio_emul.h>

#include "battery_smart.h"
#include "ec_commands.h"
#include "ec_tasks.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_ps8xxx.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "host_command.h"
#include "stubs.h"
#include "tcpm/tcpci.h"
#include "test/usb_pe.h"
#include "utils.h"
#include "test_state.h"

#define TCPCI_EMUL_LABEL DT_NODELABEL(tcpci_emul)
#define TCPCI_EMUL_LABEL2 DT_NODELABEL(tcpci_ps8xxx_emul)

#define BATTERY_ORD DT_DEP_ORD(DT_NODELABEL(battery))

#define GPIO_AC_OK_PATH DT_PATH(named_gpios, acok_od)
#define GPIO_AC_OK_PIN DT_GPIO_PIN(GPIO_AC_OK_PATH, gpios)

#define GPIO_BATT_PRES_ODL_PATH DT_PATH(named_gpios, ec_batt_pres_odl)
#define GPIO_BATT_PRES_ODL_PORT DT_GPIO_PIN(GPIO_BATT_PRES_ODL_PATH, gpios)

static void integration_usb_before(void *state)
{
	const struct emul *tcpci_emul =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL));
	const struct emul *tcpci_emul2 =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL2));
	struct i2c_emul *i2c_emul;
	struct sbat_emul_bat_data *bat;
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_AC_OK_PATH, gpios));

	ARG_UNUSED(state);
	zassert_ok(tcpc_config[0].drv->init(0), NULL);
	if (IS_ENABLED(CONFIG_BUG209907615)) {
		/* Fails USB Mux tests */
		/*
		 * Arbitrary FW ver. The emulator should really be setting this
		 * during its init.
		 */
		tcpci_emul_set_reg(tcpci_emul2, PS8XXX_REG_FW_REV, 0x31);
		zassert_ok(tcpc_config[1].drv->init(1), NULL);
	}
	tcpci_emul_set_rev(tcpci_emul, TCPCI_EMUL_REV1_0_VER1_0);
	pd_set_suspend(0, 0);
	pd_set_suspend(1, 0);
	/* Reset to disconnected state. */
	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul), NULL);
	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul2), NULL);

	/* Battery defaults to charging, so reset to not charging. */
	i2c_emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(i2c_emul);
	bat->cur = -5;

	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_AC_OK_PIN, 0), NULL);
}

static void integration_usb_after(void *state)
{
	const struct emul *tcpci_emul =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL));
	const struct emul *tcpci_emul2 =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL2));
	ARG_UNUSED(state);

	/* TODO: This function should trigger gpios to signal there is nothing
	 * attached to the port.
	 */
	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul), NULL);
	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul2), NULL);
	/* Give time to actually disconnect */
	k_sleep(K_SECONDS(1));
}

/* Check the results of EC_CMD_CHARGE_STATE against expected charger properties.
 */
static void check_charge_state(int chgnum, bool attached)
{
	struct ec_params_charge_state charge_params = {
		.chgnum = chgnum, .cmd = CHARGE_STATE_CMD_GET_STATE};
	struct ec_response_charge_state charge_response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
			EC_CMD_CHARGE_STATE, 0, charge_response, charge_params);

	zassert_ok(host_command_process(&args), "Failed to get charge state");
	zassert_equal(charge_response.get_state.ac, attached,
			"USB default but AC absent");
	/* The charging voltage and current are not directly related to the PD
	 * charging and current, but they should be positive if the battery is
	 * charging.
	 */
	if (attached) {
		zassert_true(charge_response.get_state.chg_voltage > 0,
				"Battery charging voltage %dmV",
				charge_response.get_state.chg_voltage);
		zassert_true(charge_response.get_state.chg_current > 0,
				"Battery charging current %dmA",
				charge_response.get_state.chg_current);
	}
}

/* Check the results of EC_CMD_TYPEC_STATUS against expected charger properties.
 */
static void check_typec_status(int port, enum pd_power_role port_role,
		enum usb_chg_type charger_type, int source_cap_count)
{
	struct ec_params_typec_status typec_params = {.port = port};
	struct ec_response_typec_status typec_response;
	struct host_cmd_handler_args typec_args =  BUILD_HOST_COMMAND(
			EC_CMD_TYPEC_STATUS, 0, typec_response, typec_params);

	zassert_ok(host_command_process(&typec_args),
			"Failed to get Type-C state");
	zassert_true(typec_response.pd_enabled ==
			(charger_type == USB_CHG_TYPE_PD),
			"Charger attached but PD disabled");
	zassert_true(typec_response.dev_connected ==
			(charger_type != USB_CHG_TYPE_NONE),
			"Charger attached but device disconnected");
	zassert_true(typec_response.sop_connected ==
			(charger_type == USB_CHG_TYPE_PD),
			"Charger attached but not SOP capable");
	zassert_equal(typec_response.source_cap_count, source_cap_count,
			"Charger has %d source PDOs",
			typec_response.source_cap_count);
	zassert_equal(typec_response.power_role, port_role,
			"Charger attached, but TCPM power role is %d",
			typec_response.power_role);
}

/* Check the results of EC_CMD_USB_PD_POWER_INFO against expected charger
 * properties.
 */
static void check_usb_pd_power_info(int port, enum usb_power_roles role,
		enum usb_chg_type charger_type, int charge_voltage_mv,
		int charge_current_ma)
{
	struct ec_params_usb_pd_power_info power_info_params = {.port = port};
	struct ec_response_usb_pd_power_info power_info_response;
	struct host_cmd_handler_args power_info_args =  BUILD_HOST_COMMAND(
			EC_CMD_USB_PD_POWER_INFO, 0, power_info_response,
			power_info_params);
	struct usb_chg_measures *meas = &power_info_response.meas;

	zassert_ok(host_command_process(&power_info_args),
			"Failed to get PD power info");
	zassert_equal(power_info_response.role, role,
			"Power role %d, but PD reports role %d",
			role, power_info_response.role);
	zassert_equal(power_info_response.type, charger_type,
			"Charger type %d, but PD reports type %d",
			charger_type, power_info_response.type);
	/* The measurements in this response are denoted in mV, mA, and mW. */
	zassert_equal(meas->voltage_max, charge_voltage_mv,
			"Charging at VBUS %dmV, but PD reports %dmV",
			charge_voltage_mv, meas->voltage_max);
	zassert_within(meas->voltage_now, charge_voltage_mv,
			charge_voltage_mv / 10,
			"Actually charging at VBUS %dmV, but PD reports %dmV",
			charge_voltage_mv, meas->voltage_now);
	zassert_equal(meas->current_max, charge_current_ma,
			"Charging at VBUS max %dmA, but PD reports %dmA",
			charge_current_ma, meas->current_max);
	zassert_true(meas->current_lim >= charge_current_ma,
			"Charging at VBUS max %dmA, but PD current limit %dmA",
			charge_current_ma, meas->current_lim);
	zassert_equal(power_info_response.max_power,
			charge_voltage_mv * charge_current_ma,
			"Charging up to %duW, PD max power %duW",
			charge_voltage_mv * charge_current_ma,
			power_info_response.max_power);
}

ZTEST(integration_usb, test_attach_5v_pd_charger)
{
	const struct emul *tcpci_emul =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL));
	const struct emul *charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));
	struct i2c_emul *i2c_emul;
	uint16_t battery_status;
	struct tcpci_src_emul my_charger;
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_AC_OK_PATH, gpios));

	/* Verify battery not charging. */
	i2c_emul = sbat_emul_get_ptr(BATTERY_ORD);
	zassert_ok(sbat_emul_get_word_val(i2c_emul, SB_BATTERY_STATUS,
					  &battery_status),
		   NULL);
	zassert_not_equal(battery_status & STATUS_DISCHARGING, 0,
			  "Battery is not discharging: %d", battery_status);

	/* TODO? Send host command to verify PD_ROLE_DISCONNECTED. */

	/* Attach emulated charger. The default PDO offers 5V 3A. */
	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_AC_OK_PIN, 1), NULL);
	tcpci_src_emul_init(&my_charger);
	zassert_ok(tcpci_src_emul_connect_to_tcpci(&my_charger.data,
						   &my_charger.common_data,
						   &my_charger.ops, tcpci_emul),
		   NULL);
	isl923x_emul_set_adc_vbus(charger_emul, 5000);

	/* Wait for current ramp. */
	k_sleep(K_SECONDS(10));

	/* Verify battery charging. */
	zassert_ok(sbat_emul_get_word_val(i2c_emul, SB_BATTERY_STATUS,
					  &battery_status),
		   NULL);
	zassert_equal(battery_status & STATUS_DISCHARGING, 0,
		      "Battery is discharging: %d", battery_status);

	/* Check the charging voltage and current. Cross-check the PD state,
	 * the battery/charger state, and the active PDO as reported by the PD
	 * state.
	 */
	check_charge_state(0, true);
	check_typec_status(0, PD_ROLE_SINK, USB_CHG_TYPE_PD, 1);
	/* TODO(b/217394181): Refactor to direct assert calls */
	check_usb_pd_power_info(0, USB_PD_PORT_POWER_SINK, USB_CHG_TYPE_PD,
			5000, 3000);
}

ZTEST(integration_usb, test_attach_20v_pd_charger)
{
	const struct emul *tcpci_emul =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL));
	const struct emul *charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));
	struct i2c_emul *i2c_emul;
	uint16_t battery_status;
	struct tcpci_src_emul my_charger;
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_AC_OK_PATH, gpios));

	/* Attach emulated charger. Send Source Capabilities that offer 20V. Set
	 * the charger input voltage to ~18V (the highest voltage it supports).
	 */
	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_AC_OK_PIN, 1), NULL);
	tcpci_src_emul_init(&my_charger);
	my_charger.data.pdo[1] =
		PDO_FIXED(20000, 3000, PDO_FIXED_UNCONSTRAINED);
	zassert_ok(tcpci_src_emul_connect_to_tcpci(&my_charger.data,
						   &my_charger.common_data,
						   &my_charger.ops, tcpci_emul),
		   NULL);
	isl923x_emul_set_adc_vbus(charger_emul, 20000);

	/* Wait for PD negotiation and current ramp.
	 * TODO(b/213906889): Check message timing and contents.
	 */
	k_sleep(K_SECONDS(10));

	/* Verify battery charging. */
	i2c_emul = sbat_emul_get_ptr(BATTERY_ORD);
	zassert_ok(sbat_emul_get_word_val(i2c_emul, SB_BATTERY_STATUS,
					  &battery_status),
		   NULL);
	zassert_equal(battery_status & STATUS_DISCHARGING, 0,
		      "Battery is discharging: %d", battery_status);

	/* Check the charging voltage and current. Cross-check the PD state,
	 * the battery/charger state, and the active PDO as reported by the PD
	 * state. The charging voltage and current are not directly related to
	 * the PD charging and current, but they should be positive if the
	 * battery is charging.
	 */
	check_charge_state(0, true);
	check_typec_status(0, PD_ROLE_SINK, USB_CHG_TYPE_PD, 2);

	/* TODO(b/217394181): Refactor to direct assert calls */
	check_usb_pd_power_info(0, USB_PD_PORT_POWER_SINK, USB_CHG_TYPE_PD,
			20000, 3000);
}

ZTEST(integration_usb, test_attach_sink)
{
	const struct emul *tcpci_emul =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL));
	struct tcpci_snk_emul my_sink;

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	/* Attach emulated sink */
	tcpci_snk_emul_init(&my_sink);
	zassert_ok(tcpci_snk_emul_connect_to_tcpci(&my_sink.data,
						   &my_sink.common_data,
						   &my_sink.ops, tcpci_emul),
		   NULL);

	/* Wait for PD negotiation */
	k_sleep(K_SECONDS(10));

	/* Test if partner believe that PD negotiation is completed */
	zassert_true(my_sink.data.pd_completed, NULL);
	/*
	 * Test that SRC ready is achieved
	 * TODO: Change it to examining EC_CMD_TYPEC_STATUS
	 */
	zassert_equal(PE_SRC_READY, get_state_pe(USBC_PORT_C0), NULL);
}

ZTEST(integration_usb, test_attach_drp)
{
	const struct emul *tcpci_emul =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL));
	struct tcpci_drp_emul my_drp;

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	/* Attach emulated sink */
	tcpci_drp_emul_init(&my_drp);
	zassert_ok(tcpci_drp_emul_connect_to_tcpci(&my_drp.data,
						   &my_drp.src_data,
						   &my_drp.snk_data,
						   &my_drp.common_data,
						   &my_drp.ops, tcpci_emul),
		   NULL);

	/* Wait for PD negotiation */
	k_sleep(K_SECONDS(10));

	/*
	 * Test that SRC ready is achieved
	 * TODO: Change it to examining EC_CMD_TYPEC_STATUS
	 */
	zassert_equal(PE_SNK_READY, get_state_pe(USBC_PORT_C0), NULL);
}

ZTEST(integration_usb, test_attach_src_then_snk)
{
	const struct emul *tcpci_emul_src =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL));
	const struct emul *tcpci_emul_snk =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL2));
	struct tcpci_src_emul my_charger;
	struct tcpci_snk_emul my_sink;
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_AC_OK_PATH, gpios));
	struct ec_params_usb_pd_power_info params_c0 = { .port = 0 };
	struct ec_response_usb_pd_power_info response_c0;
	struct ec_params_usb_pd_power_info params_c1 = { .port = 1 };
	struct ec_response_usb_pd_power_info response_c1;
	struct host_cmd_handler_args args_c0 = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_USB_PD_POWER_INFO, 0, response_c0);
	struct host_cmd_handler_args args_c1 = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_USB_PD_POWER_INFO, 0, response_c1);

	args_c0.params = &params_c0;
	args_c1.params = &params_c1;

	/* 1) Attach SOURCE */

	/* Attach emulated charger. */
	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_AC_OK_PIN, 1), NULL);
	tcpci_src_emul_init(&my_charger);
	zassert_ok(tcpci_src_emul_connect_to_tcpci(
			   &my_charger.data, &my_charger.common_data,
			   &my_charger.ops, tcpci_emul_src),
		   NULL);

	/* Wait for current ramp. */
	k_sleep(K_SECONDS(10));

	/* 2) Attach SINK */

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	/* Attach emulated sink */
	tcpci_snk_emul_init(&my_sink);
	zassert_ok(tcpci_snk_emul_connect_to_tcpci(
			   &my_sink.data, &my_sink.common_data, &my_sink.ops,
			   tcpci_emul_snk),
		   NULL);

	/* Wait for PD negotiation */
	k_sleep(K_SECONDS(10));

	/* TODO(b/217394181): limit to value faking */
	if (IS_ENABLED(CONFIG_BUG209907615)) {
		/* Verify Default 5V and 3A */
		/* Fails on actual mV reported as it is way past max 5000 */
		/* TODO(b/217394181): Refactor to direct assert calls */
		check_usb_pd_power_info(0, USB_PD_PORT_POWER_SINK,
					USB_CHG_TYPE_PD, 5000, 3000);
	}

	/* TODO(b/217394181): limit to value faking */
	if (IS_ENABLED(CONFIG_BUG209907615)) {
		/*
		 * TODO(b/217394181): Refactor to direct assert calls
		 * TODO(b/209907615): Confirm measure value requirements
		 */
		check_usb_pd_power_info(0, USB_PD_PORT_POWER_SOURCE,
					USB_CHG_TYPE_PD, 5000, 3000);
	}
}

ZTEST_SUITE(integration_usb, drivers_predicate_post_main, NULL,
	    integration_usb_before, integration_usb_after, NULL);
