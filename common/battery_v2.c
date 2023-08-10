/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery V2 APIs.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#include "battery.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
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

	*memmap_volt = battery_dynamic[i].actual_voltage;
	/*
	 * Rate must be absolute, flags will indicate whether
	 * the battery is charging or discharging.
	 */
	*memmap_rate = ABS(battery_dynamic[i].actual_current);
	*memmap_cap = battery_dynamic[i].remaining_capacity;
	*memmap_lfcc = battery_dynamic[i].full_capacity;
	*memmap_flags = battery_dynamic[i].flags;
}

#ifdef CONFIG_HOSTCMD_BATTERY_V2
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

		r->design_capacity = bs->design_capacity;
		r->design_voltage = bs->design_voltage;
		r->cycle_count = bs->cycle_count;

		strzcpy(r->manufacturer, bs->manufacturer_ext,
			sizeof(r->manufacturer));
		strzcpy(r->device_name, bs->model_ext, sizeof(r->device_name));
		strzcpy(r->serial, bs->serial_ext, sizeof(r->serial));
		strzcpy(r->chemistry, bs->type_ext, sizeof(r->chemistry));

		args->response_size = sizeof(*r);
	} else {
		return EC_RES_INVALID_VERSION;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_GET_STATIC, host_command_battery_get_static,
		     EC_VER_MASK(0) | EC_VER_MASK(1) | EC_VER_MASK(2));

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
#endif /* CONFIG_HOSTCMD_BATTERY_V2 */

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

	/* Zero the dynamic entries. They'll come next. */
	memset(&battery_dynamic[BATT_IDX_MAIN], 0,
	       sizeof(battery_dynamic[BATT_IDX_MAIN]));

	if (rv)
		charge_problem(PR_STATIC_UPDATE, rv);

#ifdef HAS_TASK_HOSTCMD
	battery_memmap_refresh(BATT_IDX_MAIN);
#endif

	return rv;
}

void update_dynamic_battery_info(void)
{
	static int batt_present;
	uint8_t tmp;
	int send_batt_status_event = 0;
	int send_batt_info_event = 0;
	struct charge_state_data *curr;
	struct ec_response_battery_dynamic_info *const bd =
		&battery_dynamic[BATT_IDX_MAIN];

	curr = charge_get_status();
	tmp = 0;
	if (curr->ac)
		tmp |= EC_BATT_FLAG_AC_PRESENT;

	if (curr->batt.is_present == BP_YES) {
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

	if (curr->batt.flags & BATT_FLAG_BAD_ANY)
		tmp |= EC_BATT_FLAG_INVALID_DATA;

	if (!(curr->batt.flags & BATT_FLAG_BAD_VOLTAGE))
		bd->actual_voltage = curr->batt.voltage;

	if (!(curr->batt.flags & BATT_FLAG_BAD_CURRENT))
		bd->actual_current = curr->batt.current;

	if (!(curr->batt.flags & BATT_FLAG_BAD_DESIRED_VOLTAGE))
		bd->desired_voltage = curr->batt.desired_voltage;

	if (!(curr->batt.flags & BATT_FLAG_BAD_DESIRED_CURRENT))
		bd->desired_current = curr->batt.desired_current;

	if (!(curr->batt.flags & BATT_FLAG_BAD_REMAINING_CAPACITY)) {
		/*
		 * If we're running off the battery, it must have some charge.
		 * Don't report zero charge, as that has special meaning
		 * to Chrome OS powerd.
		 */
		if (curr->batt.remaining_capacity == 0 &&
		    !curr->batt_is_charging)
			bd->remaining_capacity = 1;
		else
			bd->remaining_capacity = curr->batt.remaining_capacity;
	}

	if (!(curr->batt.flags & BATT_FLAG_BAD_FULL_CAPACITY) &&
	    (curr->batt.full_capacity <=
		     (bd->full_capacity - LFCC_EVENT_THRESH) ||
	     curr->batt.full_capacity >=
		     (bd->full_capacity + LFCC_EVENT_THRESH))) {
		bd->full_capacity = curr->batt.full_capacity;
		/* Poke the AP if the full_capacity changes. */
		send_batt_info_event++;
	}

	if (curr->batt.is_present == BP_YES &&
	    battery_is_below_threshold(BATT_THRESHOLD_TYPE_SHUTDOWN, false))
		tmp |= EC_BATT_FLAG_LEVEL_CRITICAL;

	tmp |= curr->batt_is_charging ? EC_BATT_FLAG_CHARGING :
					EC_BATT_FLAG_DISCHARGING;

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
