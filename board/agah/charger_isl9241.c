/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 *
 * We need to deal with plug / unplug of AC chargers:
 *
 *  +---------+    +USB     +---------+
 *  | BATTERY |------------>| BATTERY |
 *  |         |<------------|    +USB |
 *  +---------+    -USB     +---------+
 *      | ^                     | ^
 *  +BJ | | -BJ             +BJ | | -BJ
 *      v |                     v |
 *  +---------+    +USB     +---------+
 *  | BATTERY |------------>| BATTERY |
 *  |     +BJ |<------------| +BJ+USB |
 *  +---------+    -USB     +---------+
 *
 * Depending on available battery charge, power rating of the new charger, and
 * the system power state, transition/throttling may or may not occur but
 * switching chargers is handled as follows:
 *
 * 1. Detects a new charger or removal of an existing charger.
 * 2. charge_manager_update_charge is called with new charger's info.
 * 3. board_set_active_charge_port is called.
 *    3.1 It triggers hard & soft throttling for AP & GPU.
 *    3.2 It disable active port then enables the new port.
 * 4. HOOK_POWER_SUPPLY_CHANGE is called. We disables hard throttling.
 * 5. charger task wakes up on HOOK_POWER_SUPPLY_CHANGE, enables (or disables)
 *    bypass mode.
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
#include "throttle_ap.h"
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

static int board_enable_bj_port(bool enable)
{
	if (enable) {
		if (gpio_get_level(GPIO_BJ_ADP_PRESENT_ODL))
			return EC_ERROR_INVAL;
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 0);
	} else {
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 1);
	}

	CPRINTS("BJ power is %sabled", enable ? "en" : "dis");

	return EC_SUCCESS;
}

static void board_throttle_ap_gpu(void)
{
	throttle_ap(THROTTLE_ON, THROTTLE_HARD, THROTTLE_SRC_AC);
	throttle_gpu(THROTTLE_ON, THROTTLE_HARD, THROTTLE_SRC_AC);
}

/* Disable all VBUS sink ports except <port>. <port> = -1 disables all ports. */
static int board_disable_other_vbus_sink(int except_port)
{
	int i, r, rv = EC_SUCCESS;

	for (i = 0; i < ppc_cnt; i++) {
		if (i == except_port)
			continue;
		/*
		 * Do not return early if one fails otherwise we can get into a
		 * boot loop assertion failure.
		 */
		r = ppc_vbus_sink_enable(i, 0);
		if (r)
			CPRINTS("Failed to disable sink path C%d (%d)", i, r);
		rv |= r;
	}

	return rv;
}

/* Minimum battery SoC required for switching source port. */
#define MIN_BATT_FOR_SWITCHING_SOURCE_PORT 1

/*
 * TODO: Recover from incomplete execution:
 */
int board_set_active_charge_port(int port)
{
	enum charge_supplier active_supplier = charge_manager_get_supplier();
	int active_port = charge_manager_get_active_charge_port();

	CPRINTS("Switching charger from P%d (supplier=%d) to P%d", active_port,
		active_supplier, port);

	if (port == CHARGE_PORT_NONE) {
		CPRINTS("Disabling all charger ports");

		board_enable_bj_port(false);
		board_disable_other_vbus_sink(-1);

		return EC_SUCCESS;
	}

	/* Return on invalid or no-op call. */
	if (port < 0 || CHARGE_PORT_COUNT <= port) {
		return EC_ERROR_INVAL;
	} else if (port == active_port) {
		return EC_SUCCESS;
	} else if (board_vbus_source_enabled(port)) {
		/* Don't charge from a USBC source port */
		CPRINTS("Don't enable P%d. It's sourcing.", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * If we're in S0, throttle AP and GPU. They'll be unthrottled when
	 * a port/supply switch completes (via HOOK_POWER_SUPPLY_CHANGE).
	 *
	 * If we're running currently on a battery (active_supplier == NONE), we
	 * don't need to throttle because we're not disabling any port.
	 */
	if (chipset_in_state(CHIPSET_STATE_ON) &&
	    active_supplier != CHARGE_SUPPLIER_NONE)
		board_throttle_ap_gpu();

	/*
	 * We're here for the two cases:
	 * 1. A new charger was connected.
	 * 2. One charger was disconnected and we're switching to another.
	 */

	/*
	 * We need to check the battery if we're switching a source port. If
	 * we're just starting up or no AC was previously plugged, we shouldn't
	 * check the battery. Both cases can be caught by supplier == NONE.
	 */
	if (active_supplier != CHARGE_SUPPLIER_NONE) {
		if (charge_get_percent() < MIN_BATT_FOR_SWITCHING_SOURCE_PORT)
			return EC_ERROR_NOT_POWERED;
	}

	/* Turn off other ports' sink paths before enabling requested port. */
	if (is_pd_port(port)) {
		/*
		 * BJ port is enabled on start-up. So, we need to turn it off
		 * even if we were not previously on BJ.
		 */
		board_enable_bj_port(false);
		if (board_disable_other_vbus_sink(port))
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
		if (board_disable_other_vbus_sink(-1))
			return EC_ERROR_UNKNOWN;
		board_enable_bj_port(true);
	}

	CPRINTS("New charger P%d", port);

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
#define BJ_DEBOUNCE_MS CONFIG_EXTPOWER_DEBOUNCE_MS

int board_should_charger_bypass(void)
{
	return charge_manager_get_active_charge_port() == DEDICATED_CHARGE_PORT;
}

static void bj_connect(void)
{
	static int8_t bj_connected = -1;
	int connected = !gpio_get_level(GPIO_BJ_ADP_PRESENT_ODL);

	/* Debounce */
	if (connected == bj_connected)
		return;

	bj_connected = connected;
	CPRINTS("BJ %sconnected", connected ? "" : "dis");

	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				     DEDICATED_CHARGE_PORT,
				     connected ? &bj_power : NULL);
}
DECLARE_DEFERRED(bj_connect);

/* This handler shouldn't be needed if ACOK from isl9241 is working. */
void bj_present_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&bj_connect_data, BJ_DEBOUNCE_MS * MSEC);
}

void ac_change(void)
{
	/*
	 * Serialize. We don't handle USB-C here because we'll get a
	 * notification from TCPC.
	 */
	hook_call_deferred(&bj_connect_data, 0);
}
DECLARE_HOOK(HOOK_AC_CHANGE, ac_change, HOOK_PRIO_DEFAULT);

static void power_supply_changed(void)
{
	/*
	 * We've switched to a new charge port (or no port). Hardware throttles
	 * can be removed now. Software throttles may stay enabled and change
	 * as the situation changes.
	 */
	throttle_ap(THROTTLE_OFF, THROTTLE_HARD, THROTTLE_SRC_AC);
	/*
	 * Unthrottling GPU is done through a deferred call scheduled when it
	 * was throttled.
	 */
}
DECLARE_HOOK(HOOK_POWER_SUPPLY_CHANGE, power_supply_changed, HOOK_PRIO_DEFAULT);

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

	bj_connect();

	isl9241_set_ac_prochot(CHARGER_SOLO, AGAH_AC_PROCHOT_CURRENT_MA);
}
DECLARE_HOOK(HOOK_INIT, bj_state_init, HOOK_PRIO_INIT_CHARGE_MANAGER + 1);
