/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery driver for MAX17055.
 */

#include "battery.h"
#include "builtin/assert.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "max17055.h"
#include "printf.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

/*
 * For max17055 to finish battery presence detection, this is the minimal time
 * we have to wait since the last POR. LSB = 175ms.
 */
#define RELIABLE_BATT_DETECT_TIME 0x10

/*
 * Convert the register values to the units that match
 * smart battery protocol.
 */

/* Voltage reg value to mV */
#define VOLTAGE_CONV(REG) ((REG * 5) >> 6)
/* Current reg value to mA */
#define CURRENT_CONV(REG) (((REG * 25) >> 4) / BATTERY_MAX17055_RSENSE)
/* Capacity reg value to mAh */
#define CAPACITY_CONV(REG) (REG * 5 / BATTERY_MAX17055_RSENSE)
/* Time reg value to minute */
#define TIME_CONV(REG) ((REG * 3) >> 5)
/* Temperature reg value to 0.1K */
#define TEMPERATURE_CONV(REG) (((REG * 10) >> 8) + 2731)
/* Percentage reg value to 1% */
#define PERCENTAGE_CONV(REG) (REG >> 8)
/* Cycle count reg value (LSB = 1%) to absolute count (100%) */
#define CYCLE_COUNT_CONV(REG) ((REG * 5) >> 9)

/* Useful macros */
#define MAX17055_READ_DEBUG(offset, ptr_reg)                             \
	do {                                                             \
		if (max17055_read(offset, ptr_reg)) {                    \
			CPRINTS("%s: failed to read reg %02x", __func__, \
				offset);                                 \
			return;                                          \
		}                                                        \
	} while (0)
#define MAX17055_WRITE_DEBUG(offset, reg)                                \
	do {                                                             \
		if (max17055_write(offset, reg)) {                       \
			CPRINTS("%s: failed to read reg %02x", __func__, \
				offset);                                 \
			return;                                          \
		}                                                        \
	} while (0)

static int fake_state_of_charge = -1;

static int max17055_read(int offset, int *data)
{
	return i2c_read16(I2C_PORT_BATTERY, MAX17055_ADDR_FLAGS, offset, data);
}

static int max17055_write(int offset, int data)
{
	return i2c_write16(I2C_PORT_BATTERY, MAX17055_ADDR_FLAGS, offset, data);
}

/* Return 1 if the device id is correct. */
static int max17055_probe(void)
{
	int dev_id;

	if (max17055_read(REG_DEVICE_NAME, &dev_id))
		return 0;
	if (dev_id == MAX17055_DEVICE_ID)
		return 1;
	return 0;
}

int battery_device_name(char *device_name, int buf_size)
{
	int dev_id;
	int rv;

	rv = max17055_read(REG_DEVICE_NAME, &dev_id);
	if (rv != EC_SUCCESS)
		return rv;

	if (snprintf(device_name, buf_size, "0x%04x", dev_id) <= 0)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

int battery_state_of_charge_abs(int *percent)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_remaining_capacity(int *capacity)
{
	int rv;
	int reg;

	rv = max17055_read(REG_REMAINING_CAPACITY, &reg);
	if (!rv)
		*capacity = CAPACITY_CONV(reg);
	return rv;
}

int battery_full_charge_capacity(int *capacity)
{
	int rv;
	int reg;

	rv = max17055_read(REG_FULL_CHARGE_CAPACITY, &reg);
	if (!rv)
		*capacity = CAPACITY_CONV(reg);
	return rv;
}

int battery_time_to_empty(int *minutes)
{
	int rv;
	int reg;

	rv = max17055_read(REG_TIME_TO_EMPTY, &reg);
	if (!rv)
		*minutes = TIME_CONV(reg);
	return rv;
}

int battery_time_to_full(int *minutes)
{
	int rv;
	int reg;

	rv = max17055_read(REG_TIME_TO_FULL, &reg);
	if (!rv)
		*minutes = TIME_CONV(reg);
	return rv;
}

int battery_cycle_count(int *count)
{
	int rv;
	int reg;

	rv = max17055_read(REG_CYCLE_COUNT, &reg);
	if (!rv)
		*count = CYCLE_COUNT_CONV(reg);
	return rv;
}

int battery_design_capacity(int *capacity)
{
	int rv;
	int reg;

	rv = max17055_read(REG_DESIGN_CAPACITY, &reg);
	if (!rv)
		*capacity = CAPACITY_CONV(reg);
	return rv;
}

int battery_time_at_rate(int rate, int *minutes)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_device_chemistry(char *dest, int size)
{
	strzcpy(dest, "<unkn>", size);

	return EC_SUCCESS;
}

int battery_serial_number(int *serial)
{
	/* TODO(philipchen): Implement this function. */
	*serial = 0xFFFFFFFF;
	return EC_SUCCESS;
}

int battery_manufacture_date(int *year, int *month, int *day)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_design_voltage(int *voltage)
{
	*voltage = battery_get_info()->voltage_normal;

	return EC_SUCCESS;
}

int battery_get_mode(int *mode)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_status(int *status)
{
	int rv;
	int reg;

	*status = 0;

	rv = max17055_read(REG_FSTAT, &reg);
	if (rv)
		return rv;
	if (reg & FSTAT_FQ)
		*status |= BATTERY_FULLY_CHARGED;

	rv = max17055_read(REG_AVERAGE_CURRENT, &reg);
	if (rv)
		return rv;
	if (reg >> 15)
		*status |= BATTERY_DISCHARGING;

	return EC_SUCCESS;
}

enum battery_present battery_is_present(void)
{
	int reg = 0;
	static uint8_t batt_pres_sure;

	if (max17055_read(REG_STATUS, &reg))
		return BP_NOT_SURE;

	if (reg & STATUS_BST)
		return BP_NO;

	if (!batt_pres_sure) {
		/*
		 * The battery detection result is not reliable within
		 * ~2.8 secs since POR.
		 */
		if (!max17055_read(REG_TIMERH, &reg)) {
			/*
			 * The LSB of TIMERH reg is 3.2 hrs. If the reg has a
			 * nonzero value, battery detection must have been
			 * settled.
			 */
			if (reg) {
				batt_pres_sure = 1;
				return BP_YES;
			}
			if (!max17055_read(REG_TIMER, &reg) &&
			    ((uint32_t)reg > RELIABLE_BATT_DETECT_TIME)) {
				batt_pres_sure = 1;
				return BP_YES;
			}
		}
		return BP_NOT_SURE;
	}
	return BP_YES;
}

void battery_get_params(struct batt_params *batt)
{
	int reg = 0;
	struct batt_params batt_new = { 0 };

	/*
	 * Assuming the battery is responsive as long as
	 * max17055 finds battery is present.
	 */
	batt_new.is_present = battery_is_present();

	if (batt_new.is_present == BP_YES)
		batt_new.flags |= BATT_FLAG_RESPONSIVE;
	else if (batt_new.is_present == BP_NO)
		/* Battery is not present, gauge won't report useful info. */
		goto batt_out;

	if (max17055_read(REG_TEMPERATURE, &reg))
		batt_new.flags |= BATT_FLAG_BAD_TEMPERATURE;

	batt_new.temperature = TEMPERATURE_CONV((int16_t)reg);

	if (max17055_read(REG_STATE_OF_CHARGE, &reg) &&
	    fake_state_of_charge < 0)
		batt_new.flags |= BATT_FLAG_BAD_STATE_OF_CHARGE;

	batt_new.state_of_charge = fake_state_of_charge >= 0 ?
					   fake_state_of_charge :
					   PERCENTAGE_CONV(reg);

	if (max17055_read(REG_VOLTAGE, &reg))
		batt_new.flags |= BATT_FLAG_BAD_VOLTAGE;

	batt_new.voltage = VOLTAGE_CONV(reg);

	if (max17055_read(REG_AVERAGE_CURRENT, &reg))
		batt_new.flags |= BATT_FLAG_BAD_CURRENT;

	batt_new.current = CURRENT_CONV((int16_t)reg);

	batt_new.desired_voltage = battery_get_info()->voltage_max;
	batt_new.desired_current = BATTERY_DESIRED_CHARGING_CURRENT;

	if (battery_remaining_capacity(&batt_new.remaining_capacity))
		batt_new.flags |= BATT_FLAG_BAD_REMAINING_CAPACITY;

	if (battery_full_charge_capacity(&batt_new.full_capacity))
		batt_new.flags |= BATT_FLAG_BAD_FULL_CAPACITY;

	/*
	 * Charging allowed if both desired voltage and current are nonzero
	 * and battery isn't full (and we read them all correctly).
	 */
	if (!(batt_new.flags & BATT_FLAG_BAD_STATE_OF_CHARGE) &&
	    batt_new.desired_voltage && batt_new.desired_current &&
	    batt_new.state_of_charge < BATTERY_LEVEL_FULL)
		batt_new.flags |= BATT_FLAG_WANT_CHARGE;

	if (battery_status(&batt_new.status))
		batt_new.flags |= BATT_FLAG_BAD_STATUS;

batt_out:
	/* Update visible battery parameters */
	memcpy(batt, &batt_new, sizeof(*batt));
}

#ifdef CONFIG_CMD_PWR_AVG
int battery_get_avg_current(void)
{
	/* TODO(crbug.com/752320) implement this */
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_get_avg_voltage(void)
{
	/* TODO(crbug.com/752320) implement this */
	return -EC_ERROR_UNIMPLEMENTED;
}
#endif /* CONFIG_CMD_PWR_AVG */

/* Wait until battery is totally stable. */
int battery_wait_for_stable(void)
{
	/* TODO(philipchen): Implement this function. */
	return EC_SUCCESS;
}

static int max17055_poll_flag_clear(int regno, int mask, int timeout)
{
	int reg;

	do {
		if (max17055_read(regno, &reg))
			return EC_ERROR_UNKNOWN;

		if (!(mask & reg))
			return EC_SUCCESS;

		crec_msleep(10);
		timeout -= 10;
	} while (timeout > 0);

	return EC_ERROR_TIMEOUT;
}

static int max17055_load_ocv_table(const struct max17055_batt_profile *config)
{
	int i;
	int reg;
	int retries = 3;

	/* Unlock ocv table */
	if (max17055_write(REG_LOCK1, 0x0059) ||
	    max17055_write(REG_LOCK2, 0x00c4))
		return EC_ERROR_UNKNOWN;

	ASSERT(config->ocv_table);

	/* Write ocv data */
	for (i = 0; i < MAX17055_OCV_TABLE_SIZE; i++) {
		if (max17055_write(REG_OCV_TABLE_START + i,
				   config->ocv_table[i]))
			return EC_ERROR_UNKNOWN;
	}

	/* Read and compare ocv data */
	for (i = 0; i < MAX17055_OCV_TABLE_SIZE; i++) {
		if (max17055_read(REG_OCV_TABLE_START + i, &reg) ||
		    reg != config->ocv_table[i])
			return EC_ERROR_UNKNOWN;
	}

	while (--retries) {
		/* Lock ocv table */
		if (max17055_write(REG_LOCK1, 0x0000) ||
		    max17055_write(REG_LOCK2, 0x0000))
			return EC_ERROR_UNKNOWN;

		/*
		 * If the ocv table remains unlocked, the MAX17055 cannot
		 * monitor the capacity of the battery. Therefore, it is very
		 * critical that the ocv table is locked. To verify it is
		 * locked, simply read back the values. However, this time,
		 * all values should be read as 0x0000.
		 */
		for (i = 0; i < MAX17055_OCV_TABLE_SIZE; i++) {
			reg = 0xff;
			if (max17055_read(REG_OCV_TABLE_START + i, &reg))
				return EC_ERROR_UNKNOWN;
			if (reg)
				break;
		}
		if (i == MAX17055_OCV_TABLE_SIZE)
			break;
		crec_msleep(20);
	}
	if (!retries)
		return EC_ERROR_TIMEOUT;

	/*
	 * Delay 180ms is to prepare the environment to load the custom
	 * battery parameters. Otherwise, the initialization operation
	 * has a very small probability of failure.
	 */
	crec_msleep(180);

	return EC_SUCCESS;
}

static int max17055_exit_hibernate(void)
{
	/*
	 * Write REG_COMMAND with 0x90 to force the firmware to stop running.
	 * Write REG_HIBCFG with 0x00 to exit hibernate mode immediately.
	 * Write REG_COMMAND with 0x00 to run the firmware again.
	 */
	if (max17055_write(REG_COMMAND, 0x90) ||
	    max17055_write(REG_HIBCFG, 0x00) ||
	    max17055_write(REG_COMMAND, 0x00))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/* Configured MAX17055 with the battery parameters for full model. */
static int max17055_load_batt_model_full(void)
{
	int reg;
	int hib_cfg;

	const struct max17055_batt_profile *config;

	config = max17055_get_batt_profile();

	/* Store the original HibCFG value. */
	if (max17055_read(REG_HIBCFG, &hib_cfg))
		return EC_ERROR_UNKNOWN;

	/* Force exit from hibernate */
	if (max17055_exit_hibernate())
		return EC_ERROR_UNKNOWN;

	/* Write LearnCFG with LS 7 */
	if (max17055_write(REG_LEARNCFG, config->learn_cfg | 0x0070))
		return EC_ERROR_UNKNOWN;

	/*
	 * Unlock ocv table access, write/compare/verify custom ocv table,
	 * lock ocv table access.
	 */
	if (max17055_load_ocv_table(config))
		return EC_ERROR_UNKNOWN;

	/* Write custom parameters */
	if (max17055_write(REG_DESIGN_CAPACITY, config->design_cap) ||
	    max17055_write(REG_DQACC, config->design_cap >> 4) ||
	    max17055_write(REG_DPACC, 0x0c80) ||
	    max17055_write(REG_CHARGE_TERM_CURRENT, config->ichg_term) ||
	    max17055_write(REG_EMPTY_VOLTAGE, config->v_empty_detect))
		return EC_ERROR_UNKNOWN;

	if (max17055_write(REG_RCOMP0, config->rcomp0) ||
	    max17055_write(REG_TEMPCO, config->tempco) ||
	    max17055_write(REG_QR_TABLE00, config->qr_table00) ||
	    max17055_write(REG_QR_TABLE10, config->qr_table10))
		return EC_ERROR_UNKNOWN;

	/* Update required capacity registers */
	if (max17055_write(REG_REMAINING_CAPACITY, 0x0000) ||
	    max17055_read(REG_VFSOC, &reg))
		return EC_ERROR_UNKNOWN;

	if (max17055_write(REG_VFSOC0, reg) ||
	    max17055_write(REG_FULL_CHARGE_CAPACITY, config->design_cap) ||
	    max17055_write(REG_FULLCAPNOM, config->design_cap))
		return EC_ERROR_UNKNOWN;

	/* Prepare to Load Model */
	if (max17055_write(REG_REMAINING_CAPACITY, 0x0000) ||
	    max17055_write(REG_MIXCAP, config->design_cap))
		return EC_ERROR_UNKNOWN;

	/* Initiate model loading */
	if (max17055_read(REG_CONFIG2, &reg) ||
	    max17055_write(REG_CONFIG2, reg | CONFIG2_LDMDL))
		return EC_ERROR_UNKNOWN;

	if (max17055_poll_flag_clear(REG_CONFIG2, CONFIG2_LDMDL, 500))
		return EC_ERROR_UNKNOWN;

	/* Write LearnCFG with LS 0 */
	if (max17055_write(REG_LEARNCFG, config->learn_cfg & 0xff8f) ||
	    max17055_write(REG_QR_TABLE20, config->qr_table20) ||
	    max17055_write(REG_QR_TABLE30, config->qr_table30))
		return EC_ERROR_UNKNOWN;

	/* Restore the original HibCFG value. */
	if (max17055_write(REG_HIBCFG, hib_cfg))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/*
 * Configured MAX17055 with the battery parameters for short model or ez model
 */
static int max17055_load_batt_model_short_or_ez(void)
{
	int hib_cfg;
	int dqacc;
	int dpacc;

	const struct max17055_batt_profile *config;

	config = max17055_get_batt_profile();

	if (config->is_ez_config) {
		dqacc = config->design_cap / 32;
		/* Choose the model for charge voltage > 4.275V. */
		dpacc = dqacc * 51200 / config->design_cap;
	} else {
		dqacc = config->design_cap / 16;
		dpacc = config->dpacc;
	}

	if (max17055_write(REG_DESIGN_CAPACITY, config->design_cap) ||
	    max17055_write(REG_DQACC, dqacc) ||
	    max17055_write(REG_CHARGE_TERM_CURRENT, config->ichg_term) ||
	    max17055_write(REG_EMPTY_VOLTAGE, config->v_empty_detect))
		return EC_ERROR_UNKNOWN;

	if (!config->is_ez_config) {
		if (max17055_write(REG_LEARNCFG, config->learn_cfg))
			return EC_ERROR_UNKNOWN;
	}

	/* Store the original HibCFG value. */
	if (max17055_read(REG_HIBCFG, &hib_cfg))
		return EC_ERROR_UNKNOWN;

	/* Force exit from hibernate */
	if (max17055_exit_hibernate())
		return EC_ERROR_UNKNOWN;

	if (max17055_write(REG_DPACC, dpacc) ||
	    max17055_write(REG_MODELCFG, (MODELCFG_REFRESH | MODELCFG_VCHG)))
		return EC_ERROR_UNKNOWN;

	/* Delay up to 500 ms until MODELCFG.REFRESH bit == 0. */
	if (max17055_poll_flag_clear(REG_MODELCFG, MODELCFG_REFRESH, 500))
		return EC_ERROR_UNKNOWN;

	if (!config->is_ez_config) {
		if (max17055_write(REG_RCOMP0, config->rcomp0) ||
		    max17055_write(REG_TEMPCO, config->tempco) ||
		    max17055_write(REG_QR_TABLE00, config->qr_table00) ||
		    max17055_write(REG_QR_TABLE10, config->qr_table10) ||
		    max17055_write(REG_QR_TABLE20, config->qr_table20) ||
		    max17055_write(REG_QR_TABLE30, config->qr_table30))
			return EC_ERROR_UNKNOWN;
	}

	/* Restore the original HibCFG value. */
	if (max17055_write(REG_HIBCFG, hib_cfg))
		return EC_ERROR_UNKNOWN;
	return EC_SUCCESS;
}

static int max17055_load_batt_model(void)
{
	if (IS_ENABLED(CONFIG_BATTERY_MAX17055_FULL_MODEL))
		return max17055_load_batt_model_full();
	else
		return max17055_load_batt_model_short_or_ez();
}

static void max17055_init(void)
{
	int reg;
	int retries = 80;
#ifdef CONFIG_BATTERY_MAX17055_ALERT
	const struct max17055_alert_profile *alert_profile =
		max17055_get_alert_profile();
#endif

	if (!max17055_probe()) {
		CPRINTS("Wrong max17055 id!");
		return;
	}

	/*
	 * Set CONFIG.TSEL to measure temperature using external thermistor.
	 * Set it as early as possible because max17055 takes up to 1000ms to
	 * have the first reliable external temperature reading.
	 */
	MAX17055_READ_DEBUG(REG_CONFIG, &reg);
	MAX17055_WRITE_DEBUG(REG_CONFIG, (reg | CONF_TSEL));

	MAX17055_READ_DEBUG(REG_STATUS, &reg);

	/* Check for POR */
	if (STATUS_POR & reg) {
		/* Delay up to 800 ms until FSTAT.DNR bit == 0. */
		while (--retries) {
			MAX17055_READ_DEBUG(REG_FSTAT, &reg);
			if (!(FSTAT_DNR & reg))
				break;
			crec_msleep(10);
		}
		if (!retries) {
			CPRINTS("%s: timeout waiting for FSTAT.DNR cleared",
				__func__);
			return;
		}

		if (max17055_load_batt_model()) {
			CPRINTS("max17055 configuration failed!");
			return;
		}

		/* Clear POR bit */
		MAX17055_READ_DEBUG(REG_STATUS, &reg);
		MAX17055_WRITE_DEBUG(REG_STATUS, (reg & ~STATUS_POR));
	} else {
		const struct max17055_batt_profile *config;

		config = max17055_get_batt_profile();
		MAX17055_READ_DEBUG(REG_DESIGN_CAPACITY, &reg);

		/*
		 * Reload the battery model if the current running one
		 * is wrong.
		 */
		if (config->design_cap != reg) {
			CPRINTS("max17055 reconfig...");
			if (max17055_load_batt_model()) {
				CPRINTS("max17055 configuration failed!");
				return;
			}
		}
	}

#ifdef CONFIG_BATTERY_MAX17055_ALERT
	/* Set voltage alert range */
	MAX17055_WRITE_DEBUG(REG_VALRTTH, alert_profile->v_alert_mxmn);
	/* Set temperature alert range */
	MAX17055_WRITE_DEBUG(REG_TALRTTH, alert_profile->t_alert_mxmn);
	/* Set state-of-charge alert range */
	MAX17055_WRITE_DEBUG(REG_SALRTTH, alert_profile->s_alert_mxmn);
	/* Set current alert range */
	MAX17055_WRITE_DEBUG(REG_IALRTTH, alert_profile->i_alert_mxmn);

	/* Disable all sticky bits; enable alert AEN */
	MAX17055_READ_DEBUG(REG_CONFIG, &reg);
	MAX17055_WRITE_DEBUG(REG_CONFIG, (reg & ~CONF_ALL_STICKY) | CONF_AEN);

	/* Clear alerts */
	MAX17055_READ_DEBUG(REG_STATUS, &reg);
	MAX17055_WRITE_DEBUG(REG_STATUS, reg & ~STATUS_ALL_ALRT);
#endif

	CPRINTS("max17055 configuration succeeded!");
}
DECLARE_HOOK(HOOK_INIT, max17055_init, HOOK_PRIO_DEFAULT);
