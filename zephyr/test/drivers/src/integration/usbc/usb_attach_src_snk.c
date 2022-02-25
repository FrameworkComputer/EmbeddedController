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

#define DEFAULT_VBUS_MV 5000

/* Determined by CONFIG_PLATFORM_EC_USB_PD_PULLUP */
#define DEFAULT_VBUS_SRC_PORT_MA 1500

/* SRC TCPCI Emulator attaches as TYPEC_CC_VOLT_RP_3_0 */
#define DEFAULT_VBUS_SNK_PORT_MA 3000

#define DEFAULT_SINK_SENT_TO_SOURCE_CAP_COUNT 1
#define DEFAULT_SOURCE_SENT_TO_SINK_CAP_COUNT 1

struct emul_state {
	/* TODO(b/217737667): Remove driver specific code. */
	const struct emul *tcpci_generic_emul;
	const struct emul *tcpci_ps8xxx_emul;
	const struct emul *charger_isl923x_emul;
	struct tcpci_src_emul my_src;
	struct tcpci_snk_emul my_snk;
};

struct integration_usb_attach_src_then_snk_fixture {
	struct emul_state *my_emulator_state;
};

struct integration_usb_attach_snk_then_src_fixture {
	struct emul_state *my_emulator_state;
};

static void *integration_usb_src_snk_setup(void)
{
	const struct emul *tcpci_emul =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL));
	const struct emul *tcpci_emul2 =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL2));
	const struct emul *charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));

	static struct emul_state emul_state;
	static struct integration_usb_attach_src_then_snk_fixture fixture_state;

	/* Setting these are required because compiler believes these values are
	 * not compile time constants.
	 */
	/*
	 * TODO(b/217758708): emuls should be identified at compile-time.
	 */
	emul_state.tcpci_generic_emul = tcpci_emul;
	emul_state.tcpci_ps8xxx_emul = tcpci_emul2;
	emul_state.charger_isl923x_emul = charger_emul;
	fixture_state.my_emulator_state = &emul_state;

	/*
	 * TODO(b/221288815): TCPCI config flags should be compile-time
	 * constants
	 * TODO(b/209907615): Verify TCPCI Rev2
	 */
	/* Turn TCPCI rev 2 off */
	tcpc_config[SNK_PORT].flags = tcpc_config[SNK_PORT].flags &
				      ~TCPC_FLAGS_TCPCI_REV2_0;
	tcpc_config[SRC_PORT].flags = tcpc_config[SRC_PORT].flags &
				      ~TCPC_FLAGS_TCPCI_REV2_0;

	return &fixture_state;
}

static void attach_src_snk_common_before(struct emul_state *my_emul_state)
{
	const struct emul *tcpci_emul_src = my_emul_state->tcpci_generic_emul;
	const struct emul *tcpci_emul_snk = my_emul_state->tcpci_ps8xxx_emul;
	const struct emul *charger_emul = my_emul_state->charger_isl923x_emul;

	/* Reset vbus to 0mV */
	/* TODO(b/217737667): Remove driver specific code. */
	isl923x_emul_set_adc_vbus(charger_emul, 0);

	zassume_ok(tcpc_config[SNK_PORT].drv->init(SNK_PORT), NULL);
	/*
	 * Arbitrary FW ver. The emulator should really be setting this
	 * during its init.
	 */
	tcpci_emul_set_reg(tcpci_emul_snk, PS8XXX_REG_FW_REV, 0x31);

	zassume_ok(tcpc_config[SRC_PORT].drv->init(SRC_PORT), NULL);

	pd_set_suspend(SNK_PORT, false);
	pd_set_suspend(SRC_PORT, false);

	/* Reset to disconnected state. */
	zassume_ok(tcpci_emul_disconnect_partner(tcpci_emul_src), NULL);
	zassume_ok(tcpci_emul_disconnect_partner(tcpci_emul_snk), NULL);

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();
}

static void attach_src_snk_common_after(struct emul_state *my_emul_state)
{
	const struct emul *tcpci_generic_emul =
		my_emul_state->tcpci_generic_emul;
	const struct emul *tcpci_ps8xxx_emul = my_emul_state->tcpci_ps8xxx_emul;
	const struct emul *charger_emul = my_emul_state->charger_isl923x_emul;

	tcpci_emul_disconnect_partner(tcpci_generic_emul);
	tcpci_emul_disconnect_partner(tcpci_ps8xxx_emul);

	/* Give time to actually disconnect */
	k_sleep(K_SECONDS(1));

	/* Reset vbus to 0mV */
	/* TODO(b/217737667): Remove driver specific code. */
	isl923x_emul_set_adc_vbus(charger_emul, 0);
}

static void attach_emulated_snk(struct emul_state *my_emul_state)
{
	const struct emul *tcpci_emul_snk = my_emul_state->tcpci_ps8xxx_emul;
	struct tcpci_snk_emul *my_snk = &my_emul_state->my_snk;

	/* Attach emulated sink */
	tcpci_snk_emul_init(my_snk);
	tcpci_emul_set_rev(tcpci_emul_snk, TCPCI_EMUL_REV1_0_VER1_0);
	zassume_ok(tcpci_snk_emul_connect_to_tcpci(
			   &my_snk->data, &my_snk->common_data, &my_snk->ops,
			   tcpci_emul_snk),
		   NULL);

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));
}

static void attach_emulated_src(struct emul_state *my_emul_state)
{
	const struct emul *tcpci_emul_src = my_emul_state->tcpci_generic_emul;
	const struct emul *charger_emul = my_emul_state->charger_isl923x_emul;
	struct tcpci_src_emul *my_src = &my_emul_state->my_src;

	/* Attach emulated charger. */
	tcpci_src_emul_init(my_src);
	tcpci_emul_set_rev(tcpci_emul_src, TCPCI_EMUL_REV1_0_VER1_0);
	zassume_ok(tcpci_src_emul_connect_to_tcpci(
			   &my_src->data, &my_src->common_data, &my_src->ops,
			   tcpci_emul_src),
		   NULL);
	isl923x_emul_set_adc_vbus(charger_emul, DEFAULT_VBUS_MV);
}

static void integration_usb_attach_snk_then_src_before(void *state)
{
	const struct integration_usb_attach_src_then_snk_fixture *fixture =
		state;
	struct emul_state *my_state = fixture->my_emulator_state;

	attach_src_snk_common_before(my_state);

	/* 1) Attach SINK */
	attach_emulated_snk(my_state);

	/* Wait for PD negotiation */
	k_sleep(K_SECONDS(10));

	/* 2) Attach SOURCE */
	attach_emulated_src(my_state);

	/* Wait for PD negotiation */
	k_sleep(K_SECONDS(10));
}

static void integration_usb_attach_src_then_snk_before(void *state)
{
	const struct integration_usb_attach_src_then_snk_fixture *fixture =
		state;
	struct emul_state *my_state = fixture->my_emulator_state;

	attach_src_snk_common_before(my_state);

	/* 1) Attach SOURCE */
	attach_emulated_src(my_state);

	/* Wait for PD negotiation */
	k_sleep(K_SECONDS(10));

	/* 2) Attach SINK */
	attach_emulated_snk(my_state);

	/* Wait for PD negotiation */
	k_sleep(K_SECONDS(10));
}

static void integration_usb_attach_src_then_snk_after(void *state)
{
	const struct integration_usb_attach_src_then_snk_fixture *fixture =
		state;

	attach_src_snk_common_after(fixture->my_emulator_state);
}

static void integration_usb_attach_snk_then_src_after(void *state)
{
	const struct integration_usb_attach_snk_then_src_fixture *fixture =
		state;

	attach_src_snk_common_after(fixture->my_emulator_state);
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

	zassert_equal(response.meas.voltage_max, DEFAULT_VBUS_MV,
		      "Charging at VBUS %dmV, but PD reports %dmV",
		      DEFAULT_VBUS_MV, response.meas.voltage_max);

	zassert_within(response.meas.voltage_now, DEFAULT_VBUS_MV,
		       DEFAULT_VBUS_MV / 10,
		       "Actually charging at VBUS %dmV, but PD reports %dmV",
		       DEFAULT_VBUS_MV, response.meas.voltage_now);

	zassert_equal(response.meas.current_max, DEFAULT_VBUS_SNK_PORT_MA,
		      "Charging at VBUS max %dmA, but PD reports %dmA",
		      DEFAULT_VBUS_SNK_PORT_MA, response.meas.current_max);

	zassert_true(response.meas.current_lim >= DEFAULT_VBUS_SNK_PORT_MA,
		     "Charging at VBUS max %dmA, but PD current limit %dmA",
		     DEFAULT_VBUS_SNK_PORT_MA, response.meas.current_lim);

	zassert_equal(response.max_power,
		      DEFAULT_VBUS_MV * DEFAULT_VBUS_SNK_PORT_MA,
		      "Charging up to %duW, PD max power %duW",
		      DEFAULT_VBUS_MV * DEFAULT_VBUS_SNK_PORT_MA,
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

	/* TODO(b/209907615): Confirm measure value requirements */
	zassert_within(response.meas.voltage_now, DEFAULT_VBUS_MV,
		       DEFAULT_VBUS_MV / 10,
		       "Expected Charging at VBUS %dmV, but PD reports %dmV",
		       DEFAULT_VBUS_MV, response.meas.voltage_now);

	zassume_equal(response.meas.current_max, DEFAULT_VBUS_SRC_PORT_MA,
		      "Charging at VBUS max %dmA, but PD reports %dmA",
		      DEFAULT_VBUS_SRC_PORT_MA, response.meas.current_max);

	/* Note: We are the source so we skip checking: */
	/* meas.voltage_max */
	/* max_power */
	/* current limit */
}

ZTEST_F(integration_usb_attach_snk_then_src, verify_snk_port_pd_info)
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
	zassert_equal(response.meas.voltage_max, DEFAULT_VBUS_MV,
		      "Charging at VBUS %dmV, but PD reports %dmV",
		      DEFAULT_VBUS_MV, response.meas.voltage_max);

	zassert_within(response.meas.voltage_now, DEFAULT_VBUS_MV,
		       DEFAULT_VBUS_MV / 10,
		       "Actually charging at VBUS %dmV, but PD reports %dmV",
		       DEFAULT_VBUS_MV, response.meas.voltage_now);

	zassert_equal(response.meas.current_max, DEFAULT_VBUS_SNK_PORT_MA,
		      "Charging at VBUS max %dmA, but PD reports %dmA",
		      DEFAULT_VBUS_SNK_PORT_MA, response.meas.current_max);

	zassert_true(response.meas.current_lim >= DEFAULT_VBUS_SNK_PORT_MA,
		     "Charging at VBUS max %dmA, but PD current limit %dmA",
		     DEFAULT_VBUS_SNK_PORT_MA, response.meas.current_lim);

	zassert_equal(response.max_power,
		      DEFAULT_VBUS_MV * DEFAULT_VBUS_SNK_PORT_MA,
		      "Charging up to %duW, PD max power %duW",
		      DEFAULT_VBUS_MV * DEFAULT_VBUS_SNK_PORT_MA,
		      response.max_power);
}

ZTEST_F(integration_usb_attach_snk_then_src, verify_src_port_pd_info)
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
	zassert_within(response.meas.voltage_now, DEFAULT_VBUS_MV,
		       DEFAULT_VBUS_MV / 10,
		       "Expected Charging at VBUS %dmV, but PD reports %dmV",
		       DEFAULT_VBUS_MV, response.meas.voltage_now);

	zassume_equal(response.meas.current_max, DEFAULT_VBUS_SRC_PORT_MA,
		      "Charging at VBUS max %dmA, but PD reports %dmA",
		      DEFAULT_VBUS_SRC_PORT_MA, response.meas.current_max);

	/* Note: We are the source so we skip checking: */
	/* meas.voltage_max */
	/* max_power */
	/* current limit */
}

ZTEST_F(integration_usb_attach_src_then_snk, verify_snk_port_typec_status)
{
	struct ec_response_typec_status response =
		host_cmd_typec_status(SNK_PORT);

	zassert_true(response.pd_enabled, "Source attached but PD disabled");

	zassert_true(response.dev_connected,
		     "Source attached but device disconnected");

	zassert_true(response.sop_connected,
		     "Source attached but not SOP capable");

	zassert_equal(response.source_cap_count,
		      DEFAULT_SOURCE_SENT_TO_SINK_CAP_COUNT,
		      "Source received %d source PDOs",
		      response.source_cap_count);

	/* The source emulator is being attached to a sink port (our policy
	 * engine) so it does not send any sink caps, so sink port received no
	 * sink caps.
	 */
	zassert_equal(response.sink_cap_count, 0, "Port received %d sink PDOs",
		      response.sink_cap_count);

	zassert_equal(response.power_role, PD_ROLE_SINK,
		      "Source attached, but TCPM power role is %d",
		      response.power_role);
}

ZTEST_F(integration_usb_attach_src_then_snk, verify_src_port_typec_status)
{
	struct ec_response_typec_status response =
		host_cmd_typec_status(SRC_PORT);

	zassert_true(response.pd_enabled, "Sink attached but PD disabled");

	zassert_true(response.dev_connected,
		     "Sink attached but device disconnected");

	zassert_true(response.sop_connected,
		     "Sink attached but not SOP capable");

	/* The sink emulator is being attached to a source port (our policy
	 * engine) so it does not send any sink caps, so source port received no
	 * sink caps.
	 */
	zassert_equal(response.source_cap_count, 0,
		      "Port received %d source PDOs",
		      response.source_cap_count);

	zassert_equal(response.sink_cap_count,
		      DEFAULT_SINK_SENT_TO_SOURCE_CAP_COUNT,
		      "Port received %d sink PDOs", response.sink_cap_count);

	zassert_equal(response.power_role, PD_ROLE_SOURCE,
		      "Sink attached, but TCPM power role is %d",
		      response.power_role);
}

ZTEST_SUITE(integration_usb_attach_src_then_snk, drivers_predicate_post_main,
	    integration_usb_src_snk_setup,
	    integration_usb_attach_src_then_snk_before,
	    integration_usb_attach_src_then_snk_after, NULL);

ZTEST_SUITE(integration_usb_attach_snk_then_src, drivers_predicate_post_main,
	    integration_usb_src_snk_setup,
	    integration_usb_attach_snk_then_src_before,
	    integration_usb_attach_snk_then_src_after, NULL);
