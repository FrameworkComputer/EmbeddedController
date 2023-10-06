/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Module for reading battery config
 *
 * Default battery config (i.e. default_battery_conf) will be updated as a value
 * is read from a source storage (e.g. CBI). If a read fails, the failed field
 * will be ignored and search will continue. This allows a source storage to
 * store only the differences between the default and the target battery config,
 * which saves us the boot time and the storage space.
 *
 * If data in a storage is corrupted, this may result in mix of two pieces of
 * battery config. Partially updating the config (or using as much as
 * discovered) is most likely better (and safer) than entirely falling back to
 * the default battery config especially when only one field is missing or
 * corrupted.
 */

#include "battery_fuel_gauge.h"
#include "console.h"
#include "cros_board_info.h"
#include "hooks.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_SYSTEM, "BCFG " format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, "BCFG " format, ##args)

struct board_batt_params default_battery_conf;
static char manuf_name[32];
static char device_name[32];

test_export_static int batt_conf_read(enum cbi_data_tag tag, uint8_t *data,
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

test_export_static int batt_conf_read_ship_mode(struct board_batt_params *info)
{
	struct ship_mode_info *ship = &info->fuel_gauge.ship_mode;

	batt_conf_read(CBI_TAG_BATT_SHIP_MODE_REG_ADDR, &ship->reg_addr,
		       sizeof(ship->reg_addr));

	batt_conf_read(CBI_TAG_BATT_SHIP_MODE_REG_DATA,
		       (uint8_t *)&ship->reg_data, sizeof(ship->reg_data));

	return EC_SUCCESS;
}

test_export_static int batt_conf_read_sleep_mode(struct board_batt_params *info)
{
	struct sleep_mode_info *sleep = &info->fuel_gauge.sleep_mode;
	struct fuel_gauge_reg_addr_data reg;

	if (batt_conf_read(CBI_TAG_BATT_SLEEP_MODE, (uint8_t *)&reg,
			   sizeof(reg)) == EC_SUCCESS) {
		sleep->reg_addr = reg.addr;
		sleep->reg_data = reg.data;
	}

	return EC_SUCCESS;
}

test_export_static int batt_conf_read_fet_info(struct board_batt_params *info)
{
	struct fet_info *fet = &info->fuel_gauge.fet;

	batt_conf_read(CBI_TAG_BATT_FET_REG_ADDR, &fet->reg_addr,
		       sizeof(fet->reg_addr));
	batt_conf_read(CBI_TAG_BATT_FET_REG_MASK, (uint8_t *)&fet->reg_mask,
		       sizeof(fet->reg_mask));
	batt_conf_read(CBI_TAG_BATT_FET_DISCONNECT_VAL,
		       (uint8_t *)&fet->disconnect_val,
		       sizeof(fet->disconnect_val));
	batt_conf_read(CBI_TAG_BATT_FET_CFET_MASK, (uint8_t *)&fet->cfet_mask,
		       sizeof(fet->cfet_mask));
	batt_conf_read(CBI_TAG_BATT_FET_CFET_OFF_VAL,
		       (uint8_t *)&fet->cfet_off_val,
		       sizeof(fet->cfet_off_val));

	return EC_SUCCESS;
}

test_export_static int
batt_conf_read_fuel_gauge_info(struct board_batt_params *info)
{
	struct fuel_gauge_info *fg = &info->fuel_gauge;
	uint32_t d32;

	if (batt_conf_read(CBI_TAG_FUEL_GAUGE_MANUF_NAME, (uint8_t *)manuf_name,
			   sizeof(manuf_name)) == EC_SUCCESS)
		fg->manuf_name = manuf_name;

	if (batt_conf_read(CBI_TAG_FUEL_GAUGE_DEVICE_NAME,
			   (uint8_t *)device_name,
			   sizeof(device_name)) == EC_SUCCESS)
		fg->device_name = device_name;

	if (batt_conf_read(CBI_TAG_FUEL_GAUGE_FLAGS, (uint8_t *)&d32,
			   sizeof(d32)) == EC_SUCCESS) {
		fg->flags = d32;
	}

	batt_conf_read_ship_mode(info);
	batt_conf_read_sleep_mode(info);
	batt_conf_read_fet_info(info);

	return EC_SUCCESS;
}

test_export_static int
batt_conf_read_battery_info(struct board_batt_params *info)
{
	struct battery_info *batt = &info->batt_info;
	struct battery_voltage_current mvma;
	struct battery_temperature_range temp;

	batt_conf_read(CBI_TAG_BATT_VOLTAGE_MAX, (uint8_t *)&batt->voltage_max,
		       sizeof(batt->voltage_max));
	batt_conf_read(CBI_TAG_BATT_VOLTAGE_NORMAL,
		       (uint8_t *)&batt->voltage_normal,
		       sizeof(batt->voltage_normal));
	batt_conf_read(CBI_TAG_BATT_VOLTAGE_MIN, (uint8_t *)&batt->voltage_min,
		       sizeof(batt->voltage_min));
	if (batt_conf_read(CBI_TAG_BATT_PRECHARGE_VOLTAGE_CURRENT,
			   (uint8_t *)&mvma, sizeof(mvma)) == EC_SUCCESS) {
		batt->precharge_voltage = mvma.mv;
		batt->precharge_current = mvma.ma;
	}
	if (batt_conf_read(CBI_TAG_BATT_START_CHARGING_MIN_MAX_C,
			   (uint8_t *)&temp, sizeof(temp)) == EC_SUCCESS) {
		batt->start_charging_min_c = temp.min_c;
		batt->start_charging_max_c = temp.max_c;
	}
	if (batt_conf_read(CBI_TAG_BATT_CHARGING_MIN_MAX_C, (uint8_t *)&temp,
			   sizeof(temp)) == EC_SUCCESS) {
		batt->charging_min_c = temp.min_c;
		batt->charging_max_c = temp.max_c;
	}
	if (batt_conf_read(CBI_TAG_BATT_DISCHARGING_MIN_MAX_C, (uint8_t *)&temp,
			   sizeof(temp)) == EC_SUCCESS) {
		batt->discharging_min_c = temp.min_c;
		batt->discharging_max_c = temp.max_c;
	}

	return EC_SUCCESS;
}

__overridable bool board_batt_conf_enabled(void)
{
	union ec_common_control ctrl;

	if (cbi_get_common_control(&ctrl) != EC_SUCCESS)
		return false;

	return !!(ctrl.bcic_enabled);
}

test_export_static void batt_conf_main(void)
{
	CPRINTS("%s", __func__);
	if (board_batt_conf_enabled()) {
		CPRINTS("Reading CBI");
		default_battery_conf = board_battery_info[0];
		batt_conf_read_fuel_gauge_info(&default_battery_conf);
		batt_conf_read_battery_info(&default_battery_conf);
	} else {
		/* Battery config isn't in CBI. */
		CPRINTS("Detect battery");
		init_battery_type();
	}
	CPRINTS("%s done", __func__);
}
DECLARE_HOOK(HOOK_INIT, batt_conf_main, HOOK_PRIO_POST_I2C);

#ifdef CONFIG_CMD_BATTERY_CONFIG

static void batt_conf_dump(const struct board_batt_params *info)
{
	const struct fuel_gauge_info *fg = &info->fuel_gauge;
	const struct ship_mode_info *ship = &info->fuel_gauge.ship_mode;
	const struct sleep_mode_info *sleep = &info->fuel_gauge.sleep_mode;
	const struct fet_info *fet = &info->fuel_gauge.fet;
	const struct battery_info *batt = &info->batt_info;

	ccprintf("   .fuel_gauge = {\n");

	ccprintf("%02x:\t.manuf_name = \"%s\",\n",
		 CBI_TAG_FUEL_GAUGE_MANUF_NAME, fg->manuf_name);
	ccprintf("%02x:\t.device_name= \"%s\",\n",
		 CBI_TAG_FUEL_GAUGE_DEVICE_NAME, fg->device_name);
	ccprintf("%02x:\t.flags = 0x%x,\n", CBI_TAG_FUEL_GAUGE_FLAGS,
		 fg->flags);

	ccprintf("   \t.ship_mode = {\n");
	ccprintf("%02x:\t\t.reg_addr = 0x%02x,\n",
		 CBI_TAG_BATT_SHIP_MODE_REG_ADDR, ship->reg_addr);
	ccprintf("%02x:\t\t.reg_data = { 0x%04x, 0x%04x },\n",
		 CBI_TAG_BATT_SHIP_MODE_REG_DATA, ship->reg_data[0],
		 ship->reg_data[1]);
	ccprintf("   \t},\n");

	ccprintf("   \t.sleep_mode = {\n");
	ccprintf("%02x:\t\t.reg_addr = 0x%02x,\n",
		 CBI_TAG_BATT_SLEEP_MODE_REG_ADDR, sleep->reg_addr);
	ccprintf("%02x:\t\t.reg_data = 0x%04x,\n",
		 CBI_TAG_BATT_SLEEP_MODE_REG_DATA, sleep->reg_data);
	ccprintf("   \t},\n");

	ccprintf("   \t.fet = {\n");
	ccprintf("%02x:\t\t.reg_addr = 0x%02x,\n", CBI_TAG_BATT_FET_REG_ADDR,
		 fet->reg_addr);
	ccprintf("%02x:\t\t.reg_mask = 0x%04x,\n", CBI_TAG_BATT_FET_REG_MASK,
		 fet->reg_mask);
	ccprintf("%02x:\t\t.disconnect_val = 0x%x,\n",
		 CBI_TAG_BATT_FET_DISCONNECT_VAL, fet->disconnect_val);
	ccprintf("%02x:\t\t.cfet_mask = 0x%04x,\n", CBI_TAG_BATT_FET_CFET_MASK,
		 fet->cfet_mask);
	ccprintf("%02x:\t\t.cfet_off_val = 0x%04x,\n",
		 CBI_TAG_BATT_FET_CFET_OFF_VAL, fet->cfet_off_val);
	ccprintf("   \t},\n");

	ccprintf("   },\n"); /* end of fuel_gauge */

	ccprintf("   .batt_info = {\n");
	ccprintf("%02x:\t.voltage_max = %d,\n", CBI_TAG_BATT_VOLTAGE_MAX,
		 batt->voltage_max);
	ccprintf("%02x:\t.voltage_normal = %d,\n", CBI_TAG_BATT_VOLTAGE_NORMAL,
		 batt->voltage_normal);
	ccprintf("%02x:\t.voltage_min = %d,\n", CBI_TAG_BATT_VOLTAGE_MIN,
		 batt->voltage_min);
	ccprintf("%02x:\t.precharge_voltage= %d,\n",
		 CBI_TAG_BATT_PRECHARGE_VOLTAGE, batt->precharge_voltage);
	ccprintf("%02x:\t.precharge_current = %d,\n",
		 CBI_TAG_BATT_PRECHARGE_CURRENT, batt->precharge_current);
	ccprintf("%02x:\t.start_charging_min_c = %d,\n",
		 CBI_TAG_BATT_START_CHARGING_MIN_C, batt->start_charging_min_c);
	ccprintf("%02x:\t.start_charging_max_c = %d,\n",
		 CBI_TAG_BATT_START_CHARGING_MAX_C, batt->start_charging_max_c);
	ccprintf("%02x:\t.charging_min_c = %d,\n", CBI_TAG_BATT_CHARGING_MIN_C,
		 batt->charging_min_c);
	ccprintf("%02x:\t.charging_max_c = %d,\n", CBI_TAG_BATT_CHARGING_MAX_C,
		 batt->charging_max_c);
	ccprintf("%02x:\t.discharging_min_c = %d,\n",
		 CBI_TAG_BATT_DISCHARGING_MIN_C, batt->discharging_min_c);
	ccprintf("%02x:\t.discharging_max_c = %d,\n",
		 CBI_TAG_BATT_DISCHARGING_MAX_C, batt->discharging_max_c);
	ccprintf("   },\n"); /* end of batt_info */
}

static int cc_batt_conf(int argc, const char *argv[])
{
	static struct board_batt_params scratch_battery_conf;

	if (argc == 1) {
		batt_conf_dump(&scratch_battery_conf);
	} else if (argc == 2 && strcasecmp(argv[1], "read") == 0) {
		batt_conf_read_fuel_gauge_info(&scratch_battery_conf);
		batt_conf_read_battery_info(&scratch_battery_conf);
	} else if (argc == 2 && strcasecmp(argv[1], "reset") == 0) {
		memcpy(&scratch_battery_conf, get_batt_params(),
		       sizeof(scratch_battery_conf));
	} else {
		return EC_ERROR_PARAM_COUNT;
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(bcfg, cc_batt_conf, "[read | reset]",
			"\n"
			"Dump scratch battery config\n"
			"[reset] Load effective config to scratch buffer\n"
			"[read] Load config from CBI to scratch buffer\n");
#endif /* CONFIG_CMD_BATTERY_CONFIG */
