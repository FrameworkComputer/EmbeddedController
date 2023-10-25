/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery fuel gauge parameters
 */

#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "builtin/assert.h"
#include "console.h"
#include "cros_board_info.h"
#include "hooks.h"
#include "i2c.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)
#define BCFGPRT(format, args...) cprints(CC_CHARGER, "BCFG " format, ##args)

/*
 * Pointer to effective config (default_battery_conf or board_battery_info[]).
 */
const struct board_batt_params *battery_conf;

/* Copies of config and strings of a matching battery found in CBI. */
test_export_static struct board_batt_params default_battery_conf;
static char manuf_name[member_size(struct batt_conf_header, manuf_name)];
static char device_name[member_size(struct batt_conf_header, device_name)];

/*
 * Authenticate the battery connected.
 *
 * Compare the manufacturer name read from the fuel gauge to the
 * manufacturer names defined in the board_battery_info table. If
 * a device name has been specified in the board_battery_info table,
 * then both the manufacturer and device name must match.
 */
test_export_static bool authenticate_battery_type(int index,
						  const char *manuf_name)
{
	char device_name[32];

	const struct fuel_gauge_info *const fuel_gauge =
		&board_battery_info[index].fuel_gauge;
	int len = 0;

	/* check for valid index */
	if (index >= BATTERY_TYPE_COUNT)
		return false;

	/* manufacturer name mismatch */
	if (strcasecmp(manuf_name, fuel_gauge->manuf_name))
		return false;

	/* device name is specified in table */
	if (fuel_gauge->device_name != NULL) {
		/* Get the device name */
		if (battery_device_name(device_name, sizeof(device_name)))
			return false;

		len = strlen(fuel_gauge->device_name);

		/* device name mismatch */
		if (strncasecmp(device_name, fuel_gauge->device_name, len))
			return false;
	}

	CPRINTS("found batt:%s", fuel_gauge->manuf_name);
	return true;
}

#ifdef CONFIG_BATTERY_TYPE_NO_AUTO_DETECT

/* When battery type is not initialized */
#define BATTERY_TYPE_UNINITIALIZED -1

/* Variable to decide the battery type */
static int fixed_battery_type = BATTERY_TYPE_UNINITIALIZED;

/*
 * Function to get the fixed battery type.
 */
static int battery_get_fixed_battery_type(void)
{
	if (fixed_battery_type == BATTERY_TYPE_UNINITIALIZED) {
		CPRINTS("Warning: Battery type is not Initialized! "
			"Setting to default battery type.\n");
		fixed_battery_type = DEFAULT_BATTERY_TYPE;
	}

	return fixed_battery_type;
}

/*
 * Function to set the battery type, when auto-detection cannot be used.
 */
void battery_set_fixed_battery_type(int type)
{
	if (type < BATTERY_TYPE_COUNT)
		fixed_battery_type = type;
}
#endif /* CONFIG_BATTERY_TYPE_NO_AUTO_DETECT */

/**
 * Allows us to override the battery in order to select the battery which has
 * the right configuration for the test.
 */
test_export_static int battery_fuel_gauge_type_override = -1;

/* Get type of the battery connected on the board */
static int get_battery_type(void)
{
	char manuf_name[32];
	int i;
	static enum battery_type battery_type = BATTERY_TYPE_COUNT;

	if (IS_ENABLED(TEST_BUILD) && battery_fuel_gauge_type_override >= 0) {
		return battery_fuel_gauge_type_override;
	}

	/*
	 * If battery_type is not the default value, then can return here
	 * as there is no need to query the fuel gauge.
	 */
	if (battery_type != BATTERY_TYPE_COUNT)
		return battery_type;

	/* Get the manufacturer name. If can't read then just exit */
	if (battery_manufacturer_name(manuf_name, sizeof(manuf_name)))
		return battery_type;

#if defined(CONFIG_BATTERY_TYPE_NO_AUTO_DETECT)
	i = battery_get_fixed_battery_type();
	if (authenticate_battery_type(i, manuf_name))
		battery_type = i;
#else
	for (i = 0; i < BATTERY_TYPE_COUNT; i++) {
		if (authenticate_battery_type(i, manuf_name)) {
			battery_type = i;
			break;
		}
	}
#endif

	return battery_type;
}

__overridable int board_get_default_battery_type(void)
{
	return DEFAULT_BATTERY_TYPE;
}

void init_battery_type(void)
{
	int type = get_battery_type();

	if (type == BATTERY_TYPE_COUNT) {
		BCFGPRT("Config not found");
		type = board_get_default_battery_type();
	}
	BCFGPRT("Found config #%d", type);

	battery_conf = &board_battery_info[type];
}

const struct board_batt_params *get_batt_params(void)
{
	if (IS_ENABLED(TEST_BUILD) && battery_fuel_gauge_type_override >= 0) {
		return &board_battery_info[battery_fuel_gauge_type_override];
	}

	return battery_conf;
}

const struct battery_info *battery_get_info(void)
{
	return &get_batt_params()->batt_info;
}

static int bcfg_search_in_cbi(struct board_batt_params *info)
{
	char manuf[32];
	char device[32];
	int tag = CBI_TAG_BATTERY_CONFIG;

	if (battery_manufacturer_name(manuf, sizeof(manuf))) {
		BCFGPRT("Manuf not found");
		return EC_ERROR_UNKNOWN;
	}

	if (battery_device_name(device, sizeof(device))) {
		BCFGPRT("Name not found");
		memset(device, 0, sizeof(device));
		/* Battery name is optional. Proceed. */
	}

	BCFGPRT("Battery says %s,%s", manuf, device);

	while (1) {
		struct batt_conf_header head;
		uint8_t size = sizeof(head);
		int rv;

		rv = cbi_get_board_info(tag, (void *)&head, &size);
		if (rv) {
			BCFGPRT("No more configs (%d)", rv);
			return rv;
		}
		BCFGPRT("Checking config #%d...", tag - CBI_TAG_BATTERY_CONFIG);
		tag++;

		if (head.struct_version > 0) {
			BCFGPRT("Version mismatch: 0x%x", head.struct_version);
			continue;
		}

		if (strcasecmp(head.manuf_name, manuf)) {
			BCFGPRT("Manuf mismatch: %s", head.manuf_name);
			continue;
		}

		/* "" means a wild card (or don't care). */
		if (head.device_name[0] &&
		    strcasecmp(head.device_name, device)) {
			BCFGPRT("Name mismatch: %s", head.device_name);
			continue;
		}

		BCFGPRT("Matched");
		memcpy(info, &head.config, sizeof(*info));
		strncpy(manuf_name, head.manuf_name, sizeof(manuf_name));
		info->fuel_gauge.manuf_name = manuf_name;
		if (head.device_name[0]) {
			strncpy(device_name, head.device_name,
				sizeof(device_name));
			info->fuel_gauge.device_name = device_name;
		} else {
			info->fuel_gauge.device_name = NULL;
		}

		return EC_SUCCESS;
	}
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
	if (IS_ENABLED(CONFIG_BATTERY_CONFIG_IN_CBI) &&
	    board_batt_conf_enabled()) {
		BCFGPRT("Searching in CBI");
		if (bcfg_search_in_cbi(&default_battery_conf) == EC_SUCCESS) {
			battery_conf = &default_battery_conf;
			return;
		}
	}
	/* Battery config isn't in CBI. */
	BCFGPRT("Searching in FW");
	init_battery_type();
}
DECLARE_HOOK(HOOK_INIT, batt_conf_main, HOOK_PRIO_POST_I2C);

#ifndef CONFIG_FUEL_GAUGE
/**
 * Battery cut off command via SMBus write block.
 *
 * @param ship_mode		Battery ship mode information
 * @return non-zero if error
 */
static int cut_off_battery_block_write(const struct ship_mode_info *ship_mode)
{
	int rv;

	uint8_t cutdata[3] = {
		0x02,
		ship_mode->reg_data[0] & 0xFF,
		ship_mode->reg_data[0] >> 8,
	};

	/* SMBus protocols are block write, which include byte count
	 * byte. Byte count segments are required to communicate
	 * required action and the number of data bytes.
	 * Due to ship mode command requires writing data values twice
	 * to cutoff the battery, so byte count is 0x02.
	 */
	rv = sb_write_block(ship_mode->reg_addr, cutdata, sizeof(cutdata));
	if (rv)
		return rv;

	/* Use the next set of values */
	cutdata[1] = ship_mode->reg_data[1] & 0xFF;
	cutdata[2] = ship_mode->reg_data[1] >> 8;

	return sb_write_block(ship_mode->reg_addr, cutdata, sizeof(cutdata));
}

/**
 * Battery cut off command via SMBus write word.
 *
 * @param ship_mode		Battery ship mode information
 * @return non-zero if error
 */
static int cut_off_battery_sb_write(const struct ship_mode_info *ship_mode)
{
	int rv;

	/* Ship mode command requires writing 2 data values */
	rv = sb_write(ship_mode->reg_addr, ship_mode->reg_data[0]);
	if (rv)
		return rv;

	return sb_write(ship_mode->reg_addr, ship_mode->reg_data[1]);
}

int board_cut_off_battery(void)
{
	const struct board_batt_params *params = get_batt_params();
	int rv;

	/* If battery is unknown can't send ship mode command */
	if (!params)
		return EC_RES_ERROR;

	if (params->fuel_gauge.flags & FUEL_GAUGE_FLAG_WRITE_BLOCK)
		rv = cut_off_battery_block_write(&params->fuel_gauge.ship_mode);
	else
		rv = cut_off_battery_sb_write(&params->fuel_gauge.ship_mode);

	return rv ? EC_RES_ERROR : EC_RES_SUCCESS;
}
#endif /* !CONFIG_FUEL_GAUGE */

#ifndef TEST_BATTERY_CONFIG
enum ec_error_list battery_sleep_fuel_gauge(void)
{
	const struct board_batt_params *params = get_batt_params();
	const struct sleep_mode_info *sleep_command;

	/* Sleep entry command must be supplied as it will vary by gauge */
	if (!params)
		return EC_ERROR_UNKNOWN;

	sleep_command = &params->fuel_gauge.sleep_mode;

	if (!(params->fuel_gauge.flags & FUEL_GAUGE_FLAG_SLEEP_MODE))
		return EC_ERROR_UNIMPLEMENTED;

	return sb_write(sleep_command->reg_addr, sleep_command->reg_data);
}

static enum ec_error_list battery_get_fet_status_regval(int *regval)
{
	const struct board_batt_params *params = get_batt_params();
	int rv;
	uint8_t data[6];

	ASSERT(params);

	/* Read the status of charge/discharge FETs */
	if (params->fuel_gauge.flags & FUEL_GAUGE_FLAG_MFGACC) {
		if (params->fuel_gauge.flags & FUEL_GAUGE_FLAG_MFGACC_SMB_BLOCK)
			rv = sb_read_mfgacc_block(PARAM_OPERATION_STATUS,
						  SB_ALT_MANUFACTURER_ACCESS,
						  data, sizeof(data));
		else
			rv = sb_read_mfgacc(PARAM_OPERATION_STATUS,
					    SB_ALT_MANUFACTURER_ACCESS, data,
					    sizeof(data));
		/* Get the lowest 16bits of the OperationStatus() data */
		*regval = data[2] | data[3] << 8;
	} else
		rv = sb_read(params->fuel_gauge.fet.reg_addr, regval);

	return rv;
}

test_mockable int battery_is_charge_fet_disabled(void)
{
	const struct board_batt_params *params = get_batt_params();
	int rv;
	int reg;

	/* If battery type is not known, can't check CHG/DCHG FETs */
	if (!params) {
		/* Still don't know, so return here */
		return -1;
	}

	/*
	 * If the CFET mask hasn't been defined, assume that it's not disabled.
	 */
	if (!params->fuel_gauge.fet.cfet_mask)
		return 0;

	rv = battery_get_fet_status_regval(&reg);
	if (rv)
		return -1;

	return (reg & params->fuel_gauge.fet.cfet_mask) ==
	       params->fuel_gauge.fet.cfet_off_val;
}

/*
 * This function checks the charge/discharge FET status bits. Each battery type
 * supported provides the register address, mask, and disconnect value for these
 * 2 FET status bits. If the FET status matches the disconnected value, then
 * BATTERY_DISCONNECTED is returned. This function is required to handle the
 * cases when the fuel gauge is awake and will return a non-zero state of
 * charge, but is not able yet to provide power (i.e. discharge FET is not
 * active). By returning BATTERY_DISCONNECTED the AP will not be powered up
 * until either the external charger is able to provided enough power, or
 * the battery is able to provide power and thus prevent a brownout when the
 * AP is powered on by the EC.
 */
enum battery_disconnect_state battery_get_disconnect_state(void)
{
	const struct board_batt_params *params = get_batt_params();
	int reg;

	/* If battery type is not known, can't check CHG/DCHG FETs */
	if (!params) {
		/* Still don't know, so return here */
		return BATTERY_DISCONNECT_ERROR;
	}

	if (battery_get_fet_status_regval(&reg))
		return BATTERY_DISCONNECT_ERROR;

	if ((reg & params->fuel_gauge.fet.reg_mask) ==
	    params->fuel_gauge.fet.disconnect_val) {
		CPRINTS("Batt disconnected: reg 0x%04x mask 0x%04x disc 0x%04x",
			reg, params->fuel_gauge.fet.reg_mask,
			params->fuel_gauge.fet.disconnect_val);
		return BATTERY_DISCONNECTED;
	}

	return BATTERY_NOT_DISCONNECTED;
}
#endif /* TEST_BATTERY_CONFIG */

__overridable int
board_battery_imbalance_mv(const struct board_batt_params *info)
{
	return 0;
}

int battery_imbalance_mv(void)
{
	return board_battery_imbalance_mv(get_batt_params());
}

#ifdef CONFIG_CMD_BATTERY_CONFIG

void batt_conf_dump(const struct board_batt_params *info)
{
	const struct fuel_gauge_info *fg = &info->fuel_gauge;
	const struct ship_mode_info *ship = &info->fuel_gauge.ship_mode;
	const struct sleep_mode_info *sleep = &info->fuel_gauge.sleep_mode;
	const struct fet_info *fet = &info->fuel_gauge.fet;
	const struct battery_info *batt = &info->batt_info;

	ccprintf(".fuel_gauge = {\n");

	ccprintf("\t.manuf_name = \"%s\",\n", fg->manuf_name);
	ccprintf("\t.device_name= \"%s\",\n", fg->device_name);
	ccprintf("\t.flags = 0x%x,\n", fg->flags);

	ccprintf("\t.ship_mode = {\n");
	ccprintf("\t\t.reg_addr = 0x%02x,\n", ship->reg_addr);
	ccprintf("\t\t.reg_data = { 0x%04x, 0x%04x },\n", ship->reg_data[0],
		 ship->reg_data[1]);
	ccprintf("\t},\n");

	ccprintf("\t.sleep_mode = {\n");
	ccprintf("\t\t.reg_addr = 0x%02x,\n", sleep->reg_addr);
	ccprintf("\t\t.reg_data = 0x%04x,\n", sleep->reg_data);
	ccprintf("\t},\n");

	ccprintf("\t.fet = {\n");
	ccprintf("\t\t.reg_addr = 0x%02x,\n", fet->reg_addr);
	ccprintf("\t\t.reg_mask = 0x%04x,\n", fet->reg_mask);
	ccprintf("\t\t.disconnect_val = 0x%04x,\n", fet->disconnect_val);
	ccprintf("\t\t.cfet_mask = 0x%04x,\n", fet->cfet_mask);
	ccprintf("\t\t.cfet_off_val = 0x%04x,\n", fet->cfet_off_val);
	ccprintf("\t},\n");

	ccprintf("},\n"); /* end of fuel_gauge */

	ccprintf(".batt_info = {\n");
	ccprintf("\t.voltage_max = %d,\n", batt->voltage_max);
	ccprintf("\t.voltage_normal = %d,\n", batt->voltage_normal);
	ccprintf("\t.voltage_min = %d,\n", batt->voltage_min);
	ccprintf("\t.precharge_voltage= %d,\n", batt->precharge_voltage);
	ccprintf("\t.precharge_current = %d,\n", batt->precharge_current);
	ccprintf("\t.start_charging_min_c = %d,\n", batt->start_charging_min_c);
	ccprintf("\t.start_charging_max_c = %d,\n", batt->start_charging_max_c);
	ccprintf("\t.charging_min_c = %d,\n", batt->charging_min_c);
	ccprintf("\t.charging_max_c = %d,\n", batt->charging_max_c);
	ccprintf("\t.discharging_min_c = %d,\n", batt->discharging_min_c);
	ccprintf("\t.discharging_max_c = %d,\n", batt->discharging_max_c);
	ccprintf("},\n"); /* end of batt_info */
}

static int cc_bcfg(int argc, const char *argv[])
{
	if (argc == 1) {
		batt_conf_dump(get_batt_params());
	} else if (argc == 3) {
		struct batt_conf_header head = {};
		uint8_t size = sizeof(head);
		int index;
		int rv;
		char *e;

		index = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		if (strcasecmp(argv[1], "get") == 0) {
			rv = cbi_get_board_info(index + CBI_TAG_BATTERY_CONFIG,
						(void *)&head, &size);
			if (rv) {
				ccprintf("#%d not found (rv=%d)\n", index, rv);
				return EC_ERROR_UNAVAILABLE;
			}
			ccprintf("struct_ver = 0x%02x\n", head.struct_version);
			ccprintf("manuf = \"%s\"\n", head.manuf_name);
			ccprintf("name = \"%s\"\n", head.device_name);
			ccprintf("size = %u (expect %u)\n", size, sizeof(head));
			batt_conf_dump(&head.config);
		} else if (strcasecmp(argv[1], "set") == 0) {
			const struct board_batt_params *conf =
				get_batt_params();
			head.struct_version = 0;
			strncpy(head.manuf_name, conf->fuel_gauge.manuf_name,
				sizeof(head.manuf_name));
			if (conf->fuel_gauge.device_name)
				strncpy(head.device_name,
					conf->fuel_gauge.device_name,
					sizeof(head.device_name));
			memcpy(&head.config, conf, sizeof(head.config));
			rv = cbi_set_board_info(index + CBI_TAG_BATTERY_CONFIG,
						(void *)&head, size);
			if (rv) {
				ccprintf("Failed to write #%d (rv=%d)\n", index,
					 rv);
				return EC_ERROR_UNKNOWN;
			}
		} else {
			return EC_ERROR_PARAM2;
		}
	} else {
		return EC_ERROR_PARAM_COUNT;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(
	bcfg, cc_bcfg, "[get/set <index>]",
	"\n"
	"Dump effective battery config or config #<index> in CBI.\n"
	"Read from or write to CBI effective battery config.\n");
#endif /* CONFIG_CMD_BATTERY_CONFIG */
