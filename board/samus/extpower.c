/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Pure GPIO-based external power detection, buffered to PCH.
 * Drive high in S5-S0 when AC_PRESENT is high, otherwise drive low.
 */

#include "bq24773.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "system.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

/* Max number of attempts to enable/disable NVDC charger */
#define CHARGER_MODE_ATTEMPTS 3

/* Backboost has been detected */
static int bkboost_detected;

/* Charging is disabled */
static int charge_is_disabled;

/*
 * Charge circuit occasionally gets wedged and doesn't charge.
 * This variable keeps track of the state of the circuit.
 */
static enum {
	CHARGE_CIRCUIT_OK,
	CHARGE_CIRCUIT_WEDGED,
} charge_circuit_state = CHARGE_CIRCUIT_OK;

int extpower_is_present(void)
{
	return gpio_get_level(GPIO_AC_PRESENT);
}

static void extpower_buffer_to_pch(void)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
		/* Drive low in G3 state */
		gpio_set_level(GPIO_PCH_ACOK, 0);
	} else {
		/* Buffer from extpower in S5+ (where 3.3DSW enabled) */
		gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());
	}
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, extpower_buffer_to_pch, HOOK_PRIO_DEFAULT);

static void extpower_shutdown(void)
{
	/* Drive ACOK buffer to PCH low when shutting down */
	gpio_set_level(GPIO_PCH_ACOK, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, extpower_shutdown, HOOK_PRIO_DEFAULT);

void extpower_interrupt(enum gpio_signal signal)
{
	extpower_buffer_to_pch();

	/* Trigger notification of external power change */
	task_wake(TASK_ID_EXTPOWER);
}

static void extpower_init(void)
{
	extpower_buffer_to_pch();

	/* Enable interrupts, now that we've initialized */
	gpio_enable_interrupt(GPIO_AC_PRESENT);
}
DECLARE_HOOK(HOOK_INIT, extpower_init, HOOK_PRIO_DEFAULT);

/*
 * Save power in S3/S5/G3 by disabling charging when the battery is
 * full. Restore charging when battery is not full anymore. This saves
 * power because our input AC path is inefficient.
 */

static void check_charging_cutoff(void)
{
	/* If battery is full disable charging */
	if (charge_get_percent() == 100) {
		charge_is_disabled = 1;
		host_command_pd_send_status(PD_CHARGE_NONE);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, check_charging_cutoff, HOOK_PRIO_DEFAULT);

static void cancel_charging_cutoff(void)
{
	/* If charging is disabled, enable it */
	if (charge_is_disabled) {
		charge_is_disabled = 0;
		host_command_pd_send_status(PD_CHARGE_5V);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, cancel_charging_cutoff, HOOK_PRIO_DEFAULT);

static void batt_soc_change(void)
{
	/* If in S0, leave charging alone */
	if (chipset_in_state(CHIPSET_STATE_ON))
		return;

	/* Check to disable or enable charging based on batt state of charge */
	if (!charge_is_disabled && charge_get_percent() == 100) {
		host_command_pd_send_status(PD_CHARGE_NONE);
		charge_is_disabled = 1;
	} else if (charge_is_disabled && charge_get_percent() < 100) {
		charge_is_disabled = 0;
		host_command_pd_send_status(PD_CHARGE_5V);
	}
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, batt_soc_change, HOOK_PRIO_DEFAULT);

/**
 * Enable/disable NVDC charger to control AC to system and battery.
 */
static void charger_disable(int disable)
{
	int i, rv;

	for (i = 0; i < CHARGER_MODE_ATTEMPTS; i++) {
		rv = charger_discharge_on_ac(disable);
		if (rv == EC_SUCCESS)
			return;
	}

	CPRINTS("Setting learn mode %d failed!", disable);
}

static void allow_max_request(void)
{
	int prochot_status;
	if (charge_circuit_state == CHARGE_CIRCUIT_WEDGED) {
		/* Read PROCHOT status register to clear it */
		i2c_read8(I2C_PORT_CHARGER, BQ24773_ADDR,
			  BQ24773_PROCHOT_STATUS, &prochot_status);
		charge_circuit_state = CHARGE_CIRCUIT_OK;
	}
	host_command_pd_send_status(PD_CHARGE_MAX);
}
DECLARE_DEFERRED(allow_max_request);

static void extpower_board_hacks(int extpower, int extpower_prev)
{
	/* Cancel deferred attempt to enable max charge request */
	hook_call_deferred(allow_max_request, -1);

	/*
	 * When AC is detected, delay briefly before allowing PD
	 * to negotiate up to the max voltage to give charge circuit
	 * time to settle down. When AC goes away, set PD to only allow
	 * 5V charging for the next time AC is connected.
	 *
	 * Use NVDC charger learn mode (charger_disable()) when AC
	 * is not present to avoid backboosting when AC is plugged in.
	 *
	 * When in G3, PP5000 needs to be enabled to accurately sense
	 * CC voltage when AC is attached. When AC is disconnceted
	 * it needs to be off to save power.
	 */
	if (extpower && !extpower_prev) {
		/* AC connected */
		charger_disable(0);
		hook_call_deferred(allow_max_request, 500*MSEC);
		set_pp5000_in_g3(PP5000_IN_G3_AC, 1);
	} else if (extpower && extpower_prev) {
		/*
		 * Glitch on AC_PRESENT, attempt to recover from
		 * backboost
		 */
		host_command_pd_send_status(PD_CHARGE_NONE);
	} else {
		/* AC disconnected */
		if (!charge_is_disabled &&
		    charge_circuit_state == CHARGE_CIRCUIT_OK)
			host_command_pd_send_status(PD_CHARGE_NONE);

		charger_disable(1);

		if (!charge_is_disabled &&
		    charge_circuit_state == CHARGE_CIRCUIT_OK)
			host_command_pd_send_status(PD_CHARGE_5V);

		set_pp5000_in_g3(PP5000_IN_G3_AC, 0);
	}
	extpower_prev = extpower;
}

static void check_charge_wedged(void)
{
	int rv, prochot_status;

	if (charge_circuit_state == CHARGE_CIRCUIT_OK) {
		/* Check PROCHOT warning */
		rv = i2c_read8(I2C_PORT_CHARGER, BQ24773_ADDR,
				BQ24773_PROCHOT_STATUS, &prochot_status);
		if (rv)
			return;

		/*
		 * If PROCHOT is asserted, then charge circuit is wedged, turn
		 * on learn mode and notify PD to disable charging on all ports.
		 * Note: learn mode is critical here because when in this state
		 * backboosting causes >20V on boostin even after PD disables
		 * CHARGE_EN lines.
		 */
		if (prochot_status) {
			host_command_pd_send_status(PD_CHARGE_NONE);
			charge_circuit_state = CHARGE_CIRCUIT_WEDGED;
			CPRINTS("Charge circuit wedged!");
		}
	} else {
		/*
		 * Charge circuit is wedged and we already disabled charging,
		 * Now start to recover from wedged state by allowing 5V.
		 */
		host_command_pd_send_status(PD_CHARGE_5V);
	}
}

/**
 * Task to handle external power change
 */
void extpower_task(void)
{
	int extpower = extpower_is_present();
	int extpower_prev = 0;

	extpower_board_hacks(extpower, extpower_prev);

	/* Enable backboost detection interrupt */
	gpio_enable_interrupt(GPIO_BKBOOST_DET);

	while (1) {
		if (task_wait_event(2*SECOND) == TASK_EVENT_TIMER) {
			/* Periodically check if charge circuit is wedged */
			check_charge_wedged();
		} else {
			/* Must have received power change interrupt */
			extpower = extpower_is_present();

			/* Various board hacks to run on extpower change */
			extpower_board_hacks(extpower, extpower_prev);
			extpower_prev = extpower;

			hook_notify(HOOK_AC_CHANGE);

			/* Forward notification to host */
			host_set_single_event(extpower ?
						EC_HOST_EVENT_AC_CONNECTED :
						EC_HOST_EVENT_AC_DISCONNECTED);
		}
	}
}

void bkboost_det_interrupt(enum gpio_signal signal)
{
	/* Backboost has been detected, save it, and disable interrupt */
	bkboost_detected = 1;
	gpio_disable_interrupt(GPIO_BKBOOST_DET);
}

static int command_backboost_det(int argc, char **argv)
{
	ccprintf("Backboost detected: %d\n", bkboost_detected);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(bkboost, command_backboost_det, NULL,
			"Read backboost detection",
			NULL);
