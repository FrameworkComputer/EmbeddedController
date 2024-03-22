/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery driver.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "console.h"
#include "host_command.h"
#include "i2c.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr);
#define CPRINTS(format, args...) cprints(CC_CHARGER, "SBS " format, ##args)

#define BATTERY_NO_RESPONSE_TIMEOUT (1000 * MSEC)

static int fake_state_of_charge = -1;
static int fake_temperature = -1;

#ifdef CONFIG_SMBUS_PEC
static void addr_flags_for_pec(uint16_t *addr_flags)
{
	static int supports_pec = -1;

	if (supports_pec < 0) {
		int spec_info;
		int rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS,
				    SB_SPECIFICATION_INFO, &spec_info);
		/* failed, assuming not supported and try again later */
		if (rv)
			return;

		supports_pec = (BATTERY_SPEC_VERSION(spec_info) ==
				BATTERY_SPEC_VER_1_1_WITH_PEC);
		CPRINTS("battery supports pec: %d", supports_pec);
	}

	if (supports_pec) {
		*addr_flags |= I2C_FLAG_PEC;
	}
}
/* macro to avoid calling a routine when config is disabled */
#define ADDR_FLAGS_FOR_PEC(ADDR_FLAGS) addr_flags_for_pec(ADDR_FLAGS)

#else
/*
 * don't call it at all - this allows compiler optimization to prune
 * PEC address code
 */
#define ADDR_FLAGS_FOR_PEC(ADDR_FLAGS)

#endif

static bool sb_cutoff_or_in_progress(void)
{
	if (IS_ENABLED(CONFIG_BATTERY_CUT_OFF)) {
		/*
		 * Ship mode command need to set continuously, can't be
		 * interfered by another command.
		 */
		if (battery_cutoff_in_progress())
			return true;

		/*
		 * Some batteries would wake up after cut-off if we talk to it.
		 */
		if (battery_is_cut_off())
			return true;
	}
	return false;
}

test_mockable int sb_read(int cmd, int *param)
{
	uint16_t addr_flags = BATTERY_ADDR_FLAGS;

	if (sb_cutoff_or_in_progress())
		return EC_ERROR_ACCESS_DENIED;

	ADDR_FLAGS_FOR_PEC(&addr_flags);
	return i2c_read16(I2C_PORT_BATTERY, addr_flags, cmd, param);
}

test_mockable int sb_write(int cmd, int param)
{
	uint16_t addr_flags = BATTERY_ADDR_FLAGS;

	if (IS_ENABLED(CONFIG_BATTERY_CUT_OFF)) {
		/*
		 * Some batteries would wake up after cut-off if we talk to it.
		 */
		if (battery_is_cut_off())
			return EC_ERROR_ACCESS_DENIED;
	}

	ADDR_FLAGS_FOR_PEC(&addr_flags);

	return i2c_write16(I2C_PORT_BATTERY, addr_flags, cmd, param);
}

int sb_read_string(int offset, uint8_t *data, int len)
{
	uint16_t addr_flags = BATTERY_ADDR_FLAGS;

	if (sb_cutoff_or_in_progress())
		return EC_ERROR_ACCESS_DENIED;

	ADDR_FLAGS_FOR_PEC(&addr_flags);

	return i2c_read_string(I2C_PORT_BATTERY, addr_flags, offset, data, len);
}

int sb_read_sized_block(int offset, uint8_t *data, int len)
{
	uint16_t addr_flags = BATTERY_ADDR_FLAGS;
	int read_len = 0;

	if (sb_cutoff_or_in_progress())
		return EC_ERROR_ACCESS_DENIED;

	ADDR_FLAGS_FOR_PEC(&addr_flags);

	return i2c_read_sized_block(I2C_PORT_BATTERY, addr_flags, offset, data,
				    len, &read_len);
}

int sb_read_mfgacc(int cmd, int block, uint8_t *data, int len)
{
	int rv;

	/*
	 * First two bytes returned from read are command sent hence read
	 * doesn't yield anything if the length is less than 3 bytes.
	 */
	if (len < 3)
		return EC_ERROR_INVAL;

	/* Send manufacturer access command */
	rv = sb_write(SB_MANUFACTURER_ACCESS, cmd);
	if (rv)
		return rv;

	/*
	 * Read data on the register block.
	 * First two bytes returned are command sent,
	 * rest are actual data LSB to MSB.
	 */
	rv = sb_read_sized_block(block, data, len);
	if (rv)
		return rv;
	if ((data[0] | data[1] << 8) != cmd)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

int sb_read_mfgacc_block(int cmd, int block, uint8_t *data, int len)
{
	int rv;

	uint8_t operation_status[3] = {
		0x02,
		cmd & 0xFF,
		cmd >> 8,
	};

	/*
	 * First two bytes returned from read are command sent hence read
	 * doesn't yield anything if the length is less than 3 bytes.
	 */
	if (len < 3)
		return EC_ERROR_INVAL;

	/* Send manufacturer access command by the SMB block protocol */
	rv = sb_write_block(block, operation_status, sizeof(operation_status));
	if (rv)
		return rv;

	/*
	 * Read data on the register block.
	 * First two bytes returned are command sent,
	 * rest are actual data LSB to MSB.
	 */
	rv = sb_read_sized_block(block, data, len);
	if (rv)
		return rv;
	if ((data[0] | data[1] << 8) != cmd)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

int sb_write_block(int reg, const uint8_t *val, int len)
{
	uint16_t addr_flags = BATTERY_ADDR_FLAGS;

#ifdef CONFIG_BATTERY_CUT_OFF
	/*
	 * Some batteries would wake up after cut-off if we talk to it.
	 */
	if (battery_is_cut_off())
		return EC_ERROR_ACCESS_DENIED;
#endif

	ADDR_FLAGS_FOR_PEC(&addr_flags);

	/* TODO: implement smbus_write_block. */
	return i2c_write_block(I2C_PORT_BATTERY, addr_flags, reg, val, len);
}

int battery_get_mode(int *mode)
{
	return sb_read(SB_BATTERY_MODE, mode);
}

/**
 * Force battery to mAh mode (instead of 10mW mode) for reporting capacity.
 *
 * @return non-zero if error.
 */

static int battery_force_mah_mode(void)
{
	int val, rv;
	rv = battery_get_mode(&val);
	if (rv)
		return rv;

	if (val & MODE_CAPACITY)
		rv = sb_write(SB_BATTERY_MODE, val & ~MODE_CAPACITY);

	return rv;
}

int battery_state_of_charge_abs(int *percent)
{
	return sb_read(SB_ABSOLUTE_STATE_OF_CHARGE, percent);
}

int battery_remaining_capacity(int *capacity)
{
	int rv = battery_force_mah_mode();
	if (rv)
		return rv;

	return sb_read(SB_REMAINING_CAPACITY, capacity);
}

int battery_full_charge_capacity(int *capacity)
{
	int rv = battery_force_mah_mode();
	if (rv)
		return rv;

	return sb_read(SB_FULL_CHARGE_CAPACITY, capacity);
}

int battery_time_to_empty(int *minutes)
{
	return sb_read(SB_AVERAGE_TIME_TO_EMPTY, minutes);
}

int battery_run_time_to_empty(int *minutes)
{
	return sb_read(SB_RUN_TIME_TO_EMPTY, minutes);
}

int battery_time_to_full(int *minutes)
{
	return sb_read(SB_AVERAGE_TIME_TO_FULL, minutes);
}

/* Read battery status */
int battery_status(int *status)
{
	return sb_read(SB_BATTERY_STATUS, status);
}

/* Battery charge cycle count */
int battery_cycle_count(int *count)
{
	return sb_read(SB_CYCLE_COUNT, count);
}

int battery_design_capacity(int *capacity)
{
	int rv = battery_force_mah_mode();
	if (rv)
		return rv;

	return sb_read(SB_DESIGN_CAPACITY, capacity);
}

/* Designed battery output voltage
 * unit: mV
 */
int battery_design_voltage(int *voltage)
{
	return sb_read(SB_DESIGN_VOLTAGE, voltage);
}

/* Read serial number */
int battery_serial_number(int *serial)
{
	return sb_read(SB_SERIAL_NUMBER, serial);
}

test_mockable int battery_time_at_rate(int rate, int *minutes)
{
	int rv;
	int ok, time;
	int loop, cmd, output_sign;

	if (rate == 0) {
		*minutes = 0;
		return EC_ERROR_INVAL;
	}

	rv = sb_write(SB_AT_RATE, rate);
	if (rv)
		return rv;
	loop = 5;
	while (loop--) {
		rv = sb_read(SB_AT_RATE_OK, &ok);
		if (rv)
			return rv;
		if (ok) {
			if (rate > 0) {
				cmd = SB_AT_RATE_TIME_TO_FULL;
				output_sign = -1;
			} else {
				cmd = SB_AT_RATE_TIME_TO_EMPTY;
				output_sign = 1;
			}
			rv = sb_read(cmd, &time);
			if (rv)
				return rv;

			*minutes = (time == 0xffff) ? 0 : output_sign * time;
			return EC_SUCCESS;
		} else {
			/* wait 10ms for AT_RATE_OK */
			msleep(10);
		}
	}
	return EC_ERROR_TIMEOUT;
}

test_mockable int battery_manufacture_date(int *year, int *month, int *day)
{
	int rv;
	int ymd;

	rv = sb_read(SB_MANUFACTURE_DATE, &ymd);
	if (rv)
		return rv;

	/* battery date format:
	 * ymd = day + month * 32 + (year - 1980) * 512
	 */
	*year = ((ymd & MANUFACTURE_DATE_YEAR_MASK) >>
		 MANUFACTURE_DATE_YEAR_SHIFT) +
		MANUFACTURE_DATE_YEAR_OFFSET;
	*month = (ymd & MANUFACTURE_DATE_MONTH_MASK) >>
		 MANUFACTURE_DATE_MONTH_SHIFT;
	*day = (ymd & MANUFACTURE_DATE_DAY_MASK) >> MANUFACTURE_DATE_DAY_SHIFT;

	return EC_SUCCESS;
}

int get_battery_manufacturer_name(char *dest, int size)
{
	return sb_read_string(SB_MANUFACTURER_NAME, dest, size);
}

/* Read device name */
test_mockable int battery_device_name(char *dest, int size)
{
	return sb_read_string(SB_DEVICE_NAME, dest, size);
}

/* Read battery type/chemistry */
test_mockable int battery_device_chemistry(char *dest, int size)
{
	return sb_read_string(SB_DEVICE_CHEMISTRY, dest, size);
}

int battery_manufacturer_data(char *data, int size)
{
	return sb_read_string(SB_MANUFACTURER_DATA, data, size);
}

int battery_manufacturer_access(int cmd)
{
	return sb_write(SB_MANUFACTURER_ACCESS, cmd);
}

int battery_get_avg_current(void)
{
	int current;

	/* This is a signed 16-bit value. */
	sb_read(SB_AVERAGE_CURRENT, &current);
	return (int16_t)current;
}

#ifdef CONFIG_CMD_PWR_AVG
/*
 * Technically returns only the instantaneous reading, but tests showed that
 * for the majority of charge states above 3% this varies by less than 40mV
 * every minute, so we accept the inaccuracy here.
 */
int battery_get_avg_voltage(void)
{
	int voltage = -EC_ERROR_UNKNOWN;

	sb_read(SB_VOLTAGE, &voltage);
	return voltage;
}
#endif /* CONFIG_CMD_PWR_AVG */

/* TODO(b/266713897): Remove #ifndef */
#ifndef CONFIG_FUEL_GAUGE
static void apply_fake_state_of_charge(struct batt_params *batt)
{
	int full;

	if (fake_state_of_charge < 0)
		return;

	if (batt->flags & BATT_FLAG_BAD_FULL_CAPACITY)
		battery_design_capacity(&full);
	else
		full = batt->full_capacity;

	batt->state_of_charge = fake_state_of_charge;
	batt->remaining_capacity = full * fake_state_of_charge / 100;
	battery_compensate_params(batt);
	batt->flags &= ~BATT_FLAG_BAD_STATE_OF_CHARGE;
	batt->flags &= ~BATT_FLAG_BAD_REMAINING_CAPACITY;
}

static bool battery_want_charge(struct batt_params *batt)
{
	if (batt->flags &
	    (BATT_FLAG_BAD_DESIRED_VOLTAGE | BATT_FLAG_BAD_DESIRED_CURRENT |
	     BATT_FLAG_BAD_STATE_OF_CHARGE))
		return false;

	/*
	 * Charging allowed if both desired voltage and current are nonzero
	 * and battery isn't full (and we read them all correctly).
	 */
	if (batt->desired_voltage && batt->desired_current &&
	    batt->state_of_charge < BATTERY_LEVEL_FULL)
		return true;

	/*
	 * TODO (crosbug.com/p/29467): remove this workaround for dead battery
	 * that requests no voltage/current
	 */
	if (IS_ENABLED(CONFIG_BATTERY_REQUESTS_NIL_WHEN_DEAD)) {
		if (batt->desired_voltage == 0 && batt->desired_current == 0 &&
		    batt->state_of_charge == 0)
			return true;
	}

	return false;
}

void battery_get_params(struct batt_params *batt)
{
	struct batt_params batt_new;
	int v;

	/*
	 * Start with a copy so that only valid fields will be updated. Note
	 * sb_read doesn't change the value if I2C fails. So, the current value
	 * will be preserved.
	 */
	memcpy(&batt_new, batt, sizeof(*batt));
	batt_new.flags &= ~BATT_FLAG_VOLATILE;

	if (sb_read(SB_TEMPERATURE, &batt_new.temperature) &&
	    fake_temperature < 0)
		batt_new.flags |= BATT_FLAG_BAD_TEMPERATURE;

	/* If temperature is faked, override with faked data */
	if (fake_temperature >= 0)
		batt_new.temperature = fake_temperature;

	if (sb_read(SB_RELATIVE_STATE_OF_CHARGE, &batt_new.state_of_charge) &&
	    fake_state_of_charge < 0)
		batt_new.flags |= BATT_FLAG_BAD_STATE_OF_CHARGE;

	if (sb_read(SB_VOLTAGE, &batt_new.voltage))
		batt_new.flags |= BATT_FLAG_BAD_VOLTAGE;

	/* This is a signed 16-bit value. */
	if (sb_read(SB_CURRENT, &v))
		batt_new.flags |= BATT_FLAG_BAD_CURRENT;
	else
		batt_new.current = (int16_t)v;

	if (sb_read(SB_AVERAGE_CURRENT, &v))
		batt_new.flags |= BATT_FLAG_BAD_AVERAGE_CURRENT;

	if (sb_read(SB_CHARGING_VOLTAGE, &batt_new.desired_voltage))
		batt_new.flags |= BATT_FLAG_BAD_DESIRED_VOLTAGE;

	if (sb_read(SB_CHARGING_CURRENT, &batt_new.desired_current))
		batt_new.flags |= BATT_FLAG_BAD_DESIRED_CURRENT;

	if (battery_remaining_capacity(&batt_new.remaining_capacity))
		batt_new.flags |= BATT_FLAG_BAD_REMAINING_CAPACITY;

	if (battery_full_charge_capacity(&batt_new.full_capacity))
		batt_new.flags |= BATT_FLAG_BAD_FULL_CAPACITY;

	if (battery_status(&batt_new.status))
		batt_new.flags |= BATT_FLAG_BAD_STATUS;

	/* If any of those reads worked, the battery is responsive */
	if ((batt_new.flags & BATT_FLAG_BAD_ANY) != BATT_FLAG_BAD_ANY)
		batt_new.flags |= BATT_FLAG_RESPONSIVE;

#ifdef CONFIG_BATTERY_MEASURE_IMBALANCE
	if (battery_imbalance_mv() > CONFIG_BATTERY_MAX_IMBALANCE_MV)
		batt_new.flags |= BATT_FLAG_IMBALANCED_CELL;
#endif

#if defined(CONFIG_BATTERY_PRESENT_CUSTOM) || \
	defined(CONFIG_BATTERY_PRESENT_GPIO)
	/* Hardware can tell us for certain */
	batt_new.is_present = battery_is_present();
#else
	/* No hardware test, so we only know it's there if it responds */
	if (batt_new.flags & BATT_FLAG_RESPONSIVE)
		batt_new.is_present = BP_YES;
	else
		batt_new.is_present = BP_NOT_SURE;
#endif

	if (battery_want_charge(&batt_new))
		batt_new.flags |= BATT_FLAG_WANT_CHARGE;
	else
		/* Force both to zero */
		batt_new.desired_voltage = batt_new.desired_current = 0;

#ifdef HAS_TASK_HOSTCMD
	/* if there is no host, we don't care about compensation */
	battery_compensate_params(&batt_new);
	board_battery_compensate_params(&batt_new);
#endif

	if (IS_ENABLED(CONFIG_CMD_BATTFAKE))
		apply_fake_state_of_charge(&batt_new);

	/* Update visible battery parameters */
	memcpy(batt, &batt_new, sizeof(*batt));
}
#endif /* !CONFIG_FUEL_GAUGE */

/* Wait until battery is totally stable */
int battery_wait_for_stable(void)
{
	int status;
	uint64_t wait_timeout = get_time().val + BATTERY_NO_RESPONSE_TIMEOUT;

	CPRINTS("Wait for battery stabilized during %d",
		BATTERY_NO_RESPONSE_TIMEOUT);
	while (get_time().val < wait_timeout) {
		/* Starting pinging battery */
		if (battery_status(&status) != EC_SUCCESS) {
			msleep(25); /* clock stretching could hold 25ms */
			continue;
		}

#ifdef CONFIG_BATTERY_STBL_STAT
		if (((status & CONFIG_BATT_ALARM_MASK1) ==
		     CONFIG_BATT_ALARM_MASK1) ||
		    ((status & CONFIG_BATT_ALARM_MASK2) ==
		     CONFIG_BATT_ALARM_MASK2)) {
			msleep(25);
			continue;
		}
#endif
		/* Battery is stable */
		CPRINTS("battery responded with status %x", status);
		return EC_SUCCESS;
	}
	CPRINTS("battery not responding with status %x", status);
	return EC_ERROR_NOT_POWERED;
}

#if defined(CONFIG_CMD_BATTFAKE)
static int command_battfake(int argc, const char **argv)
{
	char *e;
	int v;

	if (argc == 2) {
		v = strtoi(argv[1], &e, 0);
		if (*e || v < -1 || v > 100)
			return EC_ERROR_PARAM1;

		fake_state_of_charge = v;
	}

	if (fake_state_of_charge >= 0)
		ccprintf("Fake batt %d%%\n", fake_state_of_charge);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battfake, command_battfake,
			"percent (-1 = use real level)",
			"Set fake battery level");

static int command_batttempfake(int argc, const char **argv)
{
	char *e;
	int t;

	if (argc == 2) {
		t = strtoi(argv[1], &e, 0);
		if (*e || t < -1 || t > 5000)
			return EC_ERROR_PARAM1;

		fake_temperature = t;
	}

	if (fake_temperature >= 0)
		ccprintf("Fake batt temperature %d.%d K\n",
			 fake_temperature / 10, fake_temperature % 10);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(
	batttempfake, command_batttempfake,
	"temperature (-1 = use real temperature)",
	"Set fake battery temperature in deciKelvin (2731 = 273.1 K = 0 deg C)");
#endif

#ifdef CONFIG_CMD_BATT_MFG_ACCESS
static int command_batt_mfg_access_read(int argc, const char **argv)
{
	char *e;
	uint8_t data[32];
	int cmd, block, len = 6;
	int rv;

	if (argc < 3 || argc > 4)
		return EC_ERROR_PARAM_COUNT;

	cmd = strtoi(argv[1], &e, 0);
	if (*e || cmd < 0)
		return EC_ERROR_PARAM1;

	block = strtoi(argv[2], &e, 0);
	if (*e || block < 0)
		return EC_ERROR_PARAM2;

	if (argc > 3) {
		len = strtoi(argv[3], &e, 0);
		len += 2;
		if (*e || len < 3 || len > sizeof(data))
			return EC_ERROR_PARAM3;
	}

	rv = sb_read_mfgacc(cmd, block, data, len);
	if (rv)
		return rv;

	ccprintf("data[MSB->LSB]=0x");
	do {
		len--;
		ccprintf("%02x ", data[len]);
	} while (len > 2);
	ccprintf("\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battmfgacc, command_batt_mfg_access_read,
			"cmd block | len",
			"Read battery manufacture access data");
#endif /* CONFIG_CMD_BATT_MFG_ACCESS */

#ifdef CONFIG_CMD_I2C_STRESS_TEST_BATTERY
test_mockable int sb_i2c_test_read(int cmd, int *param)
{
	char chemistry[sizeof(CONFIG_BATTERY_DEVICE_CHEMISTRY) + 1];
	int rv;

	if (cmd == SB_DEVICE_CHEMISTRY) {
		rv = battery_device_chemistry(
			chemistry, sizeof(CONFIG_BATTERY_DEVICE_CHEMISTRY));
		if (rv)
			return rv;
		if (strcasecmp(chemistry, CONFIG_BATTERY_DEVICE_CHEMISTRY))
			return EC_ERROR_UNKNOWN;

		*param = EC_SUCCESS;
		return EC_SUCCESS;
	}

	return sb_read(cmd, param);
}

struct i2c_stress_test_dev battery_i2c_stress_test_dev = {
	.reg_info = {
		.read_reg = SB_DEVICE_CHEMISTRY,
		.read_val = EC_SUCCESS,
		.write_reg = SB_AT_RATE,
	},
	.i2c_read_dev = &sb_i2c_test_read,
	.i2c_write_dev = &sb_write,
};
#endif /* CONFIG_CMD_I2C_STRESS_TEST_BATTERY */
