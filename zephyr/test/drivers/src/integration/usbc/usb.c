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
	const struct emul *charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));
	/* Reset vbus to 0mV */
	/* TODO(b/217610871): Remove redundant test state cleanup */
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	struct i2c_emul *i2c_emul;
	struct sbat_emul_bat_data *bat;
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_AC_OK_PATH, gpios));

	ARG_UNUSED(state);
	/*
	 * TODO(b/217755888): Refactor to using assume API
	 */
	zassert_ok(tcpc_config[0].drv->init(0), NULL);
	zassert_ok(tcpc_config[1].drv->init(1), NULL);
	tcpci_emul_set_rev(tcpci_emul, TCPCI_EMUL_REV1_0_VER1_0);
	pd_set_suspend(0, 0);
	pd_set_suspend(1, 0);
	/* Reset to disconnected state. */
	/*
	 * TODO(b/217755888): Refactor to using assume API
	 */
	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul), NULL);
	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul2), NULL);

	/* Battery defaults to charging, so reset to not charging. */
	i2c_emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(i2c_emul);
	bat->cur = -5;

	/*
	 * TODO(b/217755888): Refactor to using assume API
	 */
	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_AC_OK_PIN, 0), NULL);
}

static void integration_usb_after(void *state)
{
	const struct emul *tcpci_emul =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL));
	const struct emul *tcpci_emul2 =
		emul_get_binding(DT_LABEL(TCPCI_EMUL_LABEL2));
	const struct emul *charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));
	ARG_UNUSED(state);

	/* TODO: This function should trigger gpios to signal there is nothing
	 * attached to the port.
	 */
	/*
	 * TODO(b/217755888): Refactor to using assume API
	 */
	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul), NULL);
	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul2), NULL);
	/* Give time to actually disconnect */
	k_sleep(K_SECONDS(1));

	/* Reset vbus to 0mV */
	isl923x_emul_set_adc_vbus(charger_emul, 0);
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

ZTEST_SUITE(integration_usb, drivers_predicate_post_main, NULL,
	    integration_usb_before, integration_usb_after, NULL);
