/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "adc.h"
#include "adc_chip.h"
#include "board.h"
#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "host_command_customization.h"
#include "system.h"
#include "i2c.h"
#include "string.h"
#include "system.h"
#include "util.h"
#include "hooks.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define PARAM_CUT_OFF_LOW  0x10
#define PARAM_CUT_OFF_HIGH 0x00

/* Battery info for BQ40Z50 4-cell */
static const struct battery_info info = {
	.voltage_max = 17600,        /* mV */
	.voltage_normal = 15400,
	.voltage_min = 12000,
	.precharge_current = 72,   /* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 47,
	.charging_min_c = 0,
	.charging_max_c = 52,
	.discharging_min_c = 0,
	.discharging_max_c = 62,
};

static enum battery_present batt_pres_prev = BP_NOT_SURE;
static uint8_t charging_maximum_level = NEED_RESTORE;
static int old_btp;

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int board_cut_off_battery(void)
{
	int rv;
	uint8_t buf[3];

	/* Ship mode command must be sent twice to take effect */
	buf[0] = SB_MANUFACTURER_ACCESS & 0xff;
	buf[1] = PARAM_CUT_OFF_LOW;
	buf[2] = PARAM_CUT_OFF_HIGH;

	i2c_lock(I2C_PORT_BATTERY, 1);
	rv = i2c_xfer_unlocked(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS,
			       buf, 3, NULL, 0, I2C_XFER_SINGLE);
	rv |= i2c_xfer_unlocked(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS,
				buf, 3, NULL, 0, I2C_XFER_SINGLE);
	i2c_lock(I2C_PORT_BATTERY, 0);

	return rv;
}

__override void battery_charger_notify(uint8_t flag)
{
	static uint8_t batt_charger, new_state;
	
	batt_charger = flag & EC_BATT_FLAG_CHARGING;

	if (new_state != batt_charger) {
		new_state = batt_charger;
		host_set_single_event(EC_HOST_EVENT_BATTERY);
	}
}

static int battery_check_disconnect(void)
{
	int rv;
	uint8_t data[6];

	/* Check if battery charging + discharging is disabled. */
	rv = sb_read_mfgacc(PARAM_OPERATION_STATUS,
			    SB_ALT_MANUFACTURER_ACCESS, data, sizeof(data));
	if (rv)
		return BATTERY_DISCONNECT_ERROR;

	if (data[3] & BATTERY_DISCHARGING_DISABLED)
		return BATTERY_DISCONNECTED;


	return BATTERY_NOT_DISCONNECTED;
}

enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres;
	int mv;

	mv = adc_read_channel(ADC_VCIN1_BATT_TEMP);
	batt_pres = (mv < 3000 ? BP_YES : BP_NO);

	if (mv == ADC_READ_ERROR)
		return BP_NO;

	/*
	 * If the battery is present now and was present last time we checked,
	 * return early.
	 */
	if (batt_pres == BP_YES && batt_pres_prev == batt_pres)
		return batt_pres;


	if (!batt_pres)
		return BP_NO;
	else if (battery_check_disconnect() != BATTERY_NOT_DISCONNECTED)
		return BP_NOT_SURE;

	batt_pres_prev = batt_pres;

	return batt_pres;
}

#ifdef CONFIG_EMI_REGION1

void battery_customize(struct charge_state_data *emi_info)
{
	char text[32];
	char *str = "LION";
	int value;
	int new_btp;
	int rv;
	static int batt_state, prev_charge;
	static int read_manuf_date;
	int day = 0;
	int month = 0;
	int year = 0;

	/* manufacture date is static data */
	if (!read_manuf_date && battery_is_present() == BP_YES) {
		rv = battery_manufacture_date(&year, &month, &day);
		if (rv == EC_SUCCESS) {
			ccprintf("Batt manufacturer date: %d.%d.%d\n", year, month, day);
			*host_get_customer_memmap(EC_MEMMAP_ER1_BATT_MANUF_DAY) = day;
			*host_get_customer_memmap(EC_MEMMAP_ER1_BATT_MANUF_MONTH) = month;
			*host_get_customer_memmap(EC_MEMMAP_ER1_BATT_MANUF_YEAR) = year & 0xff;
			*host_get_customer_memmap(EC_MEMMAP_ER1_BATT_MANUF_YEAR+1) = year >> 8;
			read_manuf_date = 1;
		}
	} else if (!battery_is_present()) {
		/**
		 * if battery isn't present, we need to read manufacture
		 * date after battery is connect
		 */
		read_manuf_date = 0;
	}

	*host_get_customer_memmap(EC_MEMMAP_ER1_BATT_AVER_TEMP) = 
							(emi_info->batt.temperature - 2731)/10;
	*host_get_customer_memmap(EC_MEMMAP_ER1_BATT_PERCENTAGE) = emi_info->batt.display_charge/10;
	
	if (emi_info->batt.status & STATUS_FULLY_CHARGED)
		*host_get_customer_memmap(EC_MEMMAP_ER1_BATT_STATUS) |= EC_BATT_FLAG_FULL;
	else
		*host_get_customer_memmap(EC_MEMMAP_ER1_BATT_STATUS) &= ~EC_BATT_FLAG_FULL;

	battery_device_chemistry(text, sizeof(text));
	if (!strncmp(text, str, 4))
		*host_get_customer_memmap(EC_MEMMAP_ER1_BATT_STATUS) |= EC_BATT_TYPE;
	else
		*host_get_customer_memmap(EC_MEMMAP_ER1_BATT_STATUS) &= ~EC_BATT_TYPE;

	battery_get_mode(&value);
	/* in framework use smart.c it will force in mAh mode*/
	if (value & MODE_CAPACITY)
		*host_get_customer_memmap(EC_MEMMAP_ER1_BATT_STATUS) &= ~EC_BATT_MODE;
	else
		*host_get_customer_memmap(EC_MEMMAP_ER1_BATT_STATUS) |= EC_BATT_MODE;
	
	/* BTP: Notify AP update battery */
	new_btp = *host_get_customer_memmap(0x08) + (*host_get_customer_memmap(0x09) << 8);
	if (new_btp > old_btp && !battery_is_cut_off())
	{
		if (emi_info->batt.remaining_capacity > new_btp)
		{
			old_btp = new_btp;
			host_set_single_event(EC_HOST_EVENT_BATT_BTP);
			ccprintf ("trigger higher BTP: %d", old_btp);
		}
	} else if (new_btp < old_btp && !battery_is_cut_off())
	{
		if (emi_info->batt.remaining_capacity < new_btp)
		{
			old_btp = new_btp;
			host_set_single_event(EC_HOST_EVENT_BATT_BTP);
			ccprintf ("trigger lower BTP: %d", old_btp);
		}
	}

	/*
	 * Sometimes the battery will respond unusual remaining capacity
	 * it will make OS battery percentage stuck when EC get wrong new_btp
	 * so need to send BTP event, let BIOS update BTP
	 * when state of charge have change
	 */
	if (!(emi_info->batt.flags & BATT_FLAG_BAD_STATE_OF_CHARGE) &&
		emi_info->batt.state_of_charge != prev_charge) {
		prev_charge = emi_info->batt.state_of_charge;
		host_set_single_event(EC_HOST_EVENT_BATT_BTP);
	}

	/*
	 * When the battery present have change notify AP
	 */
	if (batt_state != emi_info->batt.is_present) {
		host_set_single_event(EC_HOST_EVENT_BATTERY);
		batt_state = emi_info->batt.is_present;
	}
}

#endif

static void battery_percentage_control(void)
{
	enum ec_charge_control_mode new_mode;
	int rv;


	if (charging_maximum_level == NEED_RESTORE)
		system_get_bbram(SYSTEM_BBRAM_IDX_CHG_MAX, &charging_maximum_level);

	if (charging_maximum_level & CHG_LIMIT_OVERRIDE) {
		new_mode = CHARGE_CONTROL_NORMAL;
		if (charge_get_percent() == 100)
			charging_maximum_level = charging_maximum_level | 0x64;
	} else if (charging_maximum_level < 20)
		new_mode = CHARGE_CONTROL_NORMAL;
	else if (charge_get_percent() > charging_maximum_level)
		new_mode = CHARGE_CONTROL_DISCHARGE;
	else if (charge_get_percent() == charging_maximum_level)
		new_mode = CHARGE_CONTROL_IDLE;
	else
		new_mode = CHARGE_CONTROL_NORMAL;

	ccprints("Charge Limit mode = %d", new_mode);

	set_chg_ctrl_mode(new_mode);
#ifdef CONFIG_CHARGER_DISCHARGE_ON_AC
	rv = charger_discharge_on_ac(new_mode == CHARGE_CONTROL_DISCHARGE);
#endif
	if (rv != EC_SUCCESS)
		ccprintf("fail to discharge.");
}
DECLARE_HOOK(HOOK_AC_CHANGE, battery_percentage_control, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, battery_percentage_control, HOOK_PRIO_DEFAULT);

static void fix_single_param(int flag, int *cached, int *curr)
{
	if (flag)
		*curr = *cached;
	else
		*cached = *curr;
}

#define CACHE_INVALIDATION_TIME_US (3 * SECOND)

/*
 * f any value in batt_params is bad, replace it with a cached
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

		/* we don't need to cache the value when we read the battery first time*/
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
/* Customize host command */

/*
 * Charging limit control.
 */
static enum ec_status cmd_charging_limit_control(struct host_cmd_handler_args *args)
{

	const struct ec_params_ec_chg_limit_control *p = args->params;
	struct ec_response_chg_limit_control *r = args->response;

	if (p->modes & CHG_LIMIT_DISABLE) {
		charging_maximum_level = 0;
		system_set_bbram(SYSTEM_BBRAM_IDX_CHG_MAX, 0);
	}

	if (p->modes & CHG_LIMIT_SET_LIMIT) {
		if( p->max_percentage < 20 )
			return EC_RES_ERROR;

		charging_maximum_level = p->max_percentage;
		system_set_bbram(SYSTEM_BBRAM_IDX_CHG_MAX, charging_maximum_level);
	}

	if (p->modes & CHG_LIMIT_OVERRIDE)
		charging_maximum_level = charging_maximum_level | CHG_LIMIT_OVERRIDE;

	if (p->modes & CHG_LIMIT_GET_LIMIT) {
		system_get_bbram(SYSTEM_BBRAM_IDX_CHG_MAX, &charging_maximum_level);
		r->max_percentage = charging_maximum_level;
		args->response_size = sizeof(*r);
	}

	battery_percentage_control();

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHARGE_LIMIT_CONTROL, cmd_charging_limit_control,
			EC_VER_MASK(0));
