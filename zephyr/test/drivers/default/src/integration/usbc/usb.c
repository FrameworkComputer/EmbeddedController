/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

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
#include "task.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "test/usb_pe.h"

#define BATTERY_NODE DT_NODELABEL(battery)

#define GPIO_AC_OK_PATH DT_PATH(named_gpios, acok_od)
#define GPIO_AC_OK_PIN DT_GPIO_PIN(GPIO_AC_OK_PATH, gpios)

#define GPIO_BATT_PRES_ODL_PATH DT_PATH(named_gpios, ec_batt_pres_odl)
#define GPIO_BATT_PRES_ODL_PORT DT_GPIO_PIN(GPIO_BATT_PRES_ODL_PATH, gpios)

static void integration_usb_before(void *state)
{
	const struct emul *tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	const struct emul *tcpci_emul2 = EMUL_GET_USBC_BINDING(1, tcpc);
	const struct emul *charger_emul = EMUL_GET_USBC_BINDING(0, chg);

	/* Restart the PD task and let it settle */
	task_set_event(TASK_ID_PD_C0, TASK_EVENT_RESET_DONE);
	k_sleep(K_SECONDS(1));

	/* Reset vbus to 0mV */
	/* TODO(b/217610871): Remove redundant test state cleanup */
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	const struct emul *battery_emul = EMUL_DT_GET(BATTERY_NODE);
	struct sbat_emul_bat_data *bat;
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_AC_OK_PATH, gpios));

	ARG_UNUSED(state);
	/*
	 * TODO(b/217755888): Refactor to using assume API
	 */
	zassert_ok(tcpc_config[0].drv->init(0), NULL);
	zassert_ok(tcpc_config[1].drv->init(1), NULL);
	tcpc_config[USBC_PORT_C0].flags &= ~TCPC_FLAGS_TCPCI_REV2_0;
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
	bat = sbat_emul_get_bat_data(battery_emul);
	bat->cur = -5;

	/*
	 * TODO(b/217755888): Refactor to using assume API
	 */
	zassert_ok(gpio_emul_input_set(gpio_dev, GPIO_AC_OK_PIN, 0), NULL);
}

static void integration_usb_after(void *state)
{
	const struct emul *tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	const struct emul *tcpci_emul2 = EMUL_GET_USBC_BINDING(1, tcpc);
	const struct emul *charger_emul = EMUL_GET_USBC_BINDING(0, chg);
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

ZTEST(integration_usb, test_attach_drp)
{
	const struct emul *tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	struct tcpci_partner_data my_drp;
	struct tcpci_drp_emul_data drp_ext;
	struct tcpci_src_emul_data src_ext;
	struct tcpci_snk_emul_data snk_ext;

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	/* Attach emulated sink */
	tcpci_partner_init(&my_drp, PD_REV20);
	my_drp.extensions = tcpci_drp_emul_init(
		&drp_ext, &my_drp, PD_ROLE_SINK,
		tcpci_src_emul_init(&src_ext, &my_drp, NULL),
		tcpci_snk_emul_init(&snk_ext, &my_drp, NULL));

	zassert_ok(tcpci_partner_connect_to_tcpci(&my_drp, tcpci_emul), NULL);

	/* Wait for PD negotiation */
	k_sleep(K_SECONDS(10));

	/*
	 * Test that SRC ready is achieved
	 * TODO: Change it to examining EC_CMD_TYPEC_STATUS
	 */
	zassert_equal(PE_SNK_READY, get_state_pe(USBC_PORT_C0), NULL);
	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul), NULL);
}

ZTEST(integration_usb, test_event_loop)
{
	int paused = tc_event_loop_is_paused(USBC_PORT_C0);

	tc_pause_event_loop(USBC_PORT_C0);
	zassert_equal(1, tc_event_loop_is_paused(USBC_PORT_C0));

	tc_start_event_loop(USBC_PORT_C0);
	zassert_equal(0, tc_event_loop_is_paused(USBC_PORT_C0));

	/* Restore pause state from beginning */
	if (paused) {
		tc_pause_event_loop(USBC_PORT_C0);
	} else {
		tc_start_event_loop(USBC_PORT_C0);
	}
}

ZTEST_SUITE(integration_usb, drivers_predicate_post_main, NULL,
	    integration_usb_before, integration_usb_after, NULL);
