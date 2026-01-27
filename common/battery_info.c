/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery V2 APIs.
 */

#include "battery.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "host_command.h"
#include "math_util.h"
#include "printf.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

/*
 * Store battery information in these 2 structures. Main (lid) battery is always
 * at index 0, and secondary (base) battery at index 1.
 */
struct battery_static_info battery_static[CONFIG_BATTERY_COUNT];
struct ec_response_battery_dynamic_info battery_dynamic[CONFIG_BATTERY_COUNT];

/*
 * Store the previous state of charge values to detect changes and trigger
 * the HOOK_BATTERY_SOC_CHANGE hook.
 */
static int prev_charge, prev_disp_charge;

#ifdef HAS_TASK_HOSTCMD
static void battery_update(enum battery_index i)
{
	char *batt_str;
	int *memmap_dcap = (int *)host_get_memmap(EC_MEMMAP_BATT_DCAP);
	int *memmap_dvlt = (int *)host_get_memmap(EC_MEMMAP_BATT_DVLT);
	int *memmap_ccnt = (int *)host_get_memmap(EC_MEMMAP_BATT_CCNT);
	int *memmap_volt = (int *)host_get_memmap(EC_MEMMAP_BATT_VOLT);
	int *memmap_rate = (int *)host_get_memmap(EC_MEMMAP_BATT_RATE);
	int *memmap_cap = (int *)host_get_memmap(EC_MEMMAP_BATT_CAP);
	int *memmap_lfcc = (int *)host_get_memmap(EC_MEMMAP_BATT_LFCC);
	uint8_t *memmap_flags = host_get_memmap(EC_MEMMAP_BATT_FLAG);

	/* Smart battery serial number is 16 bits */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_SERIAL);
	memcpy(batt_str, battery_static[i].serial_ext, EC_MEMMAP_TEXT_MAX);
	batt_str[EC_MEMMAP_TEXT_MAX - 1] = 0;

	/* Design Capacity of Full */
	*memmap_dcap = battery_static[i].design_capacity;

	/* Design Voltage */
	*memmap_dvlt = battery_static[i].design_voltage;

	/* Cycle Count */
	*memmap_ccnt = battery_static[i].cycle_count;

	/* Battery Manufacturer string */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_MFGR);
	memcpy(batt_str, battery_static[i].manufacturer_ext,
	       EC_MEMMAP_TEXT_MAX);
	batt_str[EC_MEMMAP_TEXT_MAX - 1] = 0;

	/* Battery Model string */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_MODEL);
	memcpy(batt_str, battery_static[i].model_ext, EC_MEMMAP_TEXT_MAX);
	batt_str[EC_MEMMAP_TEXT_MAX - 1] = 0;

	/* Battery Type string */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_TYPE);
	memcpy(batt_str, battery_static[i].type_ext, EC_MEMMAP_TEXT_MAX);
	batt_str[EC_MEMMAP_TEXT_MAX - 1] = 0;

	/*
	 * If any of the dynamic info is invalid, conservatively report all of
	 * the values as unknown because the flags aren't specific enough to
	 * know which value(s) are invalid.
	 *
	 * The charger keeps more detailed validity flags for each field, but
	 * that updates independently of battery_dynamic so we can't use those
	 * flags to present whatever data we do have because the charger's
	 * current flags may not match what's actually in battery_dynamic right
	 * now.
	 */
	if (battery_dynamic[i].flags & EC_BATT_FLAG_INVALID_DATA) {
		*memmap_volt = EC_MEMMAP_BATT_UNKNOWN_VALUE;
		*memmap_rate = EC_MEMMAP_BATT_UNKNOWN_VALUE;
		*memmap_cap = EC_MEMMAP_BATT_UNKNOWN_VALUE;
		*memmap_lfcc = EC_MEMMAP_BATT_UNKNOWN_VALUE;
	} else {
		*memmap_volt = battery_dynamic[i].actual_voltage;
		/*
		 * Rate must be absolute, flags will indicate whether
		 * the battery is charging or discharging.
		 */
		*memmap_rate = ABS(battery_dynamic[i].actual_current);
		*memmap_cap = battery_dynamic[i].remaining_capacity;
		*memmap_lfcc = battery_dynamic[i].full_capacity;
	}

	/* Flags are always valid, even if no other dynamic field is. */
	*memmap_flags = battery_dynamic[i].flags;
}

#ifdef CONFIG_HOSTCMD_BATTERY_INFO

static void populate_bsi_v2(struct ec_response_battery_static_info_v2 *r,
			    const struct battery_static_info *bs)
{
	r->design_capacity = bs->design_capacity;
	r->design_voltage = bs->design_voltage;
	r->cycle_count = bs->cycle_count;

	strzcpy(r->manufacturer, bs->manufacturer_ext, sizeof(r->manufacturer));
	strzcpy(r->device_name, bs->model_ext, sizeof(r->device_name));
	strzcpy(r->serial, bs->serial_ext, sizeof(r->serial));
	strzcpy(r->chemistry, bs->type_ext, sizeof(r->chemistry));
}

static enum ec_status
host_command_battery_get_static(struct host_cmd_handler_args *args)
{
	const struct ec_params_battery_static_info *p = args->params;
	const struct battery_static_info *bs;

	if (p->index >= CONFIG_BATTERY_COUNT)
		return EC_RES_INVALID_PARAM;
	bs = &battery_static[p->index];

	battery_update(p->index);
	if (args->version == 0) {
		struct ec_response_battery_static_info *r = args->response;

		r->design_capacity = bs->design_capacity;
		r->design_voltage = bs->design_voltage;
		r->cycle_count = bs->cycle_count;

		strzcpy(r->manufacturer, bs->manufacturer_ext,
			sizeof(r->manufacturer));
		strzcpy(r->model, bs->model_ext, sizeof(r->model));
		strzcpy(r->serial, bs->serial_ext, sizeof(r->serial));
		strzcpy(r->type, bs->type_ext, sizeof(r->type));

		args->response_size = sizeof(*r);
	} else if (args->version == 1) {
		struct ec_response_battery_static_info_v1 *r = args->response;

		r->design_capacity = bs->design_capacity;
		r->design_voltage = bs->design_voltage;
		r->cycle_count = bs->cycle_count;

		strzcpy(r->manufacturer_ext, bs->manufacturer_ext,
			sizeof(r->manufacturer_ext));
		strzcpy(r->model_ext, bs->model_ext, sizeof(r->model_ext));
		strzcpy(r->serial_ext, bs->serial_ext, sizeof(r->serial_ext));
		strzcpy(r->type_ext, bs->type_ext, sizeof(r->type_ext));

		args->response_size = sizeof(*r);
	} else if (args->version == 2) {
		struct ec_response_battery_static_info_v2 *r = args->response;

		populate_bsi_v2(r, bs);

		args->response_size = sizeof(*r);
	} else if (args->version == 3) {
		struct ec_response_battery_static_info_v3 *r = args->response;

		/* The v3 layout is simply v2 + an extra field */
		populate_bsi_v2((struct ec_response_battery_static_info_v2 *)r,
				bs);
#ifdef CONFIG_PLATFORM_EC_BATTERY_MANUF_INFO
		strzcpy(r->manuf_info, bs->manuf_info, sizeof(r->manuf_info));
#endif /* CONFIG_PLATFORM_EC_BATTERY_MANUF_INFO */

		args->response_size = sizeof(*r);
	} else {
		return EC_RES_INVALID_VERSION;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_GET_STATIC, host_command_battery_get_static,
		     EC_VER_MASK(0) | EC_VER_MASK(1) | EC_VER_MASK(2) |
			     EC_VER_MASK(3));

static enum ec_status
host_command_battery_get_dynamic(struct host_cmd_handler_args *args)
{
	const struct ec_params_battery_dynamic_info *p = args->params;
	struct ec_response_battery_dynamic_info *r = args->response;

	if (p->index >= CONFIG_BATTERY_COUNT)
		return EC_RES_INVALID_PARAM;

	args->response_size = sizeof(*r);
	memcpy(r, &battery_dynamic[p->index], sizeof(*r));

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_GET_DYNAMIC,
		     host_command_battery_get_dynamic, EC_VER_MASK(0));
#endif /* CONFIG_HOSTCMD_BATTERY_INFO */

void battery_memmap_refresh(enum battery_index index)
{
	if (*host_get_memmap(EC_MEMMAP_BATT_INDEX) == index)
		battery_update(index);
}

void battery_memmap_set_index(enum battery_index index)
{
	if (*host_get_memmap(EC_MEMMAP_BATT_INDEX) == index)
		return;

	*host_get_memmap(EC_MEMMAP_BATT_INDEX) = BATT_IDX_INVALID;
	if (index < 0 || index >= CONFIG_BATTERY_COUNT)
		return;

	battery_update(index);
	*host_get_memmap(EC_MEMMAP_BATT_INDEX) = index;
}

static void battery_init(void)
{
	*host_get_memmap(EC_MEMMAP_BATT_INDEX) = BATT_IDX_INVALID;
	*host_get_memmap(EC_MEMMAP_BATT_COUNT) = CONFIG_BATTERY_COUNT;
	*host_get_memmap(EC_MEMMAP_BATTERY_VERSION) = 2;

	battery_memmap_set_index(BATT_IDX_MAIN);
}
DECLARE_HOOK(HOOK_INIT, battery_init, HOOK_PRIO_DEFAULT);
#endif /* HAS_TASK_HOSTCMD */

static int is_battery_string_reliable(const char *buf)
{
	/*
	 * From is_string_printable rule, 0xFF is not printable.
	 * So, EC should think battery string is unreliable if string
	 * include 0xFF.
	 */
	while (*buf) {
		if ((*buf) == '\xff')
			return 0;
		buf++;
	}

	return 1;
}

int get_battery_threshold_percent(enum batt_threshold_type type)
{
	switch (type) {
	case BATT_THRESHOLD_TYPE_LOW:
		return BATTERY_LEVEL_LOW;
	case BATT_THRESHOLD_TYPE_SHUTDOWN:
		return CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE;
	default:
		return 0;
	}
}

bool battery_is_below_threshold(const struct batt_params *batt,
				enum batt_threshold_type type)
{
	/* If the state of charge isn't reliable, assume the level is fine. */
	if (batt->flags & BATT_FLAG_BAD_STATE_OF_CHARGE) {
		return false;
	}

	return batt->state_of_charge <= get_battery_threshold_percent(type);
}

/**
 * Checks if the battery's state of charge (SoC) or display charge has changed
 * since the last check. If it has, it updates the stored values and calls the
 * HOOK_BATTERY_SOC_CHANGE hook to notify other modules.
 *
 * This is primarily used in builds without CONFIG_CHARGER, where the charger
 * task isn't available to provide these notifications.
 *
 * @param batt battery parameters.
 */
void check_battery_soc_change(struct batt_params batt)
{
	/*
	 * Check for a change if:
	 * 1. The SoC value is valid AND it's different from the previous value.
	 * 2. The display charge value is different from the previous value.
	 */
	if ((!(batt.flags & BATT_FLAG_BAD_STATE_OF_CHARGE) &&
	     batt.state_of_charge != prev_charge) ||
	    (batt.display_charge != prev_disp_charge)) {
		prev_charge = batt.state_of_charge;
		prev_disp_charge = batt.display_charge;
		hook_notify(HOOK_BATTERY_SOC_CHANGE);
	}
}

void battery_poll_dynamic_info(void)
{
	struct batt_params batt;
	bool ac_present;
	bool is_charging;
	bool charger_idle = false;

	battery_get_params(&batt);
	ac_present = extpower_is_present();
	is_charging = ac_present && (batt.current >= 0);
#ifdef CONFIG_CHARGER
	charger_idle = charge_get_status()->state == ST_IDLE;
#else /* !CONFIG_CHARGER */
	/*
	 * If charger task is not available, manually check for changes
	 * in battery SoC to call appropriate hooks.
	 */
	check_battery_soc_change(batt);
#endif

	battery_set_dynamic_info(&batt, ac_present, is_charging, charger_idle);
}

int update_static_battery_info(void)
{
	int batt_serial;
	int val;
	/*
	 * The return values have type enum ec_error_list, but EC_SUCCESS is
	 * zero. We'll just look for any failures so we can try them all again.
	 */
	int rv, ret;

	struct battery_static_info *const bs = &battery_static[BATT_IDX_MAIN];

	/* Clear all static information. */
	memset(bs, 0, sizeof(*bs));

	/* Smart battery serial number is 16 bits */
	rv = battery_serial_number(&batt_serial);
	if (!rv)
		if (snprintf(bs->serial_ext, sizeof(bs->serial_ext), "%04X",
			     batt_serial) <= 0)
			rv |= EC_ERROR_UNKNOWN;

	/* Design Capacity of Full */
	ret = battery_design_capacity(&val);
	if (!ret)
		bs->design_capacity = val;
	rv |= ret;

	/* Design Voltage */
	ret = battery_design_voltage(&val);
	if (!ret)
		bs->design_voltage = val;
	rv |= ret;

	/* Cycle Count */
	ret = battery_cycle_count(&val);
	if (!ret)
		bs->cycle_count = val;
	rv |= ret;

	/* Battery Manufacturer string */
	rv |= battery_manufacturer_name(bs->manufacturer_ext,
					sizeof(bs->manufacturer_ext));

#ifdef CONFIG_PLATFORM_EC_BATTERY_MANUF_INFO
	/* Battery Manufacture info string */
	rv |= battery_manufacture_info(bs->manuf_info, sizeof(bs->manuf_info));
#endif /* CONFIG_PLATFORM_EC_BATTERY_MANUF_INFO */

	/* Manufacture Date */
	int mf_year, mf_month, mf_day;
	ret = battery_manufacture_date(&mf_year, &mf_month, &mf_day);
	if (!ret) {
		bs->manuf_year = mf_year;
		bs->manuf_month = mf_month;
		bs->manuf_day = mf_day;
	}
	rv |= ret;

	/* Battery Model string */
	rv |= battery_device_name(bs->model_ext, sizeof(bs->model_ext));

	/* Battery Type string */
	rv |= battery_device_chemistry(bs->type_ext, sizeof(bs->type_ext));

	/*
	 * b/181639264: Battery gauge follow SMBus SPEC and SMBus define
	 * cumulative clock low extend time for both controller (master) and
	 * peripheral (slave). However, I2C doesn't.
	 * Regarding this issue, we observe EC sometimes pull I2C CLK low
	 * a while after EC start running. Actually, we are not sure the
	 * reason until now.
	 * If EC pull I2C CLK low too long, and it may cause battery fw timeout
	 * because battery count cumulative clock extend time over 25ms.
	 * When it happened, battery will release both its CLK and DATA and
	 * reset itself. So, EC may get 0xFF when EC keep reading data from
	 * battery. Battery static information will be unreliable and need to
	 * be updated.
	 * This change is improvement that EC should retry if battery string is
	 * unreliable.
	 */
	if (!is_battery_string_reliable(bs->serial_ext) ||
	    !is_battery_string_reliable(bs->manufacturer_ext) ||
	    !is_battery_string_reliable(bs->model_ext) ||
	    !is_battery_string_reliable(bs->type_ext))
		rv |= EC_ERROR_UNKNOWN;

	/* Clear dynamic data, which will be updated next. For now all data
	 * is invalid and should not be trusted. */
	memset(&battery_dynamic[BATT_IDX_MAIN], 0,
	       sizeof(battery_dynamic[BATT_IDX_MAIN]));
	battery_dynamic[BATT_IDX_MAIN].flags = EC_BATT_FLAG_INVALID_DATA;

#ifdef HAS_TASK_HOSTCMD
	battery_memmap_refresh(BATT_IDX_MAIN);
#endif

	return rv;
}

void battery_set_dynamic_info(const struct batt_params *params, bool ac_present,
			      bool is_charging, bool sustainer_idle)
{
	static int batt_present;
	uint8_t tmp;
	__maybe_unused int send_batt_status_event = 0;
	__maybe_unused int send_batt_info_event = 0;
	struct ec_response_battery_dynamic_info *const bd =
		&battery_dynamic[BATT_IDX_MAIN];

	tmp = 0;
	if (ac_present)
		tmp |= EC_BATT_FLAG_AC_PRESENT;

	if (params->is_present == BP_YES) {
		tmp |= EC_BATT_FLAG_BATT_PRESENT;
		batt_present = 1;
		/* Tell the AP to read battery info if it is newly present. */
		if (!(bd->flags & EC_BATT_FLAG_BATT_PRESENT))
			send_batt_info_event++;
	} else {
		/*
		 * Require two consecutive updates with BP_NOT_SURE
		 * before reporting it gone to the host.
		 */
		if (batt_present)
			tmp |= EC_BATT_FLAG_BATT_PRESENT;
		else if (bd->flags & EC_BATT_FLAG_BATT_PRESENT)
			send_batt_info_event++;
		batt_present = 0;
	}

	if (params->flags & BATT_FLAG_BAD_ANY)
		tmp |= EC_BATT_FLAG_INVALID_DATA;

	if (!(params->flags & BATT_FLAG_BAD_VOLTAGE))
		bd->actual_voltage = params->voltage;

	if (!(params->flags & BATT_FLAG_BAD_CURRENT))
		bd->actual_current = params->current;

	if (!(params->flags & BATT_FLAG_BAD_DESIRED_VOLTAGE))
		bd->desired_voltage = params->desired_voltage;

	if (!(params->flags & BATT_FLAG_BAD_DESIRED_CURRENT))
		bd->desired_current = params->desired_current;

	if (!(params->flags & BATT_FLAG_BAD_REMAINING_CAPACITY)) {
		/*
		 * If we're running off the battery, it must have some charge.
		 * Don't report zero charge, as that has special meaning
		 * to Chrome OS powerd.
		 */
		if (params->remaining_capacity == 0 && !is_charging)
			bd->remaining_capacity = 1;
		else
			bd->remaining_capacity = params->remaining_capacity;
	}

	if (!(params->flags & BATT_FLAG_BAD_FULL_CAPACITY) &&
	    (params->full_capacity <= (bd->full_capacity - LFCC_EVENT_THRESH) ||
	     params->full_capacity >=
		     (bd->full_capacity + LFCC_EVENT_THRESH))) {
		bd->full_capacity = params->full_capacity;
		/* Poke the AP if the full_capacity changes. */
		send_batt_info_event++;
	}
	if (params->is_present == BP_YES &&
	    battery_is_below_threshold(params, BATT_THRESHOLD_TYPE_SHUTDOWN))
		tmp |= EC_BATT_FLAG_LEVEL_CRITICAL;

	if (!is_charging) {
		// Sustainer is discharging or there is insufficient power to
		// charge (including when there is no charger connected).
		tmp |= EC_BATT_FLAG_DISCHARGING;
	} else if (sustainer_idle) {
		// Sustainer is holding state, not charging nor discharging.
	} else if (params->status & STATUS_FULLY_CHARGED) {
		// Fully charged, not actually charging (despite
		// batt_is_charging).
	} else {
		// Otherwise, is_charging and no special case applies.
		tmp |= EC_BATT_FLAG_CHARGING;
	}

	if (battery_is_cut_off())
		tmp |= EC_BATT_FLAG_CUT_OFF;

	/* Tell the AP to re-read battery status if charge state changes */
	if (bd->flags != tmp)
		send_batt_status_event++;

	bd->flags = tmp;

#ifdef HAS_TASK_HOSTCMD
	battery_memmap_refresh(BATT_IDX_MAIN);
#endif

#ifdef CONFIG_HOSTCMD_EVENTS
	if (send_batt_info_event)
		host_set_single_event(EC_HOST_EVENT_BATTERY);
	if (send_batt_status_event)
		host_set_single_event(EC_HOST_EVENT_BATTERY_STATUS);
#endif
}
