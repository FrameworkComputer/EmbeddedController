/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

#include "charge_state.h"
#include "console.h"
#include "cypress_pd_common.h"
#include "driver/charger/isl9241.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/*****************************************************************************/
/* Internal functions */

/*****************************************************************************/
/* Interrupt handler */

void cypd_interrupt_handler_task(void *p)
{
	int evt;
	while (1) {
		evt = task_wait_event(10*MSEC);
	}
}

void pd0_chip_interrupt_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_INT_CTRL_0);

}
DECLARE_DEFERRED(pd0_chip_interrupt_deferred);

void pd1_chip_interrupt_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_INT_CTRL_1);

}
DECLARE_DEFERRED(pd1_chip_interrupt_deferred);

void pd0_chip_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&pd0_chip_interrupt_deferred_data, 0);
}

void pd1_chip_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&pd1_chip_interrupt_deferred_data, 0);
}

/*****************************************************************************/
/* Commmon functions */

enum pd_power_role pd_get_power_role(int port)
{
	/*
	 * TODO: return actual power role when PD code is imeplemtn.
	 */
	return PD_ROLE_SINK;
}

void pd_request_power_swap(int port)
{
	/*
	 * TODO: implement request power swap function.
	 */
}

void pd_set_new_power_request(int port)
{
	/* We probably dont need to do this since we will always request max. */
}

int pd_is_connected(int port)
{
	/*
	 * TODO: implement check type-c port status.
	 */
	return false;
}


__override uint8_t board_get_usb_pd_port_count(void)
{
	/*
	 * TODO: macro and return the CONFIG_USB_PD_PORT_MAX_COUNT
	 */
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	int prochot_ma;

	if (charge_ma < CONFIG_PLATFORM_EC_CHARGER_INPUT_CURRENT) {
		charge_ma = CONFIG_PLATFORM_EC_CHARGER_INPUT_CURRENT;
	}
	/*
	 * ac prochot should bigger than input current
	 * And needs to be at least 128mA bigger than the adapter current
	 */
	prochot_ma = (DIV_ROUND_UP(charge_ma, 128) * 128);
	charge_ma = charge_ma * 95 / 100;

	if ((prochot_ma - charge_ma) < 128) {
		charge_ma = prochot_ma - 128;
	}

	charge_set_input_current_limit(charge_ma, charge_mv);
	/* sync-up ac prochot with current change */
	isl9241_set_ac_prochot(0, prochot_ma);
}

int board_set_active_charge_port(int charge_port)
{
	/*
	 * TODO: implement set active charge port function.
	 */
	return EC_SUCCESS;
}

/*****************************************************************************/
/* Host command */

/*****************************************************************************/
/* EC console command */
