/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>
#include <drivers/gpio/gpio_emul.h>

#include "ec_commands.h"
#include "ec_tasks.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "emul/emul_isl923x.h"
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

#define SNK_PORT USBC_PORT_C0
#define SRC_PORT USBC_PORT_C1

#define TCPCI_EMUL_LABEL DT_NODELABEL(tcpci_emul)
#define TCPCI_EMUL_LABEL2 DT_NODELABEL(tcpci_ps8xxx_emul)

struct integration_usb_attach_src_then_snk_fixture {
	/* TODO(b/217737667): Remove driver specific code. */
	const struct emul *tcpci_generic_emul;
	const struct emul *tcpci_ps8xxx_emul;
	const struct emul *charger_isl923x_emul;
};

static void *integration_usb_src_snk_setup(void)
{
	const struct emul *tcpci_emul =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL));
	const struct emul *tcpci_emul2 =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL2));
	const struct emul *charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));

	static struct integration_usb_attach_src_then_snk_fixture emul_state;
	/* Setting these are required because compiler believes these values are
	 * not compile time constants.
	 */
	/*
	 * TODO(b/217758708): emuls should be identified at compile-time.
	 */
	emul_state.tcpci_generic_emul = tcpci_emul;
	emul_state.tcpci_ps8xxx_emul = tcpci_emul2;
	emul_state.charger_isl923x_emul = charger_emul;

	return &emul_state;
}

static void integration_usb_attach_src_then_snk_before(void *state)
{
	struct integration_usb_attach_src_then_snk_fixture *my_state = state;

	const struct emul *tcpci_emul_src = my_state->tcpci_generic_emul;
	const struct emul *tcpci_emul_snk = my_state->tcpci_ps8xxx_emul;
	const struct emul *charger_emul = my_state->charger_isl923x_emul;

	struct tcpci_src_emul my_charger;
	struct tcpci_snk_emul my_sink;

	/* Reset vbus to 0mV */
	/* TODO(b/217610871): Remove redundant test state cleanup */
	/* TODO(b/217737667): Remove driver specific code. */
	isl923x_emul_set_adc_vbus(charger_emul, 0);

	zassume_ok(tcpc_config[0].drv->init(0), NULL);
	/*
	 * Arbitrary FW ver. The emulator should really be setting this
	 * during its init.
	 */
	tcpci_emul_set_reg(tcpci_emul_snk, PS8XXX_REG_FW_REV, 0x31);
	zassume_ok(tcpc_config[1].drv->init(1), NULL);
	tcpci_emul_set_rev(tcpci_emul_src, TCPCI_EMUL_REV1_0_VER1_0);
	pd_set_suspend(0, 0);
	pd_set_suspend(1, 0);
	/* Reset to disconnected state. */
	zassume_ok(tcpci_emul_disconnect_partner(tcpci_emul_src), NULL);
	zassume_ok(tcpci_emul_disconnect_partner(tcpci_emul_snk), NULL);

	/* 1) Attach SOURCE */

	/* Attach emulated charger. */
	tcpci_src_emul_init(&my_charger);
	zassume_ok(tcpci_src_emul_connect_to_tcpci(
			   &my_charger.data, &my_charger.common_data,
			   &my_charger.ops, tcpci_emul_src),
		   NULL);
	isl923x_emul_set_adc_vbus(charger_emul, 5000);

	/* Wait for current ramp. */
	k_sleep(K_SECONDS(10));

	/* 2) Attach SINK */

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	/* Attach emulated sink */
	tcpci_snk_emul_init(&my_sink);

	zassume_ok(tcpci_snk_emul_connect_to_tcpci(
			   &my_sink.data, &my_sink.common_data, &my_sink.ops,
			   tcpci_emul_snk),
		   NULL);

	/* Wait for PD negotiation */
	k_sleep(K_SECONDS(10));
}

static void integration_usb_attach_src_then_snk_after(void *state)
{
	struct integration_usb_attach_src_then_snk_fixture *my_state = state;

	const struct emul *tcpci_generic_emul = my_state->tcpci_generic_emul;
	const struct emul *tcpci_ps8xxx_emul = my_state->tcpci_ps8xxx_emul;
	const struct emul *charger_emul = my_state->charger_isl923x_emul;

	tcpci_emul_disconnect_partner(tcpci_generic_emul);
	tcpci_emul_disconnect_partner(tcpci_ps8xxx_emul);

	/* Give time to actually disconnect */
	k_sleep(K_SECONDS(1));

	/* Reset vbus to 0mV */
	/* TODO(b/217737667): Remove driver specific code. */
	isl923x_emul_set_adc_vbus(charger_emul, 0);
}

ZTEST_F(integration_usb_attach_src_then_snk, verify_snk_port_pd_info)
{
	struct ec_params_usb_pd_power_info params = { .port = SNK_PORT };
	struct ec_response_usb_pd_power_info response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_USB_PD_POWER_INFO, 0, response, params);

	/* Assume */
	zassume_ok(host_command_process(&args), "Failed to get PD power info");

	/* Assert */
	zassert_equal(response.role, USB_PD_PORT_POWER_SINK,
		      "Power role %d, but PD reports role %d",
		      USB_PD_PORT_POWER_SINK, response.role);
	zassert_equal(response.type, USB_CHG_TYPE_PD,
		      "Charger type %d, but PD reports type %d",
		      USB_CHG_TYPE_PD, response.type);

	/* Verify Default 5V and 3A */
	zassert_equal(response.meas.voltage_max, 5000,
		      "Charging at VBUS %dmV, but PD reports %dmV", 5000,
		      response.meas.voltage_max);

	zassert_within(response.meas.voltage_now, 5000, 5000 / 10,
		       "Actually charging at VBUS %dmV, but PD reports %dmV",
		       5000, response.meas.voltage_now);

	zassert_equal(response.meas.current_max, 3000,
		      "Charging at VBUS max %dmA, but PD reports %dmA", 3000,
		      response.meas.current_max);

	zassert_true(response.meas.current_lim >= 3000,
		     "Charging at VBUS max %dmA, but PD current limit %dmA",
		     3000, response.meas.current_lim);

	zassert_equal(response.max_power, 5000 * 3000,
		      "Charging up to %duW, PD max power %duW", 5000 * 3000,
		      response.max_power);
}

ZTEST_F(integration_usb_attach_src_then_snk, verify_src_port_pd_info)
{
	struct ec_params_usb_pd_power_info params = { .port = SRC_PORT };
	struct ec_response_usb_pd_power_info response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_USB_PD_POWER_INFO, 0, response, params);

	/* Assume */
	zassume_ok(host_command_process(&args), "Failed to get PD power info");

	/* Assert */
	zassert_equal(response.role, USB_PD_PORT_POWER_SOURCE,
		      "Power role %d, but PD reports role %d", PD_ROLE_SOURCE,
		      response.role);

	zassert_equal(response.type, USB_CHG_TYPE_NONE,
		      "Charger type %d, but PD reports type %d",
		      USB_CHG_TYPE_NONE, response.type);

	/* Verify Default 5V and 3A */
	/* TODO(b/209907615): Confirm measure value requirements */
	zassert_within(response.meas.voltage_now, 5000, 5000 / 10,
		       "Expected Charging at VBUS %dmV, but PD reports %dmV",
		       5000, response.meas.voltage_now);

	zassume_equal(response.meas.current_max, 3000,
		      "Charging at VBUS max %dmA, but PD reports %dmA", 3000,
		      response.meas.current_max);

	/* Note: We are the source so we skip checking: */
	/* meas.voltage_max */
	/* max_power */
	/* current limit */
}

ZTEST_SUITE(integration_usb_attach_src_then_snk, drivers_predicate_post_main,
	    integration_usb_src_snk_setup,
	    integration_usb_attach_src_then_snk_before,
	    integration_usb_attach_src_then_snk_after, NULL);
