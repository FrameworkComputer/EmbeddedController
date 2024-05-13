/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

#include <dt-bindings/gpio_defines.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

test_export_static enum {
	STATE_ERROR, /* Stopped charging for ERR_TIME */
	STATE_CHARGE, /* Started Charging for CHG_TIME */
	STATE_STOP, /* Stopped charging for STP_TIME */
} pen_charge_state = STATE_STOP;

#define CHG_TIME 43200 /* 12 hours */
#define STP_TIME 10 /* 10 seconds */
#define ERR_TIME 600 /* 10 minutes */

test_export_static volatile int pen_timer = STP_TIME;

test_export_static uint8_t flags;
#define PEN_FAULT_DETECT BIT(0)

/*
 * Pen charge is controlled by EC
 *
 * 1) Fail safe:
 *   When pen fault is detected, pen charge will be
 *   stopped for 10 minutes [ERR_TIME].
 *
 *   |----Charge--|---Stop---|----Charge----|
 *                ^   10m
 *              fault
 *
 * 2) Repeted charge:
 *   To recover self discharge, pen charge will be
 *   restarted every 12 hours [CHG_TIME] with 10
 *   seconds rest [STP_TIME].
 *
 *   |----Charge----|-Stop-|----Charge----|-Stop-|
 *         12h        10s        12h        10s
 */
test_export_static void pen_charge(void)
{
	if (flags & PEN_FAULT_DETECT) {
		if (pen_charge_state != STATE_ERROR) {
			pen_timer = ERR_TIME;
			pen_charge_state = STATE_ERROR;
		}
	}

	switch (pen_charge_state) {
	case STATE_CHARGE:
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(ec_pen_chg_dis_odl),
			GPIO_ODR_HIGH);
		pen_timer--;
		if (pen_timer <= 0) {
			pen_charge_state = STATE_STOP;
			pen_timer = STP_TIME;
		}
		break;
	case STATE_STOP:
	case STATE_ERROR:
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(ec_pen_chg_dis_odl),
			GPIO_ODR_LOW);
		pen_timer--;
		if (pen_timer <= 0) {
			pen_charge_state = STATE_CHARGE;
			pen_timer = CHG_TIME;
			flags &= ~PEN_FAULT_DETECT;
		}
		break;
	default:
		break;
	}
}
DECLARE_HOOK(HOOK_SECOND, pen_charge, HOOK_PRIO_DEFAULT);

static void board_pen_fault_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pen_fault));
}
DECLARE_HOOK(HOOK_INIT, board_pen_fault_init, HOOK_PRIO_DEFAULT);

test_mockable void pen_fault_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_SIGNAL(DT_NODELABEL(pen_fault_od)))
		/* This function sets PEN_FAULT_DETECT only.            */
		/* pen_charge() disables pen charge on next HOOK_SECOND */
		flags |= PEN_FAULT_DETECT;
}
