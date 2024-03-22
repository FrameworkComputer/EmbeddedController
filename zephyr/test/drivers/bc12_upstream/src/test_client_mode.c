/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This test suite verifies integration of upstream BC1.2 drivers operating
 * in client mode (port partner is a charger) with the EC application. This
 * test suite is driver agnostic, and should not perform any driver specific
 * checks.
 */

#include "battery.h"
#include "charge_manager.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"
#include "usb_charge.h"
#include "usbc/bc12_upstream.h"
#include "usbc/utils.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/usb/emul_bc12.h>
#include <zephyr/drivers/usb/usb_bc12.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_drivers_bc12_upstream, LOG_LEVEL_DBG);

#define BATT_PRES_NODE NAMED_GPIOS_GPIO_NODE(ec_batt_pres_odl)

#define CHARGE_DETECT_DELAY_MS (CHARGE_DETECT_DELAY / 1000)

struct bc12_upstream_client_mode_fixture {
	const struct device *bc12_dev;
	const struct emul *bc12_emul;
	const struct device *batt_pres_port;
	const gpio_pin_t batt_pres_pin;
	unsigned int typec_port;
};

ZTEST_F(bc12_upstream_client_mode, test_bc12_client_mode_sdp)
{
	struct ec_response_usb_pd_power_info response;

	/* Verify battery has been setup */
	zassert_equal(BP_YES, battery_is_present());

	/* Initial state should be disconnected */
	response = host_cmd_power_info(fixture->typec_port);
	zassert_equal(response.role, USB_PD_PORT_POWER_DISCONNECTED,
		      "Expected power role %d, but PD reports role %d",
		      USB_PD_PORT_POWER_DISCONNECTED, response.role);

	zassert_equal(response.type, USB_CHG_TYPE_NONE,
		      "Expected charger type %d, but PD reports type %d",
		      USB_CHG_TYPE_NONE, response.type);

	/*
	 * Pretend that the USB-C Port Manager (TCPMv2) has set the port data
	 * role to UFP and decided charging from the port is allowed.
	 */
	usb_charger_task_set_event(fixture->typec_port, USB_CHG_EVENT_DR_UFP);
	charge_manager_update_dualrole(fixture->typec_port, CAP_DEDICATED);

	/* Emulate connection of a SDP charging partner */
	bc12_emul_set_charging_partner(fixture->bc12_emul, BC12_TYPE_SDP);

	/* Don't query the power info until the charge detect delay expires. */
	k_sleep(K_MSEC(CHARGE_DETECT_DELAY_MS * 2));

	response = host_cmd_power_info(0);
	zassert_equal(response.role, USB_PD_PORT_POWER_SINK,
		      "Expected power role %d, but PD reports role %d",
		      USB_PD_PORT_POWER_SINK, response.role);

	zassert_equal(response.type, USB_CHG_TYPE_BC12_SDP,
		      "Expected charger type %d, but PD reports type %d",
		      USB_CHG_TYPE_BC12_SDP, response.type);
}

ZTEST_F(bc12_upstream_client_mode, test_bc12_client_mode_cdp)
{
	struct ec_response_usb_pd_power_info response;

	/* Verify battery has been setup */
	zassert_equal(BP_YES, battery_is_present());

	/* Initial state should be disconnected */
	response = host_cmd_power_info(0);
	zassert_equal(response.role, USB_PD_PORT_POWER_DISCONNECTED,
		      "Expected power role %d, but PD reports role %d",
		      USB_PD_PORT_POWER_DISCONNECTED, response.role);

	zassert_equal(response.type, USB_CHG_TYPE_NONE,
		      "Expected charger type %d, but PD reports type %d",
		      USB_CHG_TYPE_NONE, response.type);

	/*
	 * Pretend that the USB-C Port Manager (TCPMv2) has set the port data
	 * role to UFP and decided charging from the port is allowed.
	 */
	usb_charger_task_set_event(fixture->typec_port, USB_CHG_EVENT_DR_UFP);
	charge_manager_update_dualrole(fixture->typec_port, CAP_DEDICATED);

	/* Emulate connection of a CDP charging partner */
	bc12_emul_set_charging_partner(fixture->bc12_emul, BC12_TYPE_CDP);

	/* Don't query the power info until the charge detect delay expires. */
	k_sleep(K_MSEC(CHARGE_DETECT_DELAY_MS * 2));

	response = host_cmd_power_info(0);
	zassert_equal(response.role, USB_PD_PORT_POWER_SINK,
		      "Expected power role %d, but PD reports role %d",
		      USB_PD_PORT_POWER_SINK, response.role);

	zassert_equal(response.type, USB_CHG_TYPE_BC12_CDP,
		      "Expected charger type %d, but PD reports type %d",
		      USB_CHG_TYPE_BC12_SDP, response.type);
}

ZTEST_F(bc12_upstream_client_mode, test_bc12_client_mode_dcp)
{
	struct ec_response_usb_pd_power_info response;

	/* Verify battery has been setup */
	zassert_equal(BP_YES, battery_is_present());

	/* Initial state should be disconnected */
	response = host_cmd_power_info(0);
	zassert_equal(response.role, USB_PD_PORT_POWER_DISCONNECTED,
		      "Expected power role %d, but PD reports role %d",
		      USB_PD_PORT_POWER_DISCONNECTED, response.role);

	zassert_equal(response.type, USB_CHG_TYPE_NONE,
		      "Expected charger type %d, but PD reports type %d",
		      USB_CHG_TYPE_NONE, response.type);

	/*
	 * Pretend that the USB-C Port Manager (TCPMv2) has set the port data
	 * role to UFP and decided charging from the port is allowed.
	 */
	usb_charger_task_set_event(fixture->typec_port, USB_CHG_EVENT_DR_UFP);
	charge_manager_update_dualrole(fixture->typec_port, CAP_DEDICATED);

	/* Emulate connection of a CDP charging partner */
	bc12_emul_set_charging_partner(fixture->bc12_emul, BC12_TYPE_DCP);

	/* Don't query the power info until the charge detect delay expires. */
	k_sleep(K_MSEC(CHARGE_DETECT_DELAY_MS * 2));

	response = host_cmd_power_info(0);
	zassert_equal(response.role, USB_PD_PORT_POWER_SINK,
		      "Expected power role %d, but PD reports role %d",
		      USB_PD_PORT_POWER_SINK, response.role);

	zassert_equal(response.type, USB_CHG_TYPE_BC12_DCP,
		      "Expected charger type %d, but PD reports type %d",
		      USB_CHG_TYPE_BC12_SDP, response.type);
}

static void *bc12_client_mode_setup(void)
{
	static struct bc12_upstream_client_mode_fixture fixture = {
		.bc12_dev = DEVICE_GET_USBC_BINDING(0, bc12),
		.bc12_emul = EMUL_GET_USBC_BINDING(0, bc12),
		.batt_pres_port =
			DEVICE_DT_GET(DT_GPIO_CTLR(BATT_PRES_NODE, gpios)),
		.batt_pres_pin = DT_GPIO_PIN(BATT_PRES_NODE, gpios),
		.typec_port = 0,
	};

	zassert_equal(bc12_ports[fixture.typec_port].drv, &bc12_upstream_drv);
	zassert_not_null(fixture.bc12_dev);
	zassert_not_null(fixture.bc12_emul);
	zassert_true(device_is_ready(fixture.bc12_dev));

	zassert_not_null(fixture.batt_pres_port);
	zassert_true(device_is_ready(fixture.batt_pres_port));

	return &fixture;
}

static void bc12_client_mode_before(void *f)
{
	struct bc12_upstream_client_mode_fixture *fixture = f;

	/* Pretend we have battery and AC so charging works normally. */
	gpio_emul_input_set(fixture->batt_pres_port, fixture->batt_pres_pin, 0);
	zassert_equal(BP_YES, battery_is_present());

	set_ac_enabled(true);

	/* Wait long enough for TCPMv2 to be idle. */
	k_sleep(K_MSEC(2000));

	usb_charger_task_set_event(fixture->typec_port, USB_CHG_EVENT_CC_OPEN);
	k_sleep(K_MSEC(1));
}

static void bc12_client_mode_after(void *f)
{
	struct bc12_upstream_client_mode_fixture *fixture = f;

	bc12_emul_set_charging_partner(fixture->bc12_emul, BC12_TYPE_NONE);
	set_ac_enabled(false);
}

ZTEST_SUITE(bc12_upstream_client_mode, drivers_predicate_post_main,
	    bc12_client_mode_setup, bc12_client_mode_before,
	    bc12_client_mode_after, NULL);
