/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inductive charging control */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "inductive_charging.h"
#include "lid_switch.h"
#include "timer.h"

/*
 * The inductive charger is controlled with two signals:
 *   - BASE_CHG_VDD_EN controls whether the charger is powered.
 *   - CHARGE_EN controls whether to enable charging.
 * Charging status is reported via CHARGE_DONE, but in a tricky way:
 *   - It's 0 if:
 *     + The charger is unpowered. (i.e. BASE_CHG_VDD_EN = 0)
 *     + Or charging is disabled. (i.e. CHARGE_EN = 0)
 *     + Or the charging current is small enough.
 *   - Otherwise, it's 1.
 */

/* Whether we want to process interrupts on CHARGE_DONE or not. */
static int monitor_charge_done;

/*
 * Start monitoring CHARGE_DONE and fires the interrupt once so that
 * we react to the current value.
 */
static void inductive_charging_monitor_charge(void)
{
	monitor_charge_done = 1;
	inductive_charging_interrupt(GPIO_CHARGE_DONE);
}
DECLARE_DEFERRED(inductive_charging_monitor_charge);

void inductive_charging_interrupt(enum gpio_signal signal)
{
	int charger_enabled = gpio_get_level(GPIO_BASE_CHG_VDD_EN);
	int charge_done = gpio_get_level(GPIO_CHARGE_DONE);
	static int charge_already_done;

	if (!monitor_charge_done && signal == GPIO_CHARGE_DONE)
		return;

	if (signal == GPIO_LID_OPEN) {
		/* The lid has been opened. Clear all states. */
		charge_done = 0;
		charge_already_done = 0;
		monitor_charge_done = 0;
	} else if (signal == GPIO_CHARGE_DONE) {
		/*
		 * Once we see CHARGE_DONE=1, we ignore any change on
		 * CHARGE_DONE until the next time the lid is opened.
		 */
		if (charge_done == 1)
			charge_already_done = 1;
		else if (charge_already_done)
			return;
	}

	if (!charger_enabled || charge_done) {
		gpio_set_level(GPIO_CHARGE_EN, 0);
	} else {
		gpio_set_level(GPIO_CHARGE_EN, 1);
		/*
		 * When the charging is just enabled, there might be a
		 * blip on CHARGE_DONE. Wait for a second before we start
		 * looking at CHARGE_DONE.
		 */
		if (!monitor_charge_done)
			hook_call_deferred(
				&inductive_charging_monitor_charge_data,
				SECOND);
	}
}

static void inductive_charging_deferred_update(void)
{
	int lid_open = lid_is_open();
	gpio_set_level(GPIO_BASE_CHG_VDD_EN, !lid_open);
	inductive_charging_interrupt(GPIO_LID_OPEN);
}
DECLARE_DEFERRED(inductive_charging_deferred_update);

static void inductive_charging_lid_update(void)
{
	/*
	 * When the lid close signal changes, the coils might still be
	 * unaligned. Delay here to give the coils time to align before
	 * we try to clear CHARGE_DONE.
	 */
	hook_call_deferred(&inductive_charging_deferred_update_data,
			   5 * SECOND);
}
DECLARE_HOOK(HOOK_LID_CHANGE, inductive_charging_lid_update, HOOK_PRIO_DEFAULT);

static void inductive_charging_init(void)
{
	gpio_enable_interrupt(GPIO_CHARGE_DONE);
	inductive_charging_lid_update();
}
DECLARE_HOOK(HOOK_INIT, inductive_charging_init, HOOK_PRIO_DEFAULT);
