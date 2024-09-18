/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This file tests the sourcing policies on type-C ports.  See the diagram
 * under "ChromeOS as Source - Policy for Type-C" in the usb_power.md.
 */

#include "charge_manager.h"
#include "chipset.h"
#include "emul/emul_pdc.h"
#include "timer.h"
#include "usbc/pdc_power_mgmt.h"
#include "usbc/utils.h"
#include "zephyr/sys/util.h"

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(pdc_non_pd_policy);

#define PDC_NODE_PORT0 DT_NODELABEL(pdc_emul1)
#define TEST_USBC_PORT0 USBC_PORT_FROM_DRIVER_NODE(PDC_NODE_PORT0, pdc)

#define CHARGE_DETECT_DELAY_MS (CHARGE_DETECT_DELAY / 1000)

bool pdc_power_mgmt_is_pd_attached(int port);

static const char *power_operation_mode_name[] = {
	[USB_DEFAULT_OPERATION] = "USB_DEFAULT_OPERATION",
	[BC_OPERATION] = "BC_OPERATION",
	[PD_OPERATION] = "PD_OPERATION",
	[USB_TC_CURRENT_1_5A] = "USB_TC_CURRENT_1_5A",
	[USB_TC_CURRENT_3A] = "USB_TC_CURRENT_3A",
	[USB_TC_CURRENT_5A] = "USB_TC_CURRENT_5A",
};

struct non_pd_policy_fixture {
	int port;
	const struct emul *emul_pdc;
};

static void *non_pd_policy_setup(void)
{
	static struct non_pd_policy_fixture fixture;

	fixture.emul_pdc = EMUL_DT_GET(PDC_NODE_PORT0);
	fixture.port = TEST_USBC_PORT0;

	return &fixture;
};

static void non_pd_policy_before(void *f)
{
	struct non_pd_policy_fixture *fixture = f;

	/* Start with port disconnected. */
	zassert_ok(emul_pdc_disconnect(fixture->emul_pdc));
	zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(fixture->port));
}

ZTEST_SUITE(non_pd_policy, NULL, non_pd_policy_setup, non_pd_policy_before,
	    NULL, NULL);

/* TODO - find a common location for this. */
static inline struct ec_response_usb_pd_power_info host_cmd_power_info(int port)
{
	struct ec_params_usb_pd_power_info params = { .port = port };
	struct ec_response_usb_pd_power_info response;

	zassert_ok(ec_cmd_usb_pd_power_info(NULL, &params, &response),
		   "Failed to get power info for port %d", port);
	return response;
}

/* Verify the DUT can sink from a non-PD charger at the expected
 * power level for all types of non-PD sources.
 */
ZTEST_USER_F(non_pd_policy, test_non_pd_sinking)
{
	union connector_status_t connector_status;
	struct ec_response_usb_pd_power_info response;
	union conn_status_change_bits_t connector_change;

	struct {
		enum power_operation_mode_t power_operation_mode;
		uint16_t voltage_max;
		uint16_t current_lim;
	} test[] = {
		{
			.power_operation_mode = USB_DEFAULT_OPERATION,
			.voltage_max = 5000,
			.current_lim = 500,
		},
		{
			.power_operation_mode = BC_OPERATION,
			.voltage_max = 5000,
			.current_lim = 500,
		},
		{
			.power_operation_mode = USB_TC_CURRENT_1_5A,
			.voltage_max = 5000,
			.current_lim = 1500,
		},
		{
			.power_operation_mode = USB_TC_CURRENT_3A,
			.voltage_max = 5000,
			.current_lim = 3000,
		},
		{
			.power_operation_mode = USB_TC_CURRENT_5A,
			.voltage_max = 5000,
			.current_lim = 5000,
		},
	};

	for (int i = 0; i < ARRAY_SIZE(test); i++) {
		/* Initial state should be disconnected */
		response = host_cmd_power_info(fixture->port);
		zassert_equal(response.role, USB_PD_PORT_POWER_DISCONNECTED,
			      "Expected power role %d, but EC reports role %d",
			      USB_PD_PORT_POWER_DISCONNECTED, response.role);
		zassert_equal(
			response.type, USB_CHG_TYPE_NONE,
			"Expected charger type %d, but EC reports type %d",
			USB_CHG_TYPE_NONE, response.type);

		/* First connect the partner at the USB default current only.
		 * The PDC always reports USB default current to start to
		 * conform with the tRpValueChange requirement of the USB
		 * Type-C Specification.
		 */
		LOG_INF("Connect non-PD charger, USB default mode");

		connector_status.power_direction = 0;
		connector_status.power_operation_mode = USB_DEFAULT_OPERATION;
		zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc,
						    &connector_status));
		zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(
			fixture->port));

		/* Wait tRpValueChange before emulating a change in Rp. */
		k_sleep(K_USEC(PD_T_RP_VALUE_CHANGE));

		/* Emulate a change in Rp detected by the PDC. */
		LOG_INF("Connect non-PD charger, mode = %s (%d)",
			power_operation_mode_name[test[i].power_operation_mode],
			test[i].power_operation_mode);
		connector_status.power_operation_mode =
			test[i].power_operation_mode;
		connector_change.raw_value =
			connector_status.raw_conn_status_change_bits;
		connector_change.pwr_operation_mode = 1;
		connector_status.raw_conn_status_change_bits =
			connector_change.raw_value;
		zassert_ok(emul_pdc_connect_partner(fixture->emul_pdc,
						    &connector_status));
		zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(
			fixture->port));

		/* Don't query the power info until the charge detect delay
		 * expires. */
		k_msleep(CHARGE_DETECT_DELAY_MS * 2);

		response = host_cmd_power_info(fixture->port);
		zassert_equal(response.role, USB_PD_PORT_POWER_SINK,
			      "Expected power role %d, but EC reports %d",
			      USB_PD_PORT_POWER_SINK, response.role);
		zassert_equal(
			response.type, USB_CHG_TYPE_C,
			"Expected charger type %d, but EC reports type %d",
			USB_CHG_TYPE_C, response.type);
		zassert_equal(
			response.meas.voltage_max, test[i].voltage_max,
			"Expected charger voltage %dmV, but EC reports %dmV",
			test[i].voltage_max, response.meas.voltage_max);
		zassert_equal(
			response.meas.current_lim, test[i].current_lim,
			"Expected charger current %dmA, but EC reports %dmA",
			test[i].current_lim, response.meas.current_lim);

		zassert_ok(emul_pdc_disconnect(fixture->emul_pdc));
		zassert_ok(pdc_power_mgmt_resync_port_state_for_ppm(
			fixture->port));
	}
}
