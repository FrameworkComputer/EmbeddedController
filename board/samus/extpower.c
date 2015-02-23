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

/* Extpower task has been initialized */
static int extpower_task_initialized;

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
	/* Trigger notification of external power change */
	extpower_buffer_to_pch();

	/* Wake extpower task only if task has been initialized */
	if (extpower_task_initialized)
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
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
		return;
	}

	/* Check to disable or enable charging based on batt state of charge */
	if (!charge_is_disabled && charge_get_percent() == 100) {
		host_command_pd_send_status(PD_CHARGE_NONE);
		charge_is_disabled = 1;
	} else if (charge_is_disabled && charge_get_percent() < 100) {
		charge_is_disabled = 0;
		host_command_pd_send_status(PD_CHARGE_5V);
	} else {
		/* Leave charging alone, but update battery SOC */
		host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
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

static void allow_min_charging(void)
{
	if (!charge_is_disabled && charge_circuit_state == CHARGE_CIRCUIT_OK)
		host_command_pd_send_status(PD_CHARGE_5V);
}
DECLARE_DEFERRED(allow_min_charging);

static void extpower_board_hacks(int extpower, int extpower_prev)
{
	/* Cancel deferred attempt to enable max charge request */
	hook_call_deferred(allow_max_request, -1);

	/*
	 * When AC is detected, delay briefly before allowing PD
	 * to negotiate up to the max voltage to give charge circuit
	 * time to settle down. When AC goes away, disable charging
	 * for a brief time, allowing charge state machine time to
	 * see AC has gone away, and then set PD to only allow
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

		hook_call_deferred(allow_min_charging, 100*MSEC);
		set_pp5000_in_g3(PP5000_IN_G3_AC, 0);
	}
	extpower_prev = extpower;
}

/* Return boostin_voltage or negative if error */
static int get_boostin_voltage(void)
{
	/* Static structs to save stack space */
	static struct ec_response_usb_pd_power_info pd_power_ret;
	static struct ec_params_usb_pd_power_info pd_power_args;
	int ret;
	int err;

	/* Boost-in voltage is maximum of voltage now on each port */
	pd_power_args.port = 0;
	err = pd_host_command(EC_CMD_USB_PD_POWER_INFO, 0,
			      &pd_power_args,
			      sizeof(struct ec_params_usb_pd_power_info),
			      &pd_power_ret,
			      sizeof(struct ec_response_usb_pd_power_info));
	if (err < 0)
		return err;
	ret = pd_power_ret.meas.voltage_now;

	pd_power_args.port = 1;
	err = pd_host_command(EC_CMD_USB_PD_POWER_INFO, 0,
			      &pd_power_args,
			      sizeof(struct ec_params_usb_pd_power_info),
			      &pd_power_ret,
			      sizeof(struct ec_response_usb_pd_power_info));
	if (err < 0)
		return err;

	/* Get max of two measuremente */
	if (pd_power_ret.meas.voltage_now > ret)
		ret = pd_power_ret.meas.voltage_now;

	return ret;
}

/*
 * Send command to PD to write a custom persistent log entry indicating that
 * charging was wedged. Returns pd_host_command success status.
 */
static int log_charge_wedged(void)
{
	static struct ec_params_pd_write_log_entry log_args;

	log_args.type = PD_EVENT_MCU_BOARD_CUSTOM;
	log_args.port = 0;

	return pd_host_command(EC_CMD_PD_WRITE_LOG_ENTRY, 0,
			       &log_args,
			       sizeof(struct ec_params_pd_write_log_entry),
			       NULL, 0);
}

/* Time interval between checking if charge circuit is wedged */
#define CHARGE_WEDGE_CHECK_INTERVAL (2*SECOND)

/*
 * Number of iterations through check_charge_wedged() with charging stalled
 * before attempting unwedge.
 */
#define CHARGE_STALLED_COUNT 5
/*
 * Number of iterations through check_charge_wedged() with charging stalled
 * after we already just tried unwedging the circuit, before we try again.
 */
#define CHARGE_STALLED_REPEATEDLY_COUNT 60

/*
 * Minimum number of iterations through check_charge_wedged() between
 * unwedge attempts.
 */
#define MIN_COUNTS_BETWEEN_UNWEDGES 3

static void check_charge_wedged(void)
{
	int rv, prochot_status, batt_discharging_on_ac, boostin_voltage = 0;
	static int counts_since_wedged;
	static int charge_stalled_count = CHARGE_STALLED_COUNT;
	uint8_t *batt_flags = host_get_memmap(EC_MEMMAP_BATT_FLAG);

	if (charge_circuit_state == CHARGE_CIRCUIT_OK) {
		/* Check PROCHOT warning */
		rv = i2c_read8(I2C_PORT_CHARGER, BQ24773_ADDR,
				BQ24773_PROCHOT_STATUS, &prochot_status);
		if (rv)
			prochot_status = 0;

		batt_discharging_on_ac =
			(*batt_flags & EC_BATT_FLAG_AC_PRESENT) &&
			(*batt_flags & EC_BATT_FLAG_DISCHARGING);

		/*
		 * If PROCHOT is set or we are discharging on AC, then we
		 * need to know boostin_voltage.
		 */
		if (prochot_status || batt_discharging_on_ac)
			boostin_voltage = get_boostin_voltage();

		/*
		 * If AC is present, and battery is discharging, and
		 * boostin voltage is above 5V, then we might be wedged.
		 */
		if (batt_discharging_on_ac) {
			if (boostin_voltage > 6000)
				charge_stalled_count--;
			else if (boostin_voltage >= 0)
				charge_stalled_count = CHARGE_STALLED_COUNT;
			/* If boostin_voltage < 0, don't change stalled count */
		} else {
			charge_stalled_count = CHARGE_STALLED_COUNT;
		}

		/*
		 * If we were recently wedged, then give ourselves a free pass
		 * here. This gives an opportunity for reading the PROCHOT
		 * status to clear it if the error has gone away.
		 */
		if (counts_since_wedged < MIN_COUNTS_BETWEEN_UNWEDGES)
			counts_since_wedged++;

		/*
		 * If PROCHOT is asserted AND boost_in voltage is above 5V,
		 * then charge circuit is wedged. If charging has been stalled
		 * long enough, then also consider the circuit wedged.
		 *
		 * To unwedge the charge circuit turn on learn mode and notify
		 * PD to disable charging on all ports.
		 * Note: learn mode is critical here because when in this state
		 * backboosting causes >20V on boostin even after PD disables
		 * CHARGE_EN lines.
		 */
		if ((prochot_status && boostin_voltage > 6000 &&
			counts_since_wedged >= MIN_COUNTS_BETWEEN_UNWEDGES) ||
		    charge_stalled_count <= 0) {
			counts_since_wedged = 0;
			host_command_pd_send_status(PD_CHARGE_NONE);
			charger_disable(1);
			charge_circuit_state = CHARGE_CIRCUIT_WEDGED;
			log_charge_wedged();
			CPRINTS("Charge wedged! PROCHOT %02x, Stalled: %d",
				prochot_status, charge_stalled_count);

			/*
			 * If this doesn't clear the problem, then start
			 * the stall counter higher so that we don't retry
			 * unwedging for a while. Note, if we do start charging
			 * properly, then stall counter will be set to
			 * default, so that we will trigger faster the first
			 * time it stalls out.
			 */
			charge_stalled_count = CHARGE_STALLED_REPEATEDLY_COUNT;
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
	extpower_prev = extpower;
	extpower_task_initialized = 1;

	/* Enable backboost detection interrupt */
	gpio_enable_interrupt(GPIO_BKBOOST_DET);

	while (1) {
		if (task_wait_event(CHARGE_WEDGE_CHECK_INTERVAL) ==
		    TASK_EVENT_TIMER) {
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
