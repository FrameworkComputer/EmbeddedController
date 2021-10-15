/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery V1 APIs.
 */

#include "battery.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "host_command.h"
#include "math_util.h"
#include "printf.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

/* Returns zero if every item was updated. */
int update_static_battery_info(void)
{
	char *batt_str;
	int batt_serial;
	uint8_t batt_flags = 0;
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
	if (extpower_is_present())
		batt_flags |= EC_BATT_FLAG_AC_PRESENT;
	*host_get_memmap(EC_MEMMAP_BATT_FLAG) = batt_flags;

	if (rv)
		charge_problem(PR_STATIC_UPDATE, rv);
	else
		/* No errors seen. Battery data is now present */
		*host_get_memmap(EC_MEMMAP_BATTERY_VERSION) = 1;

	return rv;
}

void update_dynamic_battery_info(void)
{
	/* The memmap address is constant. We should fix these calls somehow. */
	int *memmap_volt = (int *)host_get_memmap(EC_MEMMAP_BATT_VOLT);
	int *memmap_rate = (int *)host_get_memmap(EC_MEMMAP_BATT_RATE);
	int *memmap_cap = (int *)host_get_memmap(EC_MEMMAP_BATT_CAP);
	int *memmap_lfcc = (int *)host_get_memmap(EC_MEMMAP_BATT_LFCC);
	uint8_t *memmap_flags = host_get_memmap(EC_MEMMAP_BATT_FLAG);
	uint8_t tmp;
	int send_batt_status_event = 0;
	int send_batt_info_event = 0;
	static int batt_present;
	struct charge_state_data *curr;

	curr = charge_get_status();
	tmp = 0;
	if (curr->ac)
		tmp |= EC_BATT_FLAG_AC_PRESENT;

	if (curr->batt.is_present == BP_YES) {
		tmp |= EC_BATT_FLAG_BATT_PRESENT;
		batt_present = 1;
		/* Tell the AP to read battery info if it is newly present. */
		if (!(*memmap_flags & EC_BATT_FLAG_BATT_PRESENT))
			send_batt_info_event++;
	} else {
		/*
		 * Require two consecutive updates with BP_NOT_SURE
		 * before reporting it gone to the host.
		 */
		if (batt_present)
			tmp |= EC_BATT_FLAG_BATT_PRESENT;
		else if (*memmap_flags & EC_BATT_FLAG_BATT_PRESENT)
			send_batt_info_event++;
		batt_present = 0;
	}

	if (curr->batt.flags & EC_BATT_FLAG_INVALID_DATA)
		tmp |= EC_BATT_FLAG_INVALID_DATA;

	if (!(curr->batt.flags & BATT_FLAG_BAD_VOLTAGE))
		*memmap_volt = curr->batt.voltage;

	if (!(curr->batt.flags & BATT_FLAG_BAD_CURRENT))
		*memmap_rate = ABS(curr->batt.current);

	if (!(curr->batt.flags & BATT_FLAG_BAD_REMAINING_CAPACITY)) {
		/*
		 * If we're running off the battery, it must have some charge.
		 * Don't report zero charge, as that has special meaning
		 * to Chrome OS powerd.
		 */
		if (curr->batt.remaining_capacity == 0 && !curr->batt_is_charging)
			*memmap_cap = 1;
		else
			*memmap_cap = curr->batt.remaining_capacity;
	}

	if (!(curr->batt.flags & BATT_FLAG_BAD_FULL_CAPACITY) &&
	    (curr->batt.full_capacity <= (*memmap_lfcc - LFCC_EVENT_THRESH) ||
	     curr->batt.full_capacity >= (*memmap_lfcc + LFCC_EVENT_THRESH))) {
		*memmap_lfcc = curr->batt.full_capacity;
		/* Poke the AP if the full_capacity changes. */
		send_batt_info_event++;
	}

	if (curr->batt.is_present == BP_YES &&
	    !(curr->batt.flags & BATT_FLAG_BAD_STATE_OF_CHARGE) &&
	    curr->batt.state_of_charge <= BATTERY_LEVEL_CRITICAL)
		tmp |= EC_BATT_FLAG_LEVEL_CRITICAL;

	tmp |= curr->batt_is_charging ? EC_BATT_FLAG_CHARGING :
				       EC_BATT_FLAG_DISCHARGING;

	/* Tell the AP to re-read battery status if charge state changes */
	if (*memmap_flags != tmp)
		send_batt_status_event++;

	/* Update flags before sending host events. */
	*memmap_flags = tmp;

	if (send_batt_info_event)
		host_set_single_event(EC_HOST_EVENT_BATTERY);
	if (send_batt_status_event)
		host_set_single_event(EC_HOST_EVENT_BATTERY_STATUS);
}
