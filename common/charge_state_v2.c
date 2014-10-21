/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery charging task and state machine.
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "math_util.h"
#include "printf.h"
#include "sb_fw_update.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

#define LOW_BATTERY_SHUTDOWN_TIMEOUT_US (LOW_BATTERY_SHUTDOWN_TIMEOUT * SECOND)
#define PRECHARGE_TIMEOUT_US (PRECHARGE_TIMEOUT * SECOND)
#define LFCC_EVENT_THRESH 5 /* Full-capacity change reqd for host event */

/*
 * State for charger_task(). Here so we can reset it on a HOOK_INIT, and
 * because stack space is more limited than .bss
 */
static const struct battery_info *batt_info;
static struct charge_state_data curr;
static int prev_ac, prev_charge;
static int state_machine_force_idle;
static int manual_mode;  /* volt/curr are no longer maintained by charger */
static unsigned int user_current_limit = -1U;
test_export_static timestamp_t shutdown_warning_time;
static timestamp_t precharge_start_time;
static int battery_seems_to_be_dead;
static int battery_seems_to_be_disconnected;
static int problems_exist;
static int debugging;

/* Track problems in communicating with the battery or charger */
enum problem_type {
	PR_STATIC_UPDATE,
	PR_SET_VOLTAGE,
	PR_SET_CURRENT,
	PR_SET_MODE,
	PR_SET_INPUT_CURR,
	PR_POST_INIT,
	PR_CHG_FLAGS,
	PR_BATT_FLAGS,
	PR_CUSTOM,

	NUM_PROBLEM_TYPES
};
static const char * const prob_text[] = {
	"static update",
	"set voltage",
	"set current",
	"set mode",
	"set input current",
	"post init",
	"chg params",
	"batt params",
	"custom profile",
};
BUILD_ASSERT(ARRAY_SIZE(prob_text) == NUM_PROBLEM_TYPES);

/*
 * TODO(crosbug.com/p/27639): When do we decide a problem is real and not
 * just intermittent? And what do we do about it?
 */
static void problem(enum problem_type p, int v)
{
	static int last_prob_val[NUM_PROBLEM_TYPES];
	static timestamp_t last_prob_time[NUM_PROBLEM_TYPES];
	timestamp_t t_now, t_diff;

	if (last_prob_val[p] != v) {
		t_now = get_time();
		t_diff.val = t_now.val - last_prob_time[p].val;
		CPRINTS("charge problem: %s, 0x%x -> 0x%x after %.6lds",
			 prob_text[p], last_prob_val[p], v, t_diff.val);
		last_prob_val[p] = v;
		last_prob_time[p] = t_now;
	}
	problems_exist = 1;
}

/* Returns zero if every item was updated. */
static int update_static_battery_info(void)
{
	char *batt_str;
	int batt_serial;
	/*
	 * The return values have type enum ec_error_list, but EC_SUCCESS is
	 * zero. We'll just look for any failures so we can try them all again.
	 */
	int rv;

	/* Smart battery serial number is 16 bits */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_SERIAL);
	memset(batt_str, 0, EC_MEMMAP_TEXT_MAX);
	rv = battery_serial_number(&batt_serial);
	if (!rv)
		snprintf(batt_str, EC_MEMMAP_TEXT_MAX, "%04X", batt_serial);

	/* Design Capacity of Full */
	rv |= battery_design_capacity(
		(int *)host_get_memmap(EC_MEMMAP_BATT_DCAP));

	/* Design Voltage */
	rv |= battery_design_voltage(
		(int *)host_get_memmap(EC_MEMMAP_BATT_DVLT));

	/* Last Full Charge Capacity (this is only mostly static) */
	rv |= battery_full_charge_capacity(
		(int *)host_get_memmap(EC_MEMMAP_BATT_LFCC));

	/* Cycle Count */
	rv |= battery_cycle_count((int *)host_get_memmap(EC_MEMMAP_BATT_CCNT));

	/* Battery Manufacturer string */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_MFGR);
	memset(batt_str, 0, EC_MEMMAP_TEXT_MAX);
	rv |= battery_manufacturer_name(batt_str, EC_MEMMAP_TEXT_MAX);

	/* Battery Model string */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_MODEL);
	memset(batt_str, 0, EC_MEMMAP_TEXT_MAX);
	rv |= battery_device_name(batt_str, EC_MEMMAP_TEXT_MAX);

	/* Battery Type string */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_TYPE);
	rv |= battery_device_chemistry(batt_str, EC_MEMMAP_TEXT_MAX);

	/* Zero the dynamic entries. They'll come next. */
	*(int *)host_get_memmap(EC_MEMMAP_BATT_VOLT) = 0;
	*(int *)host_get_memmap(EC_MEMMAP_BATT_RATE) = 0;
	*(int *)host_get_memmap(EC_MEMMAP_BATT_CAP) = 0;
	*(int *)host_get_memmap(EC_MEMMAP_BATT_LFCC) = 0;
	*host_get_memmap(EC_MEMMAP_BATT_FLAG) = 0;

	if (rv)
		problem(PR_STATIC_UPDATE, 0);
	else
		/* No errors seen. Battery data is now present */
		*host_get_memmap(EC_MEMMAP_BATTERY_VERSION) = 1;

	return rv;
}

static void update_dynamic_battery_info(void)
{
	/* The memmap address is constant. We should fix these calls somehow. */
	int *memmap_volt = (int *)host_get_memmap(EC_MEMMAP_BATT_VOLT);
	int *memmap_rate = (int *)host_get_memmap(EC_MEMMAP_BATT_RATE);
	int *memmap_cap = (int *)host_get_memmap(EC_MEMMAP_BATT_CAP);
	int *memmap_lfcc = (int *)host_get_memmap(EC_MEMMAP_BATT_LFCC);
	uint8_t *memmap_flags = host_get_memmap(EC_MEMMAP_BATT_FLAG);
	uint8_t tmp;
	int cap_changed;

	tmp = 0;
	if (curr.ac)
		tmp |= EC_BATT_FLAG_AC_PRESENT;

	if (curr.batt.is_present == BP_YES)
		tmp |= EC_BATT_FLAG_BATT_PRESENT;

	if (!(curr.batt.flags & BATT_FLAG_BAD_VOLTAGE))
		*memmap_volt = curr.batt.voltage;

	if (!(curr.batt.flags & BATT_FLAG_BAD_CURRENT))
		*memmap_rate = ABS(curr.batt.current);

	if (!(curr.batt.flags & BATT_FLAG_BAD_REMAINING_CAPACITY))
		*memmap_cap = curr.batt.remaining_capacity;

	cap_changed = 0;
	if (!(curr.batt.flags & BATT_FLAG_BAD_FULL_CAPACITY) &&
	    (curr.batt.full_capacity <= (*memmap_lfcc - LFCC_EVENT_THRESH) ||
	     curr.batt.full_capacity >= (*memmap_lfcc + LFCC_EVENT_THRESH))) {
		*memmap_lfcc = curr.batt.full_capacity;
		cap_changed = 1;
	}

	if (curr.batt.is_present == BP_YES &&
	    !(curr.batt.flags & BATT_FLAG_BAD_STATE_OF_CHARGE) &&
	    curr.batt.state_of_charge <= BATTERY_LEVEL_CRITICAL)
		tmp |= EC_BATT_FLAG_LEVEL_CRITICAL;

	switch (curr.state) {
	case ST_DISCHARGE:
		tmp |= EC_BATT_FLAG_DISCHARGING;
		break;
	case ST_CHARGE:
		tmp |= EC_BATT_FLAG_CHARGING;
		break;
	default:
		/* neither charging nor discharging */
		break;
	}

	/* Tell the AP to re-read battery status if charge state changes */
	if (*memmap_flags != tmp)
		host_set_single_event(EC_HOST_EVENT_BATTERY_STATUS);
	*memmap_flags = tmp;

	/* Poke the AP if the full_capacity changes. */
	if (cap_changed)
		host_set_single_event(EC_HOST_EVENT_BATTERY);
}

static const char * const state_list[] = {
	"idle", "discharge", "charge", "precharge"
};
BUILD_ASSERT(ARRAY_SIZE(state_list) == NUM_STATES_V2);
static const char * const batt_pres[] = {
	"NO", "YES", "NOT_SURE",
};

static void dump_charge_state(void)
{
#define DUMP(FLD, FMT) ccprintf(#FLD " = " FMT "\n", curr.FLD)
#define DUMP_CHG(FLD, FMT) ccprintf("\t" #FLD " = " FMT "\n", curr.chg. FLD)
#define DUMP_BATT(FLD, FMT) ccprintf("\t" #FLD " = " FMT "\n", curr.batt. FLD)
	ccprintf("state = %s\n", state_list[curr.state]);
	DUMP(ac, "%d");
	ccprintf("chg.*:\n");
	DUMP_CHG(voltage, "%dmV");
	DUMP_CHG(current, "%dmA");
	DUMP_CHG(input_current, "%dmA");
	DUMP_CHG(status, "0x%x");
	DUMP_CHG(option, "0x%x");
	DUMP_CHG(flags, "0x%x");
	ccprintf("batt.*:\n");
	ccprintf("\ttemperature = %dC\n",
		 DECI_KELVIN_TO_CELSIUS(curr.batt.temperature));
	DUMP_BATT(state_of_charge, "%d%%");
	DUMP_BATT(voltage, "%dmV");
	DUMP_BATT(current, "%dmA");
	DUMP_BATT(desired_voltage, "%dmV");
	DUMP_BATT(desired_current, "%dmA");
	DUMP_BATT(flags, "0x%x");
	DUMP_BATT(remaining_capacity, "%dmAh");
	DUMP_BATT(full_capacity, "%dmAh");
	ccprintf("\tis_present = %s\n", batt_pres[curr.batt.is_present]);
	DUMP(requested_voltage, "%dmV");
	DUMP(requested_current, "%dmA");
	ccprintf("force_idle = %d\n", state_machine_force_idle);
	ccprintf("manual_mode = %d\n", manual_mode);
	ccprintf("user_current_limit = %dmA\n", user_current_limit);
	ccprintf("battery_seems_to_be_dead = %d\n", battery_seems_to_be_dead);
	ccprintf("battery_seems_to_be_disconnected = %d\n",
		 battery_seems_to_be_disconnected);
	ccprintf("debug output = %s\n", debugging ? "on" : "off");
#undef DUMP
}

static void show_charging_progress(void)
{
	int rv, minutes, to_full;

	if (curr.state == ST_IDLE ||
	    curr.state == ST_DISCHARGE) {
		rv = battery_time_to_empty(&minutes);
		to_full = 0;
	} else {
		rv = battery_time_to_full(&minutes);
		to_full = 1;
	}

	if (rv)
		CPRINTS("Battery %d%% / ??h:?? %s",
			curr.batt.state_of_charge,
			to_full ? "to full" : "to empty");
	else
		CPRINTS("Battery %d%% / %dh:%d %s",
			curr.batt.state_of_charge,
			minutes / 60, minutes % 60,
			to_full ? "to full" : "to empty");

	if (debugging) {
		ccprintf("battery:\n");
		print_battery_debug();
		ccprintf("charger:\n");
		print_charger_debug();
		ccprintf("chg:\n");
		dump_charge_state();
	}
}

/*
 * Ask the charger for some voltage and current. If either value is 0,
 * charging is disabled; otherwise it's enabled. Negative values are ignored.
 */
static int charge_request(int voltage, int current)
{
	int r1 = EC_SUCCESS, r2 = EC_SUCCESS, r3 = EC_SUCCESS;
	static int prev_volt, prev_curr;

	if (!voltage || !current)
		voltage = current = 0;

	if (prev_volt != voltage || prev_curr != current)
		CPRINTS("%s(%dmV, %dmA)", __func__, voltage, current);

	if (voltage >= 0)
		r1 = charger_set_voltage(voltage);
	if (r1 != EC_SUCCESS)
		problem(PR_SET_VOLTAGE, r1);

	if (current >= 0)
		r2 = charger_set_current(current);
	if (r2 != EC_SUCCESS)
		problem(PR_SET_CURRENT, r2);

	/*
	 * Set the charge inhibit bit when possible as it appears to save
	 * power in some cases (e.g. Nyan with BQ24735).
	 */
	if (voltage > 0 || current > 0)
		r3 = charger_set_mode(0);
	else
		r3 = charger_set_mode(CHARGE_FLAG_INHIBIT_CHARGE);
	if (r3 != EC_SUCCESS)
		problem(PR_SET_MODE, r3);

	/*
	 * Only update if the request worked, so we'll keep trying on failures.
	 */
	if (!r1 && !r2) {
		prev_volt = voltage;
		prev_curr = current;
	}

	return r1 ? r1 : r2;
}


/* Force charging off before the battery is full. */
static int charge_force_idle(int enable)
{
	/*
	 * Force idle is only meaningful if external power is
	 * present. If it's not present we can't charge anyway.
	 */
	if (enable && !curr.ac)
		return EC_ERROR_NOT_POWERED;

	state_machine_force_idle = enable;
	if (enable) {
		charge_request(0, 0);
		manual_mode = 1;
	} else {
		manual_mode = 0;
	}
	return EC_SUCCESS;
}

static void prevent_hot_discharge(void)
{
	int batt_temp_c;

	/* If the AP is off already, the thermal task should handle it. */
	if (!chipset_in_state(CHIPSET_STATE_ON))
		return;

	/* Same if we can't read the battery temp. */
	if (curr.batt.flags & BATT_FLAG_BAD_TEMPERATURE)
		return;

	/*
	 * TODO(crosbug.com/p/27641): Shouldn't we do this in stages, like
	 * prevent_deep_discharge()?  Send an event, give the AP time to react,
	 * maybe even hibernate the EC if things are really bad?
	 *
	 * TODO(crosbug.com/p/27642): The thermal loop should watch the battery
	 * temp anyway, so it can turn fans on. It could also force an AP
	 * shutdown if it's too hot, but AFAIK we don't have anything in place
	 * to do a battery shutdown if it's really really hot. We probably
	 * should, just in case.
	 */
	batt_temp_c = DECI_KELVIN_TO_CELSIUS(curr.batt.temperature);
	if (batt_temp_c > batt_info->discharging_max_c ||
	    batt_temp_c < batt_info->discharging_min_c) {
		CPRINTS("charge force shutdown due to battery temp %dC",
			batt_temp_c);
		chipset_force_shutdown();
		host_set_single_event(EC_HOST_EVENT_BATTERY_SHUTDOWN);
	}
}

/* True if we know the charge is too low, or we know the voltage is too low. */
static inline int battery_too_low(void)
{
	return ((!(curr.batt.flags & BATT_FLAG_BAD_STATE_OF_CHARGE) &&
		 curr.batt.state_of_charge < BATTERY_LEVEL_SHUTDOWN) ||
		(!(curr.batt.flags & BATT_FLAG_BAD_VOLTAGE) &&
		 curr.batt.voltage <= batt_info->voltage_min));
}

/* Shut everything down before the battery completely dies. */
static void prevent_deep_discharge(void)
{
	if (!battery_too_low()) {
		/* Reset shutdown warning time */
		shutdown_warning_time.val = 0;
		return;
	}

	CPRINTS("Low battery: %d%%, %dmV",
		curr.batt.state_of_charge, curr.batt.voltage);

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
#ifdef CONFIG_HIBERNATE
		/* AP is off, so shut down the EC now */
		CPRINTS("charge force EC hibernate due to low battery");
		system_hibernate(0, 0);
#endif
	} else if (!shutdown_warning_time.val) {
		/* Warn AP battery level is so low we'll shut down */
		CPRINTS("charge warn shutdown due to low battery");
		shutdown_warning_time = get_time();
		host_set_single_event(EC_HOST_EVENT_BATTERY_SHUTDOWN);
	} else if (get_time().val > shutdown_warning_time.val +
		   LOW_BATTERY_SHUTDOWN_TIMEOUT_US) {
		/* Timeout waiting for AP to shut down, so kill it */
		CPRINTS("charge force shutdown due to low battery");
		chipset_force_shutdown();
	}
}

/*
 * Send host events as the battery charge drops below certain thresholds.
 * We handle forced shutdown and other actions elsewhere; this is just for the
 * host events. We send these even if the AP is off, since the AP will read and
 * discard any events it doesn't care about the next time it wakes up.
 */
static void notify_host_of_low_battery(void)
{
	/* We can't tell what the current charge is. Assume it's okay. */
	if (curr.batt.flags & BATT_FLAG_BAD_STATE_OF_CHARGE)
		return;

	if (curr.batt.state_of_charge <= BATTERY_LEVEL_LOW &&
	    prev_charge > BATTERY_LEVEL_LOW)
		host_set_single_event(EC_HOST_EVENT_BATTERY_LOW);

	if (curr.batt.state_of_charge <= BATTERY_LEVEL_CRITICAL &&
	    prev_charge > BATTERY_LEVEL_CRITICAL)
		host_set_single_event(EC_HOST_EVENT_BATTERY_CRITICAL);
}

const struct batt_params *charger_current_battery_params(void)
{
	return &curr.batt;
}

void charger_init(void)
{
	/* Initialize current state */
	memset(&curr, 0, sizeof(curr));
	curr.batt.is_present = BP_NOT_SURE;
	curr.desired_input_current = CONFIG_CHARGER_INPUT_CURRENT;
}
DECLARE_HOOK(HOOK_INIT, charger_init, HOOK_PRIO_DEFAULT);

/* Main loop */
void charger_task(void)
{
	int sleep_usec;
	int need_static = 1;

	/* Get the battery-specific values */
	batt_info = battery_get_info();

	prev_ac = prev_charge = -1;
	state_machine_force_idle = 0;
	shutdown_warning_time.val = 0UL;
	battery_seems_to_be_dead = 0;

	while (1) {

#ifdef CONFIG_SB_FIRMWARE_UPDATE
		if (sb_fw_update_in_progress()) {
			task_wait_event(CHARGE_MAX_SLEEP_USEC);
			continue;
		}
#endif

		/* Let's see what's going on... */
		curr.ts = get_time();
		sleep_usec = 0;
		problems_exist = 0;
		curr.ac = extpower_is_present();
		if (curr.ac != prev_ac) {
			if (curr.ac) {
				/*
				 * Some chargers are unpowered when the AC is
				 * off, so we'll reinitialize it when AC
				 * comes back and set the input current limit.
				 * Try again if it fails.
				 */
				int rv = charger_post_init();
				if (rv != EC_SUCCESS) {
					problem(PR_POST_INIT, rv);
				} else {
					rv = charger_set_input_current(
						curr.desired_input_current);
					if (rv != EC_SUCCESS)
						problem(PR_SET_INPUT_CURR, rv);
					else
						prev_ac = curr.ac;
				}
			} else {
				/* Some things are only meaningful on AC */
				state_machine_force_idle = 0;
				battery_seems_to_be_dead = 0;
				prev_ac = curr.ac;
			}
		}
		charger_get_params(&curr.chg);
		battery_get_params(&curr.batt);

		/*
		 * TODO(crosbug.com/p/27527). Sometimes the battery thinks its
		 * temperature is 6280C, which seems a bit high. Let's ignore
		 * anything above the boiling point of tungsten until this bug
		 * is fixed. If the battery is really that warm, we probably
		 * have more urgent problems.
		 */
		if (curr.batt.temperature > CELSIUS_TO_DECI_KELVIN(5660)) {
			CPRINTS("ignoring ridiculous batt.temp of %dC",
				 DECI_KELVIN_TO_CELSIUS(curr.batt.temperature));
			curr.batt.flags |= BATT_FLAG_BAD_TEMPERATURE;
		}

		/*
		 * Now decide what we want to do about it. We'll normally just
		 * pass along whatever the battery wants to the charger. Note
		 * that if battery_get_params() can't get valid values from the
		 * battery it uses (0, 0), which is probably safer than blindly
		 * applying power to a battery we can't talk to.
		 */
		curr.requested_voltage = curr.batt.desired_voltage;
		curr.requested_current = curr.batt.desired_current;

		/* If we *know* there's no battery, wait for one to appear. */
		if (curr.batt.is_present == BP_NO) {
			ASSERT(curr.ac);	/* How are we running? */
			curr.state = ST_IDLE;
			goto wait_for_it;
		}

		/*
		 * If we had trouble talking to the battery or the charger, we
		 * should probably do nothing for a bit, and if it doesn't get
		 * better then flag it as an error.
		 */
		if (curr.chg.flags & CHG_FLAG_BAD_ANY)
			problem(PR_CHG_FLAGS, curr.chg.flags);
		if (curr.batt.flags & BATT_FLAG_BAD_ANY)
			problem(PR_BATT_FLAGS, curr.batt.flags);

		if (!curr.ac) {
			curr.state = ST_DISCHARGE;
			/* Don't let the battery hurt itself. */
			prevent_hot_discharge();
			prevent_deep_discharge();
			goto wait_for_it;
		}

		/* Okay, we're on AC and we should have a battery. */

		/* Used for factory tests. */
		if (state_machine_force_idle) {
			curr.state = ST_IDLE;
			goto wait_for_it;
		}

		/* If the battery is not responsive, try to wake it up. */
		if (!(curr.batt.flags & BATT_FLAG_RESPONSIVE)) {
			if (battery_seems_to_be_dead) {
				/* It's dead, do nothing */
				curr.state = ST_IDLE;
				curr.requested_voltage = 0;
				curr.requested_current = 0;
			} else if (curr.state == ST_PRECHARGE &&
				   (get_time().val > precharge_start_time.val +
				    PRECHARGE_TIMEOUT_US)) {
				/* We've tried long enough, give up */
				CPRINTS("battery seems to be dead");
				battery_seems_to_be_dead = 1;
				curr.state = ST_IDLE;
				curr.requested_voltage = 0;
				curr.requested_current = 0;
			} else {
				/* See if we can wake it up */
				if (curr.state != ST_PRECHARGE) {
					CPRINTS("try to wake battery");
					precharge_start_time = get_time();
					need_static = 1;
				}
				curr.state = ST_PRECHARGE;
				curr.requested_voltage =
					batt_info->voltage_max;
				curr.requested_current =
					batt_info->precharge_current;
			}
			goto wait_for_it;
		} else {
			/* The battery is responding. Yay. Try to use it. */
#ifdef CONFIG_BATTERY_REQUESTS_NIL_WHEN_DEAD
			/*
			 * TODO (crosbug.com/p/29467): remove this workaround
			 * for dead battery that requests no voltage/current
			 */
			if (curr.requested_voltage == 0 &&
			    curr.requested_current == 0 &&
			    curr.batt.state_of_charge == 0) {
				/* Battery is dead, give precharge current */
				curr.requested_voltage =
					batt_info->voltage_max;
				curr.requested_current =
					batt_info->precharge_current;
			} else
#endif
#ifdef CONFIG_BATTERY_REVIVE_DISCONNECT
			battery_seems_to_be_disconnected = 0;

			if (curr.requested_voltage == 0 &&
			    curr.requested_current == 0 &&
			    battery_get_disconnect_state() ==
			    BATTERY_DISCONNECTED) {
				/*
				 * Battery is in disconnect state. Apply a
				 * current to kick it out of this state.
				 */
				CPRINTS("found battery in disconnect state");
				curr.requested_voltage =
					batt_info->voltage_max;
				curr.requested_current =
					batt_info->precharge_current;
				battery_seems_to_be_disconnected = 1;
			} else
#endif
			if (curr.state == ST_PRECHARGE ||
			    battery_seems_to_be_dead) {
				CPRINTS("battery woke up");

				/* Update the battery-specific values */
				batt_info = battery_get_info();
				need_static = 1;
			}

			battery_seems_to_be_dead = 0;
			curr.state = ST_CHARGE;
		}

		/*
		 * If battery seems to be disconnected, we need to get it
		 * out of that state, even if the charge level is full.
		 */
		if (curr.batt.state_of_charge >= BATTERY_LEVEL_FULL &&
		    !battery_seems_to_be_disconnected) {
			/* Full up. Stop charging */
			curr.state = ST_IDLE;
			goto wait_for_it;
		}

		/*
		 * TODO(crosbug.com/p/27643): Quit trying if charging too long
		 * without getting full (CONFIG_CHARGER_TIMEOUT_HOURS).
		 */

#ifdef CONFIG_CHARGER_PROFILE_OVERRIDE
		sleep_usec = charger_profile_override(&curr);
		if (sleep_usec < 0)
			problem(PR_CUSTOM, sleep_usec);
#endif

wait_for_it:
		/* Keep the AP informed */
		if (need_static)
			need_static = update_static_battery_info();
		/* Wait on the dynamic info until the static info is good. */
		if (!need_static)
			update_dynamic_battery_info();
		notify_host_of_low_battery();

		/* And the EC console */
		if (!(curr.batt.flags & BATT_FLAG_BAD_STATE_OF_CHARGE) &&
		    curr.batt.state_of_charge != prev_charge) {
			show_charging_progress();
			prev_charge = curr.batt.state_of_charge;
		}

		/* Turn charger off if it's not needed */
		if (curr.state == ST_IDLE || curr.state == ST_DISCHARGE) {
			curr.requested_voltage = 0;
			curr.requested_current = 0;
		}

		/* Apply external limits */
		if (curr.requested_current > user_current_limit)
			curr.requested_current = user_current_limit;

		/* Round to valid values */
		curr.requested_voltage =
			charger_closest_voltage(curr.requested_voltage);
		curr.requested_current =
			charger_closest_current(curr.requested_current);

		/* Charger only accpets request when AC is on. */
		if (curr.ac) {
			/*
			 * Some batteries would wake up after cut-off if we keep
			 * charging it. Thus, we only charge when AC is on and
			 * battery is not cut off yet.
			 */
			if (battery_is_cut_off())
				charge_request(0, 0);
			/*
			 * As a safety feature, some chargers will stop
			 * charging if we don't communicate with it frequently
			 * enough. In manual mode, we'll just tell it what it
			 * knows.
			 */
			else if (manual_mode) {
				charge_request(curr.chg.voltage,
					       curr.chg.current);
			} else {
				charge_request(curr.requested_voltage,
					       curr.requested_current);
			}
		}

		/* How long to sleep? */
		if (problems_exist)
			/* If there are errors, don't wait very long. */
			sleep_usec = CHARGE_POLL_PERIOD_SHORT;
		else if (sleep_usec <= 0) {
			/* default values depend on the state */
			if (curr.state == ST_IDLE ||
			    curr.state == ST_DISCHARGE) {
				/* If AP is off, we can sleep a long time */
				if (chipset_in_state(CHIPSET_STATE_ANY_OFF |
						     CHIPSET_STATE_SUSPEND))
					sleep_usec =
						CHARGE_POLL_PERIOD_VERY_LONG;
				else
					/* Discharging, not too urgent */
					sleep_usec = CHARGE_POLL_PERIOD_LONG;
			} else {
				/* Charging, so pay closer attention */
				sleep_usec = CHARGE_POLL_PERIOD_CHARGE;
			}
		}

		/* Adjust for time spent in this loop */
		sleep_usec -= (int)(get_time().val - curr.ts.val);
		if (sleep_usec < CHARGE_MIN_SLEEP_USEC)
			sleep_usec = CHARGE_MIN_SLEEP_USEC;
		else if (sleep_usec > CHARGE_MAX_SLEEP_USEC)
			sleep_usec = CHARGE_MAX_SLEEP_USEC;

		task_wait_event(sleep_usec);
	}
}


/*****************************************************************************/
/* Exported functions */

int charge_want_shutdown(void)
{
	return (curr.state == ST_DISCHARGE) &&
		!(curr.batt.flags & BATT_FLAG_BAD_STATE_OF_CHARGE) &&
		(curr.batt.state_of_charge < BATTERY_LEVEL_SHUTDOWN);
}

enum charge_state charge_get_state(void)
{
	switch (curr.state) {
	case ST_IDLE:
		if (battery_seems_to_be_dead)
			return PWR_STATE_ERROR;
		return PWR_STATE_IDLE;
	case ST_DISCHARGE:
		return PWR_STATE_DISCHARGE;
	case ST_CHARGE:
		/* The only difference here is what the LEDs display. */
		if (curr.batt.state_of_charge >= BATTERY_LEVEL_NEAR_FULL)
			return PWR_STATE_CHARGE_NEAR_FULL;
		else
			return PWR_STATE_CHARGE;
	default:
		/* Anything else can be considered an error for LED purposes */
		return PWR_STATE_ERROR;
	}
}

uint32_t charge_get_flags(void)
{
	uint32_t flags = 0;

	if (state_machine_force_idle)
		flags |= CHARGE_FLAG_FORCE_IDLE;
	if (curr.ac)
		flags |= CHARGE_FLAG_EXTERNAL_POWER;
	if (curr.batt.flags & BATT_FLAG_RESPONSIVE)
		flags |= CHARGE_FLAG_BATT_RESPONSIVE;

	return flags;
}

int charge_get_percent(void)
{
	/*
	 * Since there's no way to indicate an error to the caller, we'll just
	 * return the last known value. Even if we've never been able to talk
	 * to the battery, that'll be zero, which is probably as good as
	 * anything.
	 */
	return curr.batt.state_of_charge;
}

int charge_temp_sensor_get_val(int idx, int *temp_ptr)
{
	if (curr.batt.flags & BATT_FLAG_BAD_TEMPERATURE)
		return EC_ERROR_UNKNOWN;

	/* Battery temp is 10ths of degrees K, temp wants degrees K */
	*temp_ptr = curr.batt.temperature / 10;
	return EC_SUCCESS;
}

int charge_set_input_current_limit(int ma)
{
	curr.desired_input_current = ma;
	return charger_set_input_current(ma);
}

/*****************************************************************************/
/* Hooks */

/* Wake up the task when something important happens */
static void charge_wakeup(void)
{
	task_wake(TASK_ID_CHARGER);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, charge_wakeup, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_AC_CHANGE, charge_wakeup, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Host commands */

static int charge_command_charge_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_charge_control *p = args->params;
	int rv;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	rv = charge_force_idle(p->mode != CHARGE_CONTROL_NORMAL);
	if (rv != EC_SUCCESS)
		return rv;

#ifdef CONFIG_CHARGER_DISCHARGE_ON_AC
	rv = board_discharge_on_ac(p->mode == CHARGE_CONTROL_DISCHARGE);
	if (rv != EC_SUCCESS)
		return rv;
#endif

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHARGE_CONTROL, charge_command_charge_control,
		     EC_VER_MASK(1));

static void reset_current_limit(void)
{
	user_current_limit = -1U;
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, reset_current_limit, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, reset_current_limit, HOOK_PRIO_DEFAULT);

static int charge_command_current_limit(struct host_cmd_handler_args *args)
{
	const struct ec_params_current_limit *p = args->params;

	user_current_limit = p->limit;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHARGE_CURRENT_LIMIT, charge_command_current_limit,
		     EC_VER_MASK(0));

static int charge_command_charge_state(struct host_cmd_handler_args *args)
{
	const struct ec_params_charge_state *in = args->params;
	struct ec_response_charge_state *out = args->response;
	uint32_t val;
	int rv = EC_RES_SUCCESS;

	switch (in->cmd) {

	case CHARGE_STATE_CMD_GET_STATE:
		out->get_state.ac = curr.ac;
		out->get_state.chg_voltage = curr.chg.voltage;
		out->get_state.chg_current = curr.chg.current;
		out->get_state.chg_input_current = curr.chg.input_current;
		out->get_state.batt_state_of_charge = curr.batt.state_of_charge;
		args->response_size = sizeof(out->get_state);
		break;

	case CHARGE_STATE_CMD_GET_PARAM:
		val = 0;
#ifdef CONFIG_CHARGER_PROFILE_OVERRIDE
		/* custom profile params */
		if (in->get_param.param >= CS_PARAM_CUSTOM_PROFILE_MIN &&
		    in->get_param.param <= CS_PARAM_CUSTOM_PROFILE_MAX) {
			rv  = charger_profile_override_get_param(
				in->get_param.param, &val);
		} else
#endif
			/* standard params */
			switch (in->get_param.param) {
			case CS_PARAM_CHG_VOLTAGE:
				val = curr.chg.voltage;
				break;
			case CS_PARAM_CHG_CURRENT:
				val = curr.chg.current;
				break;
			case CS_PARAM_CHG_INPUT_CURRENT:
				val = curr.chg.input_current;
				break;
			case CS_PARAM_CHG_STATUS:
				val = curr.chg.status;
				break;
			case CS_PARAM_CHG_OPTION:
				val = curr.chg.option;
				break;
			default:
				rv = EC_RES_INVALID_PARAM;
			}

		/* got something */
		out->get_param.value = val;
		args->response_size = sizeof(out->get_param);
		break;

	case CHARGE_STATE_CMD_SET_PARAM:
		val = in->set_param.value;
#ifdef CONFIG_CHARGER_PROFILE_OVERRIDE
		/* custom profile params */
		if (in->set_param.param >= CS_PARAM_CUSTOM_PROFILE_MIN &&
		    in->set_param.param <= CS_PARAM_CUSTOM_PROFILE_MAX) {
			rv  = charger_profile_override_set_param(
				in->set_param.param, val);
		} else
#endif
			switch (in->set_param.param) {
			case CS_PARAM_CHG_VOLTAGE:
				val = charger_closest_voltage(val);
				if (charge_request(val, -1))
					rv = EC_RES_ERROR;
				manual_mode = 1;
				break;
			case CS_PARAM_CHG_CURRENT:
				val = charger_closest_current(val);
				if (charge_request(-1, val))
					rv = EC_RES_ERROR;
				manual_mode = 1;
				break;
			case CS_PARAM_CHG_INPUT_CURRENT:
				if (charger_set_input_current(val))
					rv = EC_RES_ERROR;
				break;
			case CS_PARAM_CHG_STATUS:
				/* Can't set this */
				rv = EC_RES_ACCESS_DENIED;
				break;
			case CS_PARAM_CHG_OPTION:
				if (charger_set_option(val))
					rv = EC_RES_ERROR;
				break;
			default:
				rv = EC_RES_INVALID_PARAM;

			}
		break;

	default:
		CPRINTS("EC_CMD_CHARGE_STATE: bad cmd 0x%x", in->cmd);
		rv = EC_RES_INVALID_PARAM;
	}

	return rv;
}

DECLARE_HOST_COMMAND(EC_CMD_CHARGE_STATE, charge_command_charge_state,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console commands */

static int command_chgstate(int argc, char **argv)
{
	int rv;
	int val;

	if (argc > 1) {
		if (!strcasecmp(argv[1], "idle")) {
			if (argc <= 2)
				return EC_ERROR_PARAM_COUNT;
			if (!parse_bool(argv[2], &val))
				return EC_ERROR_PARAM2;
			rv = charge_force_idle(val);
			if (rv)
				return rv;
		} else if (!strcasecmp(argv[1], "debug")) {
			if (argc <= 2)
				return EC_ERROR_PARAM_COUNT;
			if (!parse_bool(argv[2], &debugging))
				return EC_ERROR_PARAM2;
		} else {
			/* maybe handle board_discharge_on_ac() too? */
			return EC_ERROR_PARAM1;
		}
	}

	dump_charge_state();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(chgstate, command_chgstate,
			"[idle|debug on|off]",
			"Get/set charge state machine status",
			NULL);
