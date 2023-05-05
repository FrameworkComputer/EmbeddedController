/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Library for read/write battery info in CBI
 *
 * Default battery info (i.e. default_battery_info) will be updated as a value
 * is read from CBI. If a read fails, the failed field will be ignored and
 * search will continue. This allows CBI to store only the differences between
 * the default and the target battery info, which saves us the boot time and the
 * CBI space.
 *
 * If CBI is corrupted, this may result in mix of two pieces of battery info.
 * Partially updating the info (or using as much as discovered) is most likely
 * better (and safer) than entirely falling back to the default battery info
 * especially when only one field is missing or corrupted.
 */

#include "battery_fuel_gauge.h"
#include "console.h"
#include "cros_board_info.h"
#include "hooks.h"

#define CPRINTF(format, args...) cprintf(CC_SYSTEM, "CBI " format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, "CBI " format, ##args)

test_export_static int batt_cbi_read(enum cbi_data_tag tag, uint8_t *data,
				     uint8_t size)
{
	int rv;

	rv = cbi_get_board_info(tag, data, &size);
	if (rv == EC_ERROR_UNKNOWN)
		/* Tag not found. This is ok. Fall back to default. */
		return EC_ERROR_UNKNOWN;

	if (rv == EC_ERROR_INVAL) {
		/* Data in CBI is larger than expected. */
		CPRINTS("batt data tag=%d is larger (%d)", tag, size);
		return EC_ERROR_INVAL;
	}

	/* It's ok size of the cbi data is smaller than expected, */
	return EC_SUCCESS;
}

test_export_static int batt_cbi_read_ship_mode(struct board_batt_params *info)
{
	struct ship_mode_info *ship = &info->fuel_gauge.ship_mode;
	uint8_t d8;

	batt_cbi_read(CBI_TAG_BATT_SHIP_MODE_REG_ADDR, &ship->reg_addr,
		      sizeof(ship->reg_addr));

	batt_cbi_read(CBI_TAG_BATT_SHIP_MODE_REG_DATA,
		      (uint8_t *)&ship->reg_data, sizeof(ship->reg_data));

	if (batt_cbi_read(CBI_TAG_BATT_SHIP_MODE_FLAGS, &d8, sizeof(d8)) ==
	    EC_SUCCESS)
		ship->wb_support = d8 & BIT(0) ? 1 : 0;

	return EC_SUCCESS;
}

test_export_static int batt_cbi_read_sleep_mode(struct board_batt_params *info)
{
	struct sleep_mode_info *sleep = &info->fuel_gauge.sleep_mode;
	uint8_t d8;

	batt_cbi_read(CBI_TAG_BATT_SLEEP_MODE_REG_ADDR, &sleep->reg_addr,
		      sizeof(sleep->reg_addr));

	batt_cbi_read(CBI_TAG_BATT_SLEEP_MODE_REG_DATA,
		      (uint8_t *)&sleep->reg_data, sizeof(sleep->reg_data));

	if (batt_cbi_read(CBI_TAG_BATT_SLEEP_MODE_FLAGS, &d8, sizeof(d8)) ==
	    EC_SUCCESS)
		sleep->sleep_supported = d8 & BIT(0) ? 1 : 0;

	return EC_SUCCESS;
}

test_export_static int batt_cbi_read_fet_info(struct board_batt_params *info)
{
	struct fet_info *fet = &info->fuel_gauge.fet;
	uint8_t d8;

	batt_cbi_read(CBI_TAG_BATT_FET_REG_ADDR, &fet->reg_addr,
		      sizeof(fet->reg_addr));
	batt_cbi_read(CBI_TAG_BATT_FET_REG_MASK, (uint8_t *)&fet->reg_mask,
		      sizeof(fet->reg_mask));
	batt_cbi_read(CBI_TAG_BATT_FET_DISCONNECT_VAL,
		      (uint8_t *)&fet->disconnect_val,
		      sizeof(fet->disconnect_val));
	batt_cbi_read(CBI_TAG_BATT_FET_CFET_MASK, (uint8_t *)&fet->cfet_mask,
		      sizeof(fet->cfet_mask));
	batt_cbi_read(CBI_TAG_BATT_FET_CFET_OFF_VAL,
		      (uint8_t *)&fet->cfet_off_val, sizeof(fet->cfet_off_val));
	if (batt_cbi_read(CBI_TAG_BATT_FET_FLAGS, &d8, sizeof(d8)) ==
	    EC_SUCCESS)
		fet->mfgacc_support = d8 & BIT(0) ? 1 : 0;

	return EC_SUCCESS;
}

test_export_static int
batt_cbi_read_fuel_gauge_info(struct board_batt_params *info)
{
	struct fuel_gauge_info *fg = &info->fuel_gauge;
	uint8_t d8;

	batt_cbi_read(CBI_TAG_FUEL_GAUGE_MANUF_NAME, (uint8_t *)&fg->manuf_name,
		      sizeof(fg->manuf_name));
	batt_cbi_read(CBI_TAG_FUEL_GAUGE_DEVICE_NAME,
		      (uint8_t *)&fg->device_name, sizeof(fg->device_name));
	if (batt_cbi_read(CBI_TAG_FUEL_GAUGE_FLAGS, &d8, sizeof(d8)) ==
	    EC_SUCCESS)
		fg->override_nil = d8 & BIT(0) ? 1 : 0;

	batt_cbi_read_ship_mode(info);
	batt_cbi_read_sleep_mode(info);
	batt_cbi_read_fet_info(info);

	return EC_SUCCESS;
}

test_export_static int
batt_cbi_read_battery_info(struct board_batt_params *info)
{
	const struct battery_info *batt = &info->batt_info;

	batt_cbi_read(CBI_TAG_BATT_VOLTAGE_MAX, (uint8_t *)&batt->voltage_max,
		      sizeof(batt->voltage_max));
	batt_cbi_read(CBI_TAG_BATT_VOLTAGE_NORMAL,
		      (uint8_t *)&batt->voltage_normal,
		      sizeof(batt->voltage_normal));
	batt_cbi_read(CBI_TAG_BATT_VOLTAGE_MIN, (uint8_t *)&batt->voltage_min,
		      sizeof(batt->voltage_min));
	batt_cbi_read(CBI_TAG_BATT_PRECHARGE_VOLTAGE,
		      (uint8_t *)&batt->precharge_voltage,
		      sizeof(batt->precharge_voltage));
	batt_cbi_read(CBI_TAG_BATT_PRECHARGE_CURRENT,
		      (uint8_t *)&batt->precharge_current,
		      sizeof(batt->precharge_current));
	batt_cbi_read(CBI_TAG_BATT_START_CHARGING_MIN_C,
		      (uint8_t *)&batt->start_charging_min_c,
		      sizeof(batt->start_charging_min_c));
	batt_cbi_read(CBI_TAG_BATT_START_CHARGING_MAX_C,
		      (uint8_t *)&batt->start_charging_max_c,
		      sizeof(batt->start_charging_max_c));
	batt_cbi_read(CBI_TAG_BATT_CHARGING_MIN_C,
		      (uint8_t *)&batt->charging_min_c,
		      sizeof(batt->charging_min_c));
	batt_cbi_read(CBI_TAG_BATT_CHARGING_MAX_C,
		      (uint8_t *)&batt->charging_max_c,
		      sizeof(batt->charging_max_c));
	batt_cbi_read(CBI_TAG_BATT_DISCHARGING_MIN_C,
		      (uint8_t *)&batt->discharging_min_c,
		      sizeof(batt->discharging_min_c));
	batt_cbi_read(CBI_TAG_BATT_DISCHARGING_MAX_C,
		      (uint8_t *)&batt->discharging_max_c,
		      sizeof(batt->discharging_max_c));

	return EC_SUCCESS;
}

test_export_static void batt_cbi_main(void)
{
	CPRINTS("%s", __func__);
	batt_cbi_read_fuel_gauge_info(&default_battery_info);
	batt_cbi_read_battery_info(&default_battery_info);
	CPRINTS("%s done", __func__);
}
DECLARE_HOOK(HOOK_INIT, batt_cbi_main, HOOK_PRIO_DEFAULT);
