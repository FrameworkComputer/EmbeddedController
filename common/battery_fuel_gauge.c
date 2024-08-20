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
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)
#define BCFGPRT(format, args...) cprints(CC_CHARGER, "BCFG " format, ##args)

/*
 * Pointer to an active config. It's battery_conf_cache if a config is found
 * in CBI or board_battery_info[x] if a config is found in FW.
 */
test_export_static const struct batt_conf_embed *battery_conf;

static char batt_manuf_name[SBS_MAX_STR_OBJ_SIZE];
static char batt_device_name[SBS_MAX_STR_OBJ_SIZE];

/* Copies of config and strings of a matching battery found in CBI. */
static struct batt_conf_embed battery_conf_cache = {
	.manuf_name = batt_manuf_name,
	.device_name = batt_device_name,
};

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
	const struct batt_conf_embed *const conf = &board_battery_info[index];
	int len = 0;

	/* check for valid index */
	if (index >= BATTERY_TYPE_COUNT)
		return false;

	/* manufacturer name mismatch */
	if (strcasecmp(manuf_name, conf->manuf_name))
		return false;

	/* device name is specified in table */
	if (conf->device_name != NULL) {
		/* If config specifies device name, battery must have one. */
		if (batt_device_name[0] == '\0')
			return false;

		len = strlen(conf->device_name);

		/* device name mismatch */
		if (strncasecmp(batt_device_name, conf->device_name, len))
			return false;
	}

	CPRINTS("found batt:%s", conf->manuf_name);
	return true;
}

/**
 * Find a config for the battery from board_battery_info[].
 *
 * This is supposed to be called only if batt_manuf_name is populated.
 */
static int get_battery_type(void)
{
	int i;
	enum battery_type battery_type = BATTERY_TYPE_COUNT;

	for (i = 0; i < BATTERY_TYPE_COUNT; i++) {
		if (authenticate_battery_type(i, batt_manuf_name)) {
			battery_type = i;
			break;
		}
	}

	return battery_type;
}

__overridable int board_get_default_battery_type(void)
{
	return DEFAULT_BATTERY_TYPE;
}

static int bcfg_search_in_cbi(struct batt_conf_embed *batt)
{
	int tag = CBI_TAG_BATTERY_CONFIG;

	while (1) {
		uint8_t buf[BATT_CONF_MAX_SIZE];
		uint8_t size = sizeof(buf);
		uint16_t expected;
		uint8_t *p = buf;
		struct batt_conf_header *head;
		char *m, *d;
		int rv;

		rv = cbi_get_board_info(tag, buf, &size);
		if (rv) {
			BCFGPRT("No more configs (%d)", rv);
			return rv;
		}
		BCFGPRT("Checking config #%d (size=%d)...",
			tag - CBI_TAG_BATTERY_CONFIG, size);
		tag++;

		head = (struct batt_conf_header *)buf;
		if (head->struct_version > EC_BATTERY_CONFIG_STRUCT_VERSION) {
			BCFGPRT("Version mismatch: 0x%x", head->struct_version);
			continue;
		}

		/* Check total size. */
		expected = sizeof(*head) + head->manuf_name_size +
			   head->device_name_size + sizeof(batt->config);
		if (size != expected) {
			BCFGPRT("Size mismatch: %u != %u", size, expected);
			continue;
		}

		/* Check manufacturer name. */
		p += sizeof(*head);
		m = (char *)p;
		/* Check length explicitly because 'm' isn't null terminated. */
		if (head->manuf_name_size != strlen(batt_manuf_name) ||
		    strncasecmp(m, batt_manuf_name, strlen(batt_manuf_name))) {
			BCFGPRT("Manuf mismatch: %.*s", head->manuf_name_size,
				m);
			continue;
		}

		/* (Optional) Check device name. */
		p += head->manuf_name_size;
		d = (char *)p;
		/*
		 * If config has no device name, it's a wild card.
		 *
		 * We do partial match. As long as the battery's device name
		 * starts with the config's device name, it's considered as a
		 * match. The characters in the battery's device name after that
		 * are ignored.
		 */
		if (head->device_name_size != 0 &&
		    strncasecmp(d, batt_device_name, head->device_name_size)) {
			BCFGPRT("Device name mismatch: %.*s",
				head->device_name_size, d);
			continue;
		}

		BCFGPRT("Matched");

		/* Save config in cache. */
		memset(batt->manuf_name, 0, SBS_MAX_STR_OBJ_SIZE);
		memset(batt->device_name, 0, SBS_MAX_STR_OBJ_SIZE);
		strncpy(batt->manuf_name, m, head->manuf_name_size);
		strncpy(batt->device_name, d, head->device_name_size);
		p += head->device_name_size;
		memcpy(&batt->config, p, sizeof(batt->config));

		return EC_SUCCESS;
	}
}

void init_battery_type(void)
{
	int type;
	int dflt = board_get_default_battery_type();
	int ret;

	ret = battery_manufacturer_name(batt_manuf_name,
					sizeof(batt_manuf_name));

	for (int i = 0; i < CONFIG_BATTERY_INIT_TYPE_RETRY_COUNT; i++) {
		if (ret == EC_SUCCESS) {
			break;
		}
		CPRINTS("Manuf name not found, wait 100ms then retry (attempt %d)",
			i);
		crec_msleep(100);
		ret = battery_manufacturer_name(batt_manuf_name,
						sizeof(batt_manuf_name));
	}

	if (ret) {
		BCFGPRT("Manuf name not found");
		battery_conf = &board_battery_info[dflt];
		return;
	}

	/* Don't carry over any previous name (in case i2c fail). */
	memset(batt_device_name, 0, sizeof(batt_device_name));
	ret = battery_device_name(batt_device_name, sizeof(batt_device_name));

	for (int i = 0; i < CONFIG_BATTERY_INIT_TYPE_RETRY_COUNT; i++) {
		if (ret == EC_SUCCESS) {
			break;
		}
		CPRINTS("Device name not found, wait 100ms then retry (attempt %d)",
			i);
		crec_msleep(100);
		ret = battery_device_name(batt_device_name,
					  sizeof(batt_device_name));
	}

	if (ret) {
		BCFGPRT("Device name not found");
		memset(batt_device_name, 0, sizeof(batt_device_name));
		/* Battery name is optional. Proceed. */
	}

	BCFGPRT("Battery says %s,%s", batt_manuf_name, batt_device_name);

	if (IS_ENABLED(CONFIG_BATTERY_CONFIG_IN_CBI) &&
	    board_batt_conf_enabled()) {
		BCFGPRT("Searching in CBI");
		if (bcfg_search_in_cbi(&battery_conf_cache) == EC_SUCCESS) {
			battery_conf = &battery_conf_cache;
			return;
		}
	}

	/* Battery config isn't in CBI. */
	BCFGPRT("Searching in FW");

	type = get_battery_type();
	if (type == BATTERY_TYPE_COUNT) {
		BCFGPRT("Config not found. Fall back to config #%d", dflt);
		type = dflt;
	} else {
		BCFGPRT("Found config #%d", type);
	}

	battery_conf = &board_battery_info[type];
}
DECLARE_HOOK(HOOK_INIT, init_battery_type, HOOK_PRIO_BATTERY_INIT);

const struct batt_conf_embed *get_batt_conf(void)
{
	return battery_conf;
}

test_export_static const struct board_batt_params *get_batt_params(void)
{
	return get_batt_conf() ? &get_batt_conf()->config : NULL;
}

const struct battery_info *battery_get_info(void)
{
	return &get_batt_params()->batt_info;
}

__overridable bool board_batt_conf_enabled(void)
{
	return true;
}

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

static enum ec_status hc_battery_config(struct host_cmd_handler_args *args)
{
	struct batt_conf_header *r = args->response;
	const struct batt_conf_embed *batt = get_batt_conf();
	uint8_t *p = (void *)r;

	r->struct_version = EC_BATTERY_CONFIG_STRUCT_VERSION;
	r->manuf_name_size = strlen(batt->manuf_name);
	r->device_name_size = batt->device_name ? strlen(batt->device_name) : 0;
	p += sizeof(*r);
	memcpy(p, batt->manuf_name, r->manuf_name_size);
	p += r->manuf_name_size;
	memcpy(p, batt->device_name, r->device_name_size);
	p += r->device_name_size;
	memcpy(p, &batt->config, sizeof(batt->config));

	args->response_size = sizeof(*r) + r->manuf_name_size +
			      r->device_name_size + sizeof(batt->config);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_CONFIG, hc_battery_config, EC_VER_MASK(0));

#ifdef CONFIG_CMD_BATTERY_CONFIG

void batt_conf_dump(const struct batt_conf_export *conf)
{
	const struct board_batt_params *info = &conf->config;
	const struct fuel_gauge_info *fg = &info->fuel_gauge;
	const struct ship_mode_info *ship = &info->fuel_gauge.ship_mode;
	const struct sleep_mode_info *sleep = &info->fuel_gauge.sleep_mode;
	const struct fet_info *fet = &info->fuel_gauge.fet;
	const struct battery_info *batt = &info->batt_info;

	ccprintf(".manuf_name = \"%s\",\n", conf->manuf_name);
	ccprintf(".device_name= \"%s\",\n", conf->device_name);
	ccprintf(".fuel_gauge = {\n");
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
