/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "assert.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state_v2.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "cros_board_info.h"
#include "fw_config.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "power_button.h"
#include "hooks.h"
#include "power.h"
#include "switch.h"
#include "throttle_ap.h"
#include "usbc_config.h"
#include "usbc_ppc.h"

#include "gpio_list.h" /* Must come after other header files. */

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

/******************************************************************************/
/* USB-A charging control */

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USBA,
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

/******************************************************************************/

int board_set_active_charge_port(int port)
{
	int rv;

	CPRINTS("Requested charge port change to %d", port);

	/*
	 * The charge manager may ask us to switch to no charger if we're
	 * running off USB-C only but upstream doesn't support PD. It requires
	 * that we accept this switch otherwise it triggers an assert and EC
	 * reset; it's not possible to boot the AP anyway, but we want to avoid
	 * resetting the EC so we can continue to do the "low power" LED blink.
	 */
	if (port == CHARGE_PORT_NONE)
		return EC_SUCCESS;

	if (port < 0 || CHARGE_PORT_COUNT <= port)
		return EC_ERROR_INVAL;

	if (port == charge_manager_get_active_charge_port())
		return EC_SUCCESS;

	/* Don't charge from a source port */
	if (board_vbus_source_enabled(port))
		return EC_ERROR_INVAL;

	/* Don't change the charge port */
	if (charge_manager_get_active_charge_port() != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	/* Make sure BJ adapter is sourcing power */
	if (port == CHARGE_PORT_BARRELJACK &&
				gpio_get_level(GPIO_BJ_ADP_PRESENT_ODL)) {
		CPRINTS("BJ port selected, but not present!");
		return EC_ERROR_INVAL;
	}

	CPRINTS("New charger p%d", port);

	switch (port) {
	case CHARGE_PORT_TYPEC0:
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 1);
		rv = ppc_vbus_sink_enable(CHARGE_PORT_TYPEC0, 1);
		if (rv) {
			CPRINTS("Failed to enable C0 sink path");
			return rv;
		}
		break;
	case CHARGE_PORT_BARRELJACK:
		rv = ppc_vbus_sink_enable(CHARGE_PORT_TYPEC0, 0);
		if (rv) {
			CPRINTS("Failed to disable C0 sink path");
			return rv;
		}
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 0);
		break;
	default:
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
				int max_ma, int charge_mv)
{
}

static void adp_state_init(void)
{
	ASSERT(CHARGE_PORT_ENUM_COUNT == CHARGE_PORT_COUNT);
	/*
	 * Initialize all charge suppliers to 0. The charge manager waits until
	 * all ports have reported in before doing anything.
	 */
	for (int i = 0; i < CHARGE_PORT_COUNT; i++) {
		for (int j = 0; j < CHARGE_SUPPLIER_COUNT; j++)
			charge_manager_update_charge(j, i, NULL);
	}
}
DECLARE_HOOK(HOOK_INIT, adp_state_init, HOOK_PRIO_INIT_CHARGE_MANAGER + 1);

static void board_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
