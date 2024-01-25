/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio.h>

#include "adc.h"
#include "battery.h"
#include "battery_smart.h"
#include "battery_fuel_gauge.h"
#include "board_adc.h"
#include "board_host_command.h"
#include "charger.h"
#include "charge_state.h"
#include "console.h"
#include "customized_shared_memory.h"
#include "hooks.h"
#include "system.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)

#define CACHE_INVALIDATION_TIME_US (3 * SECOND)

static enum battery_present batt_pres_prev = BP_NOT_SURE;
static uint8_t charging_maximum_level = EC_CHARGE_LIMIT_RESTORE;
static int old_btp;

enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres;
	int mv;

	mv = adc_read_channel(ADC_VCIN1_BATT_TEMP);
	batt_pres = (mv < 2200 ? BP_YES : BP_NO);

	if (mv == ADC_READ_ERROR)
		return BP_NO;

	/*
	 * If the battery is present now and was present last time we checked,
	 * return early.
	 */
	if (batt_pres == BP_YES && batt_pres_prev == batt_pres)
		return batt_pres;

	batt_pres_prev = batt_pres;

	return batt_pres;
}

uint32_t get_system_percentage(void)
{
	static uint32_t pre_os_percentage;
	uint32_t memmap_cap = *(uint32_t *)host_get_memmap(EC_MEMMAP_BATT_CAP);
	uint32_t memmap_lfcc = *(uint32_t *)host_get_memmap(EC_MEMMAP_BATT_LFCC);
	uint32_t os_percentage = 1000 * memmap_cap / (memmap_lfcc + 1);

	/* ensure this value is valid */
	if (os_percentage <= 1000 && os_percentage >= 0) {
		pre_os_percentage = os_percentage;
		return os_percentage;
	} else
		return pre_os_percentage;
}

static void battery_percentage_control(void)
{
	enum ec_charge_control_mode new_mode;
	static int in_percentage_control;
	uint32_t batt_os_percentage = get_system_percentage();
	int rv;

	/**
	 * If the host command EC_CMD_CHARGE_CONTROL set control mode to CHARGE_CONTROL_DISCHARGE
	 * or CHARGE_CONTROL_IDLE, ignore the battery_percentage_control();
	 */
	if (!in_percentage_control && get_chg_ctrl_mode() != CHARGE_CONTROL_NORMAL)
		return;

	if (charging_maximum_level == EC_CHARGE_LIMIT_RESTORE)
		system_get_bbram(SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX, &charging_maximum_level);

	if (charging_maximum_level & CHG_LIMIT_OVERRIDE) {
		new_mode = CHARGE_CONTROL_NORMAL;
		if (batt_os_percentage == 1000)
			charging_maximum_level = charging_maximum_level | 0x64;
	} else if (charging_maximum_level < 20)
		new_mode = CHARGE_CONTROL_NORMAL;
	else if (batt_os_percentage > charging_maximum_level * 10) {
		new_mode = CHARGE_CONTROL_DISCHARGE;
		in_percentage_control = 1;
	} else if (batt_os_percentage == charging_maximum_level * 10) {
		new_mode = CHARGE_CONTROL_IDLE;
		in_percentage_control = 1;
	} else {
		new_mode = CHARGE_CONTROL_NORMAL;
		in_percentage_control = 0;
	}

	set_chg_ctrl_mode(new_mode);
#ifdef CONFIG_PLATFORM_EC_CHARGER_DISCHARGE_ON_AC
	rv = charger_discharge_on_ac(new_mode == CHARGE_CONTROL_DISCHARGE);
#endif
	if (rv != EC_SUCCESS)
		CPRINTS("Failed to discharge.");
}
DECLARE_HOOK(HOOK_AC_CHANGE, battery_percentage_control, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, battery_percentage_control, HOOK_PRIO_DEFAULT);

enum battery_present board_batt_is_present(void)
{
	/*
	 * Due to adc_read_channel() will clear the task event,
	 * we should get the battery status without read adc channel again.
	 */
	return batt_pres_prev;
}

void battery_customize(struct charge_state_data *curr_batt)
{
	char text[32];
	char *str = "LION";
	int value;
	int new_btp;
	int rv;
	static int batt_state;
	static int read_manuf_date;
	int day = 0;
	int month = 0;
	int year = 0;
	uint32_t batt_os_percentage = get_system_percentage();

	/* manufacture date is static data */
	if (!read_manuf_date && battery_is_present() == BP_YES) {
		rv = battery_manufacture_date(&year, &month, &day);
		if (rv == EC_SUCCESS) {
			ccprintf("Batt manufacturer date: %d.%d.%d\n", year, month, day);
			*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_MANUF_DAY) = day;
			*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_MANUF_MONTH) = month;
			*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_MANUF_YEAR) =
				year & 0xff;
			*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_MANUF_YEAR + 1) =
				year >> 8;
			read_manuf_date = 1;
		}
	} else if (!battery_is_present()) {
		/**
		 * if battery isn't present, we need to read manufacture
		 * date after battery is connect
		 */
		read_manuf_date = 0;
	}

	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_AVER_TEMP) =
					(curr_batt->batt.temperature - 2731) / 10;
	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_PERCENTAGE) =
					batt_os_percentage / 10;

	if (curr_batt->batt.status & STATUS_FULLY_CHARGED)
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_STATUS) |= EC_BATT_FLAG_FULL;
	else
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_STATUS) &= ~EC_BATT_FLAG_FULL;

	battery_device_chemistry(text, sizeof(text));
	if (!strncmp(text, str, 4))
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_STATUS) |= EC_BATT_TYPE;
	else
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_STATUS) &= ~EC_BATT_TYPE;

	battery_get_mode(&value);
	/* in framework use smart.c it will force in mAh mode*/
	if (value & MODE_CAPACITY)
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_STATUS) &= ~EC_BATT_MODE;
	else
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_STATUS) |= EC_BATT_MODE;

	/* BTP: Notify AP update battery */
	new_btp = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_TRIP_POINT) +
			(*host_get_memmap(EC_CUSTOMIZED_MEMMAP_BATT_TRIP_POINT+1) << 8);

	if (!(curr_batt->batt.flags & BATT_FLAG_BAD_REMAINING_CAPACITY)) {

		if (old_btp == 0)
			old_btp = curr_batt->batt.remaining_capacity;

		if (new_btp == 0 && batt_os_percentage < 995)
			host_set_single_event(EC_HOST_EVENT_BATT_BTP);

		if (new_btp > old_btp && !battery_is_cut_off()) {
			if (curr_batt->batt.remaining_capacity > new_btp) {
				old_btp = new_btp;
				host_set_single_event(EC_HOST_EVENT_BATT_BTP);
			}
		} else if (new_btp < old_btp && !battery_is_cut_off()) {
			if (curr_batt->batt.remaining_capacity < new_btp) {
				old_btp = new_btp;
				host_set_single_event(EC_HOST_EVENT_BATT_BTP);
			}
		}
	}

	/*
	 * When the battery present have change notify AP
	 */
	if (batt_state != curr_batt->batt.is_present) {
		host_set_single_event(EC_HOST_EVENT_BATTERY);
		batt_state = curr_batt->batt.is_present;
	}
}

static void fix_single_param(int flag, int *cached, int *curr)
{
	if (flag)
		*curr = *cached;
	else
		*cached = *curr;
}

/*
 * If any value in batt_params is bad, replace it with a cached
 * good value, to make sure we never send random numbers to ap
 * side.
 */
__override void board_battery_compensate_params(struct batt_params *batt)
{
	static struct batt_params batt_cache = { 0 };
	static timestamp_t deadline;

	/*
	 * If battery keeps failing for 3 seconds, stop hiding the error and
	 * report back to host.
	 */

	if (batt->flags & BATT_FLAG_RESPONSIVE) {
		if (batt->flags & BATT_FLAG_BAD_ANY) {
			if (timestamp_expired(deadline, NULL))
				return;
		} else
			deadline.val = get_time().val + CACHE_INVALIDATION_TIME_US;
	} else if (!(batt->flags & BATT_FLAG_RESPONSIVE)) {
		/**
		 * There are 4 situations for battery is not repsonsed
		 * 1. Darin battery (first time)
		 * 2. Dead battery (first time)
		 * 3. No battery (is preset)
		 * 4. Others
		 */
		/* we don't need to cache the value when battery is not present */
		if (!batt->is_present) {
			batt_cache.flags &= ~BATT_FLAG_RESPONSIVE;
			return;
		}

		/* we don't need to cache the value when we read the battery first time */
		if (!(batt_cache.flags & BATT_FLAG_RESPONSIVE))
			return;

		/**
		 * If battery keeps no responsing for 3 seconds, stop hiding the error and
		 * back to host.
		 */
		if (timestamp_expired(deadline, NULL)) {
			batt_cache.flags &= ~BATT_FLAG_RESPONSIVE;
			return;
		}
	}

	/* return cached values for at most CACHE_INVALIDATION_TIME_US */
	fix_single_param(batt->flags & BATT_FLAG_BAD_STATE_OF_CHARGE,
			&batt_cache.state_of_charge,
			&batt->state_of_charge);
	fix_single_param(batt->flags & BATT_FLAG_BAD_VOLTAGE,
			&batt_cache.voltage,
			&batt->voltage);
	fix_single_param(batt->flags & BATT_FLAG_BAD_CURRENT,
			&batt_cache.current,
			&batt->current);
	fix_single_param(batt->flags & BATT_FLAG_BAD_DESIRED_VOLTAGE,
			&batt_cache.desired_voltage,
			&batt->desired_voltage);
	fix_single_param(batt->flags & BATT_FLAG_BAD_DESIRED_CURRENT,
			&batt_cache.desired_current,
			&batt->desired_current);
	fix_single_param(batt->flags & BATT_FLAG_BAD_REMAINING_CAPACITY,
			&batt_cache.remaining_capacity,
			&batt->remaining_capacity);
	fix_single_param(batt->flags & BATT_FLAG_BAD_FULL_CAPACITY,
			&batt_cache.full_capacity,
			&batt->full_capacity);
	fix_single_param(batt->flags & BATT_FLAG_BAD_STATUS,
			&batt_cache.status,
			&batt->status);
	fix_single_param(batt->flags & BATT_FLAG_BAD_TEMPERATURE,
			&batt_cache.temperature,
			&batt->temperature);
	/*
	 * If battery_compensate_params() didn't calculate display_charge
	 * for us, also update it with last good value.
	 */
	fix_single_param(batt->display_charge == 0,
			&batt_cache.display_charge,
			&batt->display_charge);

	/* remove bad flags after applying cached values */
	batt->flags &= ~BATT_FLAG_BAD_ANY;
	batt->flags |= BATT_FLAG_RESPONSIVE;
	batt_cache.flags |= BATT_FLAG_RESPONSIVE;
}

/*****************************************************************************/
/* Host command */

static enum ec_status cmd_charging_limit_control(struct host_cmd_handler_args *args)
{

	const struct ec_params_ec_chg_limit_control *p = args->params;
	struct ec_response_chg_limit_control *r = args->response;

	if (p->modes & CHG_LIMIT_DISABLE) {
		charging_maximum_level = 0;
		system_set_bbram(SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX, 0);
	}

	if (p->modes & CHG_LIMIT_SET_LIMIT) {
		if (p->max_percentage < 20)
			return EC_RES_ERROR;

		charging_maximum_level = p->max_percentage;
		system_set_bbram(SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX, charging_maximum_level);
	}

	if (p->modes & CHG_LIMIT_OVERRIDE)
		charging_maximum_level = charging_maximum_level | CHG_LIMIT_OVERRIDE;

	if (p->modes & CHG_LIMIT_GET_LIMIT) {
		system_get_bbram(SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX, &charging_maximum_level);
		r->max_percentage = charging_maximum_level;
		args->response_size = sizeof(*r);
	}

	battery_percentage_control();

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHARGE_LIMIT_CONTROL, cmd_charging_limit_control,
			EC_VER_MASK(0));
