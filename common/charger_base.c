/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery charging task and state machine.
 */

#include "charge_state.h"
#include "charger.h"
#include "charger_base.h"
#include "common.h"
#include "console.h"
#include "ec_ec_comm_client.h"
#include "hooks.h"
#include "string.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)

/* Base has responded to one of our commands already. */
static int base_responsive;
static int charge_base;
static int prev_charge_base;
static int prev_current_base;
static int prev_allow_charge_base;
static int prev_current_lid;

/*
 * In debugging mode, with AC, input current to allocate to base. Negative
 * value disables manual mode.
 */
static int manual_ac_current_base = -1;
/*
 * In debugging mode, when discharging, current to transfer from lid to base
 * (negative to transfer from base to lid). Only valid when enabled is true.
 */
static int manual_noac_enabled;
static int manual_noac_current_base;

/*
 * Parameters for dual-battery policy.
 * TODO(b:71881017): This should be made configurable by AP in the future.
 */
struct dual_battery_policy {
	/*** Policies when AC is not connected. ***/
	/* Voltage to use when using OTG mode between lid and base (mV) */
	uint16_t otg_voltage;
	/* Maximum current to apply from base to lid (mA) */
	uint16_t max_base_to_lid_current;
	/*
	 * Margin to apply between provided OTG output current and input current
	 * limit, to make sure that input charger does not overcurrent output
	 * charger. input_current = (1-margin) * output_current. (/128)
	 */
	uint8_t margin_otg_current;

	/* Only do base to lid OTG when base battery above this value (%) */
	uint8_t min_charge_base_otg;

	/*
	 * When base/lid battery percentage is below this value, do
	 * battery-to-battery charging. (%)
	 */
	uint8_t max_charge_base_batt_to_batt;
	uint8_t max_charge_lid_batt_to_batt;

	/*** Policies when AC is connected. ***/
	/* Minimum power to allocate to base (mW), includes some margin to allow
	 * base to charge when critically low.
	 */
	uint16_t min_base_system_power;

	/* Smoothing factor for lid power (/128) */
	uint8_t lid_system_power_smooth;
	/*
	 * Smoothing factor for base/lid battery power, when the battery power
	 * is decreasing only: we try to estimate the maximum power that the
	 * battery is willing to take and always reset it when it draws more
	 * than the estimate. (/128)
	 */
	uint8_t battery_power_smooth;

	/*
	 * Margin to add to requested base/lid battery power, to figure out how
	 * much current to allocate. allocation = (1+margin) * request. (/128)
	 */
	uint8_t margin_base_battery_power;
	uint8_t margin_lid_battery_power;

	/* Maximum current to apply from lid to base (mA) */
	uint16_t max_lid_to_base_current;
};

static const struct dual_battery_policy db_policy = {
	.otg_voltage = 12000, /* mV */
	.max_base_to_lid_current = 1800, /* mA, about 2000mA with margin. */
	.margin_otg_current = 13, /* /128 = 10.1% */
	.min_charge_base_otg = 5, /* % */
	.max_charge_base_batt_to_batt = 4, /* % */
	.max_charge_lid_batt_to_batt = 10, /* % */
	.min_base_system_power = 1300, /* mW */
	.lid_system_power_smooth = 32, /* 32/128 = 0.25 */
	.battery_power_smooth = 1, /* 1/128 = 0.008 */
	.margin_base_battery_power = 32, /* 32/128 = 0.25 */
	.margin_lid_battery_power = 32, /* 32/128 = 0.25 */
	.max_lid_to_base_current = 2000, /* mA */
};

/* Add at most "value" to power_var, subtracting from total_power budget. */
#define CHG_ALLOCATE(power_var, total_power, value)       \
	do {                                              \
		int val_capped = MIN(value, total_power); \
		(power_var) += val_capped;                \
		(total_power) -= val_capped;              \
	} while (0)

/* Check if a base is connected */
bool base_connected(void)
{
	return board_is_base_connected();
}

/**
 * Setup current settings for base, and record previous values, if the base
 * is responsive.
 *
 * @param current_base Current to be drawn by base (negative to provide power)
 * @param allow_charge_base Whether base battery should be charged (only makes
 *                          sense with positive current)
 */
static int set_base_current(int current_base, int allow_charge_base)
{
	/* "OTG" voltage from base to lid. */
	const int otg_voltage = db_policy.otg_voltage;
	int ret;

	ret = ec_ec_client_base_charge_control(current_base, otg_voltage,
					       allow_charge_base);
	if (ret) {
		/* Ignore errors until the base is responsive. */
		if (base_responsive)
			return ret;
	} else {
		base_responsive = 1;
		prev_current_base = current_base;
		prev_allow_charge_base = allow_charge_base;
	}

	return EC_RES_SUCCESS;
}

/**
 * Setup current settings for lid and base, in a safe way.
 *
 * @param current_base Current to be drawn by base (negative to provide power)
 * @param allow_charge_base Whether base battery should be charged (only makes
 *                          sense with positive current)
 * @param current_lid Current to be drawn by lid (negative to provide power)
 * @param allow_charge_lid Whether lid battery should be charged
 * @param is_full Whether the lid battery is full
 */
static void set_base_lid_current(int current_base, int allow_charge_base,
				 int current_lid, int allow_charge_lid,
				 bool is_full)
{
	/* "OTG" voltage from lid to base. */
	const int otg_voltage = db_policy.otg_voltage;

	int lid_first;
	int ret;
	int chgnum = 0;

	/* TODO(b:71881017): This is still quite verbose during charging. */
	if (prev_current_base != current_base ||
	    prev_allow_charge_base != allow_charge_base ||
	    prev_current_lid != current_lid) {
		CPRINTS("Base/Lid: %d%s/%d%s mA", current_base,
			allow_charge_base ? "+" : "", current_lid,
			allow_charge_lid ? "+" : "");
	}

	/*
	 * To decide whether to first control the lid or the base, we first
	 * control the side that _reduces_ current that would be drawn, then
	 * setup one that would start providing power, then increase current.
	 */
	if (current_lid >= 0 && current_lid < prev_current_lid)
		lid_first = 1; /* Lid decreases current */
	else if (current_base >= 0 && current_base < prev_current_base)
		lid_first = 0; /* Base decreases current */
	else if (current_lid < 0)
		lid_first = 1; /* Lid provide power */
	else
		lid_first = 0; /* All other cases: control the base first */

	if (!lid_first && base_connected()) {
		ret = set_base_current(current_base, allow_charge_base);
		if (ret)
			return;
	}

	if (current_lid >= 0) {
		ret = charge_set_output_current_limit(CHARGER_SOLO, 0, 0);
		if (ret)
			return;
		ret = charger_set_input_current_limit(chgnum, current_lid);
		if (ret)
			return;
		ret = charge_request(allow_charge_lid, is_full);
	} else {
		ret = charge_set_output_current_limit(
			CHARGER_SOLO, -current_lid, otg_voltage);
	}

	if (ret)
		return;

	prev_current_lid = current_lid;

	if (lid_first && base_connected()) {
		ret = set_base_current(current_base, allow_charge_base);
		if (ret)
			return;
	}

	/*
	 * Make sure cross-power is enabled (it might not be enabled right after
	 * plugging the base, or when an adapter just got connected).
	 */
	if (base_connected() && current_base != 0)
		board_enable_base_power(1);
}

/**
 * Smooth power value, covering some edge cases.
 * Compute s*curr+(1-s)*prev, where s is in 1/128 unit.
 */
static int smooth_value(int prev, int curr, int s)
{
	if (curr < 0)
		curr = 0;
	if (prev < 0)
		return curr;

	return prev + s * (curr - prev) / 128;
}

/**
 * Add margin m to value. Compute (1+m)*value, where m is in 1/128 unit.
 */
static int add_margin(int value, int m)
{
	return value + m * value / 128;
}

void base_charge_allocate_input_current_limit(
	const struct charge_state_data *curr, bool is_full, bool debugging)
{
	/*
	 * All the power numbers are in mW.
	 *
	 * Since we work with current and voltage in mA and mV, multiplying them
	 * gives numbers in uW, which are dangerously close to overflowing when
	 * doing intermediate computations (60W * 100 overflows a 32-bit int,
	 * for example). We therefore divide the product by 1000 and re-multiply
	 * the power numbers by 1000 when converting them back to current.
	 */
	int total_power = 0;

	static int prev_base_battery_power = -1;
	int base_battery_power = 0;
	int base_battery_power_max = 0;

	static int prev_lid_system_power = -1;
	int lid_system_power;

	static int prev_lid_battery_power = -1;
	int lid_battery_power = 0;
	int lid_battery_power_max = 0;

	int power_base = 0;
	int power_lid = 0;

	int current_base = 0;
	int current_lid = 0;

	int charge_lid = charge_get_percent();

	const struct ec_response_battery_dynamic_info *const base_bd =
		&battery_dynamic[BATT_IDX_BASE];
	const struct batt_params *batt = &curr->batt;

	if (!base_connected()) {
		set_base_lid_current(0, 0, curr->desired_input_current, 1,
				     is_full);
		prev_base_battery_power = -1;
		return;
	}

	/* Charging */
	if (curr->desired_input_current > 0 && curr->input_voltage > 0)
		total_power = curr->desired_input_current *
			      curr->input_voltage / 1000;

	/*
	 * TODO(b:71723024): We should be able to replace this test by curr.ac,
	 * but the value is currently wrong, especially during transitions.
	 */
	if (total_power <= 0) {
		int base_critical =
			charge_base >= 0 &&
			charge_base < db_policy.max_charge_base_batt_to_batt;

		/* Discharging */
		prev_base_battery_power = -1;
		prev_lid_system_power = -1;
		prev_lid_battery_power = -1;

		/* Manual control */
		if (manual_noac_enabled) {
			int lid_current, base_current;

			if (manual_noac_current_base > 0) {
				base_current = -manual_noac_current_base;
				lid_current = add_margin(
					manual_noac_current_base,
					db_policy.margin_otg_current);
			} else {
				lid_current = manual_noac_current_base;
				base_current = add_margin(
					-manual_noac_current_base,
					db_policy.margin_otg_current);
			}

			set_base_lid_current(base_current, 0, lid_current, 0,
					     is_full);
			return;
		}

		/*
		 * System is off, cut power to the base. We'll reset the base
		 * when system restarts, or when AC is plugged.
		 */
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			set_base_lid_current(0, 0, 0, 0, is_full);
			if (base_responsive) {
				/* Base still responsive, put it to sleep. */
				CPRINTF("Hibernating base\n");
				ec_ec_client_hibernate();
				base_responsive = 0;
				board_enable_base_power(0);
			}
			return;
		}

		/*
		 * System is suspended, let the lid and base run on their
		 * own power. However, if the base battery is critically low, we
		 * still want to provide power to the base, to make sure it
		 * stays alive to be able to wake the system on keyboard or
		 * touchpad events.
		 */
		if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
		    !base_critical) {
			set_base_lid_current(0, 0, 0, 0, is_full);
			return;
		}

		if (charge_base > db_policy.min_charge_base_otg) {
			int lid_current = db_policy.max_base_to_lid_current;
			int base_current = add_margin(
				lid_current, db_policy.margin_otg_current);
			/* Draw current from base to lid */
			set_base_lid_current(
				-base_current, 0, lid_current,
				charge_lid <
					db_policy.max_charge_lid_batt_to_batt,
				is_full);
		} else {
			/*
			 * Base battery is too low, apply power to it, and allow
			 * it to charge if it is critically low.
			 *
			 * TODO(b:71881017): When suspended, this will make the
			 * battery charge oscillate between 3 and 4 percent,
			 * which might not be great for battery life. We need
			 * some hysteresis.
			 */
			/*
			 * TODO(b:71881017): Precompute (ideally, at build time)
			 * the base_current, so we do not need to do a division
			 * here.
			 */
			int base_current =
				(db_policy.min_base_system_power * 1000) /
				db_policy.otg_voltage;
			int lid_current = add_margin(
				base_current, db_policy.margin_otg_current);

			set_base_lid_current(base_current, base_critical,
					     -lid_current, 0, is_full);
		}

		return;
	}

	/* Manual control */
	if (manual_ac_current_base >= 0) {
		int current_base = manual_ac_current_base;
		int current_lid =
			curr->desired_input_current - manual_ac_current_base;

		if (current_lid < 0) {
			current_base = curr->desired_input_current;
			current_lid = 0;
		}

		set_base_lid_current(current_base, 1, current_lid, 1, is_full);
		return;
	}

	/* Estimate system power. */
	lid_system_power = charger_get_system_power() / 1000;

	/* Smooth system power, as it is very spiky */
	lid_system_power = smooth_value(prev_lid_system_power, lid_system_power,
					db_policy.lid_system_power_smooth);
	prev_lid_system_power = lid_system_power;

	/*
	 * TODO(b:71881017): Smoothing the battery power isn't necessarily a
	 * good idea: if the system takes up too much power, we may reduce the
	 * estimate power too quickly, leading to oscillations when the system
	 * power goes down. Instead, we should probably estimate the current
	 * based on remaining capacity.
	 */
	/* Estimate lid battery power. */
	if (!(batt->flags & (BATT_FLAG_BAD_VOLTAGE | BATT_FLAG_BAD_CURRENT)))
		lid_battery_power = batt->current * batt->voltage / 1000;
	if (lid_battery_power < prev_lid_battery_power)
		lid_battery_power =
			smooth_value(prev_lid_battery_power, lid_battery_power,
				     db_policy.battery_power_smooth);
	if (!(batt->flags &
	      (BATT_FLAG_BAD_DESIRED_VOLTAGE | BATT_FLAG_BAD_DESIRED_CURRENT)))
		lid_battery_power_max =
			batt->desired_current * batt->desired_voltage / 1000;

	lid_battery_power = MIN(lid_battery_power, lid_battery_power_max);

	/* Estimate base battery power. */
	if (!(base_bd->flags & EC_BATT_FLAG_INVALID_DATA)) {
		base_battery_power = base_bd->actual_current *
				     base_bd->actual_voltage / 1000;
		base_battery_power_max = base_bd->desired_current *
					 base_bd->desired_voltage / 1000;
	}
	if (base_battery_power < prev_base_battery_power)
		base_battery_power = smooth_value(
			prev_base_battery_power, base_battery_power,
			db_policy.battery_power_smooth);
	base_battery_power = MIN(base_battery_power, base_battery_power_max);

	if (debugging) {
		CPRINTF("%s:\n", __func__);
		CPRINTF("total power: %d\n", total_power);
		CPRINTF("base battery power: %d (%d)\n", base_battery_power,
			base_battery_power_max);
		CPRINTF("lid system power: %d\n", lid_system_power);
		CPRINTF("lid battery power: %d\n", lid_battery_power);
		CPRINTF("percent base/lid: %d%% %d%%\n", charge_base,
			charge_lid);
	}

	prev_lid_battery_power = lid_battery_power;
	prev_base_battery_power = base_battery_power;

	if (total_power > 0) { /* Charging */
		/* Allocate system power */
		CHG_ALLOCATE(power_base, total_power,
			     db_policy.min_base_system_power);
		CHG_ALLOCATE(power_lid, total_power, lid_system_power);

		/* Allocate lid, then base battery power */
		lid_battery_power = add_margin(
			lid_battery_power, db_policy.margin_lid_battery_power);
		CHG_ALLOCATE(power_lid, total_power, lid_battery_power);

		base_battery_power =
			add_margin(base_battery_power,
				   db_policy.margin_base_battery_power);
		CHG_ALLOCATE(power_base, total_power, base_battery_power);

		/* Give everything else to the lid. */
		CHG_ALLOCATE(power_lid, total_power, total_power);
		if (debugging)
			CPRINTF("power: base %d mW / lid %d mW\n", power_base,
				power_lid);

		current_base = 1000 * power_base / curr->input_voltage;
		current_lid = 1000 * power_lid / curr->input_voltage;

		if (current_base > db_policy.max_lid_to_base_current) {
			current_lid += (current_base -
					db_policy.max_lid_to_base_current);
			current_base = db_policy.max_lid_to_base_current;
		}

		if (debugging)
			CPRINTF("current: base %d mA / lid %d mA\n",
				current_base, current_lid);

		set_base_lid_current(current_base, 1, current_lid, 1, is_full);
	} else { /* Discharging */
	}

	if (debugging)
		CPRINTF("====\n");
}

void base_update_battery_info(void)
{
	struct ec_response_battery_dynamic_info *const bd =
		&battery_dynamic[BATT_IDX_BASE];

	if (!base_connected()) {
		const int invalid_flags = EC_BATT_FLAG_INVALID_DATA;
		/* Invalidate static/dynamic information */
		if (bd->flags != invalid_flags) {
			bd->flags = invalid_flags;

			host_set_single_event(EC_HOST_EVENT_BATTERY);
			host_set_single_event(EC_HOST_EVENT_BATTERY_STATUS);
		}
		charge_base = -1;
		base_responsive = 0;
		prev_current_base = 0;
		prev_allow_charge_base = 0;
	} else if (base_responsive) {
		int old_flags = bd->flags;
		int flags_changed;
		int old_full_capacity = bd->full_capacity;

		ec_ec_client_base_get_dynamic_info();
		flags_changed = (old_flags != bd->flags);
		/* Fetch static information when flags change. */
		if (flags_changed)
			ec_ec_client_base_get_static_info();

		battery_memmap_refresh(BATT_IDX_BASE);

		/* Newly connected battery, or change in capacity. */
		if (old_flags & EC_BATT_FLAG_INVALID_DATA ||
		    ((old_flags & EC_BATT_FLAG_BATT_PRESENT) !=
		     (bd->flags & EC_BATT_FLAG_BATT_PRESENT)) ||
		    old_full_capacity != bd->full_capacity)
			host_set_single_event(EC_HOST_EVENT_BATTERY);

		if (flags_changed)
			host_set_single_event(EC_HOST_EVENT_BATTERY_STATUS);

		/* Update charge_base */
		if (bd->flags & (BATT_FLAG_BAD_FULL_CAPACITY |
				 BATT_FLAG_BAD_REMAINING_CAPACITY))
			charge_base = -1;
		else if (bd->full_capacity > 0)
			charge_base = 100 * bd->remaining_capacity /
				      bd->full_capacity;
		else
			charge_base = 0;
	}
}

bool base_check_extpower(int ac, int prev_ac)
{
	bool zero_ac = false;

	/*
	 * When base is powering the system, make sure ac stays 0.
	 * TODO(b:71723024): Fix extpower_is_present() in hardware instead.
	 */
	if (base_responsive && prev_current_base < 0) {
		ac = 0;
		zero_ac = true;
	}

	/* System is off: if AC gets connected, reset the base. */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) && !prev_ac && ac)
		board_base_reset();

	return zero_ac;
}

static int command_chgdualdebug(int argc, const char **argv)
{
	int val;
	char *e;

	if (argc > 1) {
		if (argv[1][0] == 'c') {
			if (argc <= 2)
				return EC_ERROR_PARAM_COUNT;

			if (!strcasecmp(argv[2], "auto")) {
				val = -1;
			} else {
				val = strtoi(argv[2], &e, 0);
				if (*e || val < 0)
					return EC_ERROR_PARAM2;
			}

			manual_ac_current_base = val;
			charge_wakeup();
		} else if (argv[1][0] == 'd') {
			if (argc <= 2)
				return EC_ERROR_PARAM_COUNT;

			if (!strcasecmp(argv[2], "auto")) {
				manual_noac_enabled = 0;
			} else {
				val = strtoi(argv[2], &e, 0);
				if (*e)
					return EC_ERROR_PARAM2;
				manual_noac_current_base = val;
				manual_noac_enabled = 1;
			}
			charge_wakeup();
		} else {
			return EC_ERROR_PARAM1;
		}
	} else {
		ccprintf("Base/Lid: %d%s/%d mA\n", prev_current_base,
			 prev_allow_charge_base ? "+" : "", prev_current_lid);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(chgdualdebug, command_chgdualdebug,
			"[charge (auto|<current>)|discharge (auto|<current>)]",
			"Manually control dual-battery charging algorithm.");

void charger_base_setup(void)
{
	base_responsive = 0;
	charge_base = -1;
}

bool charger_base_charge_changed(void)
{
	return charge_base != prev_charge_base;
}

void charger_base_charge_update(void)
{
	prev_charge_base = charge_base;
}

void charger_base_show_charge(void)
{
	CPRINTS("Base battery %d%%", charge_base);
}

bool charger_base_charge_near_full(void)
{
	if (charge_base > -1 && charge_base < BATTERY_LEVEL_NEAR_FULL)
		return false;

	return true;
}

/* Reset the base on S5->S0 transition. */
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_base_reset, HOOK_PRIO_DEFAULT);
