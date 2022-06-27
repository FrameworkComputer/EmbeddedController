/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

#include "charge_manager.h"
#include "charge_state.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "compile_time_macros.h"
#include "console.h"
#include "driver/charger/isl9241.h"
#include "gpio.h"
#include "hooks.h"
#include "stdbool.h"
#include "usbc_ppc.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

/* Charger Chip Configuration */
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL9241_ADDR_FLAGS,
		.drv = &isl9241_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(chg_chips) == CHARGER_NUM);

static int board_disable_bj_port(void)
{
	gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 1);
	/* If the current port is BJ, disable bypass mode. */
	if (charge_manager_get_supplier() == CHARGE_SUPPLIER_DEDICATED)
		return charger_enable_bypass_mode(0, 0);

	CPRINTS("BJ power is disabled");

	return EC_SUCCESS;
}

static int board_enable_bj_port(void)
{
	if (gpio_get_level(GPIO_BJ_ADP_PRESENT_ODL))
		return EC_ERROR_INVAL;
	gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 0);

	CPRINTS("BJ power is enabled");

	return charger_enable_bypass_mode(0, 1);
}

/*
 * TODO:
 *
 * When AC is being plugged in (including switching source port),
 *   1. Deassert NVIDIA_GPU_ACOFF_ODL.
 *   2. Call evaluate_d_notify.
 *
 * When AC is being lost,
 *   1. Assert NVIDIA_GPU_ACOFF_ODL.
 *   2. Set D-Notify to D5.
 *   3. Differ-call
 *      a. Deassert NVIDIA_GPU_ACOFF_ODL.
 *      b. evaluate_d_notify
 */
static int board_throttle_ap_gpu(bool enable)
{
	int rv = EC_SUCCESS;

	if (!chipset_in_state(CHIPSET_STATE_ON))
		return EC_SUCCESS;

	CPRINTS("TODO: %s to %s AP & GPU (%d)", rv ? "Failed" : "Succeeded",
		enable ? "throttle" : "unthrottle", rv);

	return rv;
}

/* Disable all VBUS sink ports except <port>. <port> = -1 disables all ports. */
static int board_disable_vbus_sink(int port)
{
	int i, r, rv = EC_SUCCESS;

	for (i = 0; i < ppc_cnt; i++) {
		if (i == port)
			continue;
		/*
		 * Do not return early if one fails otherwise we can get into a
		 * boot loop assertion failure.
		 */
		r = ppc_vbus_sink_enable(i, 0);
		CPRINTS("%s to disable sink path C%d (%d).",
			r ? "Failed" : "Succeeded", i, r);
		rv |= r;
	}

	return rv;
}

/* Minimum battery SoC required for switching source port. */
#define MIN_BATT_FOR_SWITCHING_SOURCE_PORT 1

/*
 * It should also work on POR with/without a battery:
 *
 * 1. EC gathers power info of all ports.
 * 2. Identify the highest power port.
 * 3. If
 *    1. battery soc = 0% --> Exit
 *    2. BJ_ADP_PRESENT_ODL = 1 --> Exit
 *    3. highest power port == active port --> Exit
 * 4. If
 *    1. in S0, throttle AP & GPU to the DC rating.
 * 5. Turn off the current active port.
 * 6. Turn on the highest power port.
 * 7. If
 *    1. in S0, throttle AP & GPU back.
 *
 * TODO: Are the following cases covered?
 * 1. Two AC adapters are plugged. Then, the active adapter is removed.
 *
 * TODO: Recover from incomplete execution:
 * 1. Failed to turn on/off PPC.
 */
int board_set_active_charge_port(int port)
{
	enum charge_supplier supplier = charge_manager_get_supplier();
	int active_port = charge_manager_get_active_charge_port();

	CPRINTS("Changing charge port to %d (current port=%d supplier=%d)",
		port, active_port, supplier);

	if (port == CHARGE_PORT_NONE) {
		CPRINTS("Disabling all charger ports");

		board_throttle_ap_gpu(1);

		board_disable_bj_port();
		board_disable_vbus_sink(-1);

		return EC_SUCCESS;
	}

	if (port < 0 || CHARGE_PORT_COUNT <= port) {
		return EC_ERROR_INVAL;
	} else if (port == active_port) {
		return EC_SUCCESS;
	} else if (board_vbus_source_enabled(port)) {
		/* Don't charge from a USBC source port */
		CPRINTS("Don't enable C%d. It's sourcing.", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * We need to check the battery if we're switching a source port. If
	 * we're just starting up or no AC was previously plugged, we shouldn't
	 * check the battery. Both cases can be caught by supplier == NONE.
	 */
	if (supplier != CHARGE_SUPPLIER_NONE) {
		if (charge_get_percent() < MIN_BATT_FOR_SWITCHING_SOURCE_PORT)
			return EC_ERROR_NOT_POWERED;
	}

	/* Turn off other ports' sink paths before enabling requested port. */
	if (port == CHARGE_PORT_TYPEC0 || port == CHARGE_PORT_TYPEC1) {
		/*
		 * BJ port is on POR. So, we need to turn it off even if we're
		 * not previously on BJ.
		 */
		board_disable_bj_port();
		if (board_disable_vbus_sink(port))
			return EC_ERROR_UNCHANGED;

		/* Enable requested USBC charge port. */
		if (ppc_vbus_sink_enable(port, 1)) {
			CPRINTS("Failed to enable sink path for C%d", port);
			return EC_ERROR_UNKNOWN;
		}
	} else if (port == CHARGE_PORT_BARRELJACK) {
		/*
		 * We can't proceed unless both ports are successfully
		 * disconnected as sources.
		 */
		if (board_disable_vbus_sink(-1))
			return EC_ERROR_UNKNOWN;
		board_enable_bj_port();
	}

	/* Switching port is complete. Turn off throttling. */
	if (supplier != CHARGE_SUPPLIER_NONE)
		board_throttle_ap_gpu(0);

	CPRINTS("New charger p%d", port);

	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
	charge_set_input_current_limit(
		MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}

static const struct charge_port_info bj_power = {
	/* 150W (also default) */
	.voltage = 19500,
	.current = 7700,
};

/* Debounce time for BJ plug/unplug */
#define BJ_DEBOUNCE_MS 1000

static void bj_connect_deferred(void)
{
	static int8_t bj_connected = -1;
	const struct charge_port_info *pi = NULL;
	int connected = !gpio_get_level(GPIO_BJ_ADP_PRESENT_ODL);

	if (connected == bj_connected)
		return;

	if (connected)
		pi = &bj_power;

	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				     DEDICATED_CHARGE_PORT, pi);
	bj_connected = connected;
	CPRINTS("BJ %s", connected ? "connected" : "disconnected");
}
DECLARE_DEFERRED(bj_connect_deferred);

void bj_present_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&bj_connect_deferred_data, BJ_DEBOUNCE_MS * MSEC);
}

static void bj_state_init(void)
{
	/*
	 * Initialize all charge suppliers to 0. The charge manager waits until
	 * all ports have reported in before doing anything.
	 */
	for (int i = 0; i < CHARGE_PORT_COUNT; i++) {
		for (int j = 0; j < CHARGE_SUPPLIER_COUNT; j++)
			charge_manager_update_charge(j, i, NULL);
	}

	bj_connect_deferred();
}
DECLARE_HOOK(HOOK_INIT, bj_state_init, HOOK_PRIO_INIT_CHARGE_MANAGER + 1);
