/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery driver for MAX17055.
 */

#include "battery.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "max17055.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

/*
 * For max17055 to finish battery presence detection, this is the minimal time
 * we have to wait since the last POR. LSB = 175ms.
 */
#define RELIABLE_BATT_DETECT_TIME	0x10

/*
 * Convert the register values to the units that match
 * smart battery protocol.
 */

/* Voltage reg value to mV */
#define VOLTAGE_CONV(REG)       ((REG * 5) >> 6)
/* Current reg value to mA */
#define CURRENT_CONV(REG)       (((REG * 25) >> 4) / BATTERY_MAX17055_RSENSE)
/* Capacity reg value to mAh */
#define CAPACITY_CONV(REG)      (REG * 5 / BATTERY_MAX17055_RSENSE)
/* Time reg value to minute */
#define TIME_CONV(REG)          ((REG * 3) >> 5)
/* Temperature reg value to 0.1K */
#define TEMPERATURE_CONV(REG)   (((REG * 10) >> 8) + 2731)
/* Percentage reg value to 1% */
#define PERCENTAGE_CONV(REG)    (REG >> 8)
/* Cycle count reg value (LSB = 1%) to absolute count (100%) */
#define CYCLE_COUNT_CONV(REG)	((REG * 5) >> 9)

/* Useful macros */
#define MAX17055_READ_DEBUG(offset, ptr_reg) \
	do { \
		if (max17055_read(offset, ptr_reg)) { \
			CPRINTS("%s: failed to read reg %02x", \
				__func__, offset); \
			return; \
		} \
	} while (0)
#define MAX17055_WRITE_DEBUG(offset, reg) \
	do { \
		if (max17055_write(offset, reg)) { \
			CPRINTS("%s: failed to read reg %02x", \
				__func__, offset); \
			return; \
		} \
	} while (0)

static int fake_state_of_charge = -1;

static int max17055_read(int offset, int *data)
{
	return i2c_read16(I2C_PORT_BATTERY, MAX17055_ADDR, offset, data);
}

static int max17055_write(int offset, int data)
{
	return i2c_write16(I2C_PORT_BATTERY, MAX17055_ADDR, offset, data);
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
	strzcpy(device_name, "<BATT>", buf_size);

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

int battery_manufacturer_name(char *dest, int size)
{
	strzcpy(dest, "<unkn>", size);

	return EC_SUCCESS;
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

	rv = max17055_read(REG_CURRENT, &reg);
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

	/* Reset params */
	memset(batt, 0, sizeof(struct batt_params));
	/*
	 * Assuming the battery is responsive as long as
	 * max17055 finds battery is present.
	 */
	batt->is_present = battery_is_present();

	if (batt->is_present == BP_YES)
		batt->flags |= BATT_FLAG_RESPONSIVE;
	else if (batt->is_present == BP_NO)
		/* Battery is not present, gauge won't report useful info. */
		return;

	if (max17055_read(REG_TEMPERATURE, &reg))
		batt->flags |= BATT_FLAG_BAD_TEMPERATURE;

	batt->temperature = TEMPERATURE_CONV((int16_t)reg);

	if (max17055_read(REG_STATE_OF_CHARGE, &reg) &&
	    fake_state_of_charge < 0)
		batt->flags |= BATT_FLAG_BAD_STATE_OF_CHARGE;

	batt->state_of_charge = fake_state_of_charge >= 0 ?
				fake_state_of_charge : PERCENTAGE_CONV(reg);

	if (max17055_read(REG_VOLTAGE, &reg))
		batt->flags |= BATT_FLAG_BAD_VOLTAGE;

	batt->voltage = VOLTAGE_CONV(reg);

	if (max17055_read(REG_CURRENT, &reg))
		batt->flags |= BATT_FLAG_BAD_CURRENT;

	batt->current = CURRENT_CONV((int16_t)reg);

	batt->desired_voltage = battery_get_info()->voltage_max;
	batt->desired_current = BATTERY_DESIRED_CHARGING_CURRENT;

	if (battery_remaining_capacity(&batt->remaining_capacity))
		batt->flags |= BATT_FLAG_BAD_REMAINING_CAPACITY;

	if (battery_full_charge_capacity(&batt->full_capacity))
		batt->flags |= BATT_FLAG_BAD_FULL_CAPACITY;

	/*
	 * Charging allowed if both desired voltage and current are nonzero
	 * and battery isn't full (and we read them all correctly).
	 */
	if (!(batt->flags & BATT_FLAG_BAD_STATE_OF_CHARGE) &&
	    batt->desired_voltage &&
	    batt->desired_current &&
	    batt->state_of_charge < BATTERY_LEVEL_FULL)
		batt->flags |= BATT_FLAG_WANT_CHARGE;

	if (battery_status(&batt->status))
		batt->flags |= BATT_FLAG_BAD_STATUS;
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

/* Configured MAX17055 with the battery parameters for optimal performance. */
static int max17055_load_batt_model(void)
{
	int reg;
	int hib_cfg;
	int dqacc;
	int dpacc;

	int retries = 50;

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

	/* Special sequence to exit hibernate mode. */
	if (max17055_write(0x60, 0x90) ||
	    max17055_write(REG_HIBCFG, 0) ||
	    max17055_write(0x60, 0))
		return EC_ERROR_UNKNOWN;

	if (max17055_write(REG_DPACC, dpacc) ||
	    max17055_write(REG_MODELCFG, (MODELCFG_REFRESH | MODELCFG_VCHG)))
		return EC_ERROR_UNKNOWN;

	/* Delay up to 500 ms until MODELCFG.REFRESH bit == 0. */
	while (--retries) {
		if (max17055_read(REG_MODELCFG, &reg))
			return EC_ERROR_UNKNOWN;
		if (!(MODELCFG_REFRESH & reg))
			break;
		msleep(10);
	}
	if (!retries)
		return EC_ERROR_TIMEOUT;

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

static void max17055_init(void)
{
	int reg;
	int retries = 80;

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
			msleep(10);
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

	CPRINTS("max17055 configuration succeeded!");
}
DECLARE_HOOK(HOOK_INIT, max17055_init, HOOK_PRIO_DEFAULT);
