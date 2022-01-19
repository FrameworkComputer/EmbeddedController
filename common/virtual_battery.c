/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Virtual battery cross-platform code for Chrome EC */

#include "battery.h"
#include "charge_state.h"
#include "i2c.h"
#include "system.h"
#include "util.h"
#include "virtual_battery.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

#define BATT_MODE_UNINITIALIZED -1

/*
 * The state machine used to parse smart battery command
 * to support virtual battery.
 */
enum batt_cmd_parse_state {
	IDLE = 0, /* initial state */
	START = 1, /* received the register address (command code) */
	WRITE_VB, /* writing data bytes to the peripheral */
	READ_VB, /* reading data bytes to the peripheral */
};

static enum batt_cmd_parse_state sb_cmd_state;
static uint8_t cache_hit;
static const uint8_t *batt_cmd_head;
static int acc_write_len;

int virtual_battery_handler(struct ec_response_i2c_passthru *resp,
				   int in_len, int *err_code, int xferflags,
				   int read_len, int write_len,
				   const uint8_t *out)
{

#if defined(CONFIG_BATTERY_PRESENT_GPIO) || \
	defined(CONFIG_BATTERY_PRESENT_CUSTOM)
	/*
	 * If the battery isn't present, return a NAK (which we
	 * would have gotten anyways had we attempted to talk to
	 * the battery.)
	 */
	if (battery_is_present() != BP_YES) {
		resp->i2c_status = EC_I2C_STATUS_NAK;
		return EC_ERROR_INVAL;
	}
#endif
	switch (sb_cmd_state) {
	case IDLE:
		/*
		 * A legal battery command must start
		 * with a i2c write for reg index.
		 */
		if (write_len == 0) {
			resp->i2c_status = EC_I2C_STATUS_NAK;
			return EC_ERROR_INVAL;
		}
		/* Record the head of battery command. */
		batt_cmd_head = out;
		sb_cmd_state = START;
		*err_code = 0;
		break;
	case START:
		if (write_len > 0) {
			sb_cmd_state = WRITE_VB;
			*err_code = 0;
		} else {
			sb_cmd_state = READ_VB;
			*err_code = virtual_battery_operation(batt_cmd_head,
						NULL, 0, 0);
			/*
			 * If the reg is not handled by virtual battery, we
			 * do not support it.
			 */
			if (*err_code)
				return EC_ERROR_INVAL;
			cache_hit = 1;
		}
		break;
	case WRITE_VB:
		if (write_len == 0) {
			resp->i2c_status = EC_I2C_STATUS_NAK;
			reset_parse_state();
			return EC_ERROR_INVAL;
		}
		*err_code = 0;
		break;
	case READ_VB:
		if (read_len == 0) {
			resp->i2c_status = EC_I2C_STATUS_NAK;
			reset_parse_state();
			return EC_ERROR_INVAL;
		}
		/*
		 * Do not send the command to battery
		 * if the reg is cached.
		 */
		if (cache_hit)
			*err_code = 0;
		break;
	default:
		reset_parse_state();
		return EC_ERROR_INVAL;
	}

	acc_write_len += write_len;

	/* the last message */
	if (xferflags & I2C_XFER_STOP) {
		switch (sb_cmd_state) {
		/* write to virtual battery */
		case START:
		case WRITE_VB:
			virtual_battery_operation(batt_cmd_head,
						NULL,
						0,
						acc_write_len);
			break;
		/* read from virtual battery */
		case READ_VB:
			if (cache_hit) {
				read_len += in_len;
				memset(&resp->data[0], 0, read_len);
				virtual_battery_operation(batt_cmd_head,
							&resp->data[0],
							read_len,
							0);
			}
			break;
		default:
			reset_parse_state();
			return EC_ERROR_INVAL;

		}
		/* Reset the state in the end of messages */
		reset_parse_state();
	}
	return EC_RES_SUCCESS;
}

void reset_parse_state(void)
{
	sb_cmd_state = IDLE;
	cache_hit = 0;
	acc_write_len = 0;
}

/*
 * Copy memmap string data from offset to dest, up to size len, in the format
 * expected by SBS (first byte of dest contains strlen).
 */
void copy_memmap_string(uint8_t *dest, int offset, int len)
{
	uint8_t *memmap_str;
	uint8_t memmap_strlen;

	if (len == 0)
		return;
	memmap_str = host_get_memmap(offset);
	/* memmap_str might not be NULL terminated */
	memmap_strlen = *(memmap_str + EC_MEMMAP_TEXT_MAX - 1) == '\0' ?
			strlen(memmap_str) : EC_MEMMAP_TEXT_MAX;
	dest[0] = memmap_strlen;
	memcpy(dest + 1, memmap_str, MIN(memmap_strlen, len - 1));
}

static void copy_battery_info_string(uint8_t *dst, const uint8_t *src, int len)
{
	if (len == 0)
		return;

	dst[0] = strlen(src);
	strncpy(dst + 1, src, len - 1);
}

int virtual_battery_operation(const uint8_t *batt_cmd_head,
			      uint8_t *dest,
			      int read_len,
			      int write_len)
{
	int val;
	int year, month, day;
	/*
	 * We cache battery operational mode locally for both read and write
	 * commands. If MODE_CAPACITY bit is set, battery capacity will be
	 * reported in 10mW/10mWh, instead of the default unit, mA/mAh.
	 * Note that we don't update the cached capacity: We do a real-time
	 * conversion and return the converted values.
	 */
	static int batt_mode_cache = BATT_MODE_UNINITIALIZED;
	const struct batt_params *curr_batt;
	/*
	 * Don't allow host reads into arbitrary memory space, most params
	 * are two bytes.
	 */
	int bounded_read_len = MIN(read_len, 2);
	const struct battery_static_info *bs;

	if (IS_ENABLED(CONFIG_BATTERY_V2))
		/*
		 * TODO: To support multiple batteries, we need to translate
		 * i2c address to a battery index.
		 */
		bs = &battery_static[BATT_IDX_MAIN];

	curr_batt = charger_current_battery_params();
	switch (*batt_cmd_head) {
	case SB_BATTERY_MODE:
		if (write_len == 3) {
			batt_mode_cache = batt_cmd_head[1] |
					  (batt_cmd_head[2] << 8);
		} else if (read_len > 0) {
			if (batt_mode_cache == BATT_MODE_UNINITIALIZED)
				/*
				 * Read the battery operational mode from
				 * the battery to initialize batt_mode_cache.
				 * This may cause an i2c transaction.
				 */
				if (battery_get_mode(&batt_mode_cache) ==
				    EC_ERROR_UNIMPLEMENTED)
					/*
					 * Register not supported, choose
					 * typical SB defaults.
					 */
					batt_mode_cache =
					   MODE_INTERNAL_CHARGE_CONTROLLER |
					   MODE_ALARM |
					   MODE_CHARGER;

			memcpy(dest, &batt_mode_cache, bounded_read_len);
		}
		break;
	case SB_SERIAL_NUMBER:
		val = strtoi(host_get_memmap(EC_MEMMAP_BATT_SERIAL), NULL, 16);
		memcpy(dest, &val, bounded_read_len);
		break;
	case SB_VOLTAGE:
		if (curr_batt->flags & BATT_FLAG_BAD_VOLTAGE)
			return EC_ERROR_BUSY;
		memcpy(dest, &(curr_batt->voltage), bounded_read_len);
		break;
	case SB_RELATIVE_STATE_OF_CHARGE:
		if (curr_batt->flags & BATT_FLAG_BAD_STATE_OF_CHARGE)
			return EC_ERROR_BUSY;
		memcpy(dest, &(curr_batt->state_of_charge), bounded_read_len);
		break;
	case SB_TEMPERATURE:
		if (curr_batt->flags & BATT_FLAG_BAD_TEMPERATURE)
			return EC_ERROR_BUSY;
		memcpy(dest, &(curr_batt->temperature), bounded_read_len);
		break;
	case SB_CURRENT:
		if (curr_batt->flags & BATT_FLAG_BAD_CURRENT)
			return EC_ERROR_BUSY;
		memcpy(dest, &(curr_batt->current), bounded_read_len);
		break;
	case SB_AVERAGE_CURRENT:
		/* This may cause an i2c transaction */
		if (curr_batt->flags & BATT_FLAG_BAD_AVERAGE_CURRENT)
			return EC_ERROR_BUSY;
		val = battery_get_avg_current();
		memcpy(dest, &val, bounded_read_len);
		break;
	case SB_MAX_ERROR:
		/* report as 3% to make kernel happy */
		val = BATTERY_LEVEL_SHUTDOWN;
		memcpy(dest, &val, bounded_read_len);
		break;
	case SB_FULL_CHARGE_CAPACITY:
		if (curr_batt->flags & BATT_FLAG_BAD_FULL_CAPACITY ||
				curr_batt->flags & BATT_FLAG_BAD_VOLTAGE)
			return EC_ERROR_BUSY;
		val = curr_batt->full_capacity;
		if (batt_mode_cache & MODE_CAPACITY)
			val = val * curr_batt->voltage / 10000;
		memcpy(dest, &val, bounded_read_len);
		break;
	case SB_BATTERY_STATUS:
		if (curr_batt->flags & BATT_FLAG_BAD_STATUS)
			return EC_ERROR_BUSY;
		memcpy(dest, &(curr_batt->status), bounded_read_len);
		break;
	case SB_CYCLE_COUNT:
		memcpy(dest, (int *)host_get_memmap(EC_MEMMAP_BATT_CCNT),
		       bounded_read_len);
		break;
	case SB_DESIGN_CAPACITY:
		if (curr_batt->flags & BATT_FLAG_BAD_VOLTAGE)
			return EC_ERROR_BUSY;
		val = *(int *)host_get_memmap(EC_MEMMAP_BATT_DCAP);
		if (batt_mode_cache & MODE_CAPACITY)
			val = val * curr_batt->voltage / 10000;
		memcpy(dest, &val, bounded_read_len);
		break;
	case SB_DESIGN_VOLTAGE:
		memcpy(dest, (int *)host_get_memmap(EC_MEMMAP_BATT_DVLT),
		       bounded_read_len);
		break;
	case SB_REMAINING_CAPACITY:
		if (curr_batt->flags & BATT_FLAG_BAD_REMAINING_CAPACITY ||
				curr_batt->flags & BATT_FLAG_BAD_VOLTAGE)
			return EC_ERROR_BUSY;
		val = curr_batt->remaining_capacity;
		if (batt_mode_cache & MODE_CAPACITY)
			val = val * curr_batt->voltage / 10000;
		memcpy(dest, &val, bounded_read_len);
		break;
	case SB_MANUFACTURER_NAME:
		if (IS_ENABLED(CONFIG_BATTERY_V2))
			copy_battery_info_string(dest, bs->manufacturer_ext,
						 read_len);
		else
			copy_memmap_string(dest, EC_MEMMAP_BATT_MFGR, read_len);
		break;
	case SB_DEVICE_NAME:
		if (IS_ENABLED(CONFIG_BATTERY_V2))
			copy_battery_info_string(dest, bs->model_ext, read_len);
		else
			copy_memmap_string(dest, EC_MEMMAP_BATT_MODEL,
					   read_len);
		break;
	case SB_DEVICE_CHEMISTRY:
		if (IS_ENABLED(CONFIG_BATTERY_V2))
			copy_battery_info_string(dest, bs->type_ext, read_len);
		else
			copy_memmap_string(dest, EC_MEMMAP_BATT_TYPE, read_len);
		break;
	case SB_AVERAGE_TIME_TO_FULL:
		/* This may cause an i2c transaction */
		if (battery_time_to_full(&val))
			return EC_ERROR_INVAL;
		memcpy(dest, &val, bounded_read_len);
		break;
	case SB_AVERAGE_TIME_TO_EMPTY:
		/* This may cause an i2c transaction */
		if (battery_time_to_empty(&val))
			return EC_ERROR_INVAL;
		memcpy(dest, &val, bounded_read_len);
		break;
#ifdef CONFIG_BATTERY_SMART
	/*
	 * Only supports sb for now, respective gauges should implement their
	 * function.
	 */
	case SB_RUN_TIME_TO_EMPTY:
		/* This may cause an i2c transaction */
		if (battery_run_time_to_empty(&val))
			return EC_ERROR_INVAL;
		memcpy(dest, &val, bounded_read_len);
		break;
#endif
	case SB_CHARGING_CURRENT:
		if (curr_batt->flags & BATT_FLAG_BAD_DESIRED_CURRENT)
			return EC_ERROR_BUSY;
		val = curr_batt->desired_current;
		memcpy(dest, &val, bounded_read_len);
		break;
	case SB_CHARGING_VOLTAGE:
		if (curr_batt->flags & BATT_FLAG_BAD_DESIRED_VOLTAGE)
			return EC_ERROR_BUSY;
		val = curr_batt->desired_voltage;
		memcpy(dest, &val, bounded_read_len);
		break;
	case SB_MANUFACTURE_DATE:
		/* This may cause an i2c transaction */
		if (!battery_manufacture_date(&year, &month, &day)) {
			/* Encode in Smart Battery Spec format */
			val = ((year - 1980) << 9) + (month << 5) + day;
		} else {
			/*
			 * Return 0 on error. The kernel is unhappy with
			 * returning an error code.
			 */
			val = 0;
		}
		memcpy(dest, &val, bounded_read_len);
		break;
	case SB_MANUFACTURER_ACCESS:
		/* No manuf. access reg access allowed over VB interface */
		return EC_ERROR_INVAL;
	case SB_SPECIFICATION_INFO:
		/* v1.1 without PEC, no scale factor to voltage and current */
		val = 0x0011;
		memcpy(dest, &val, bounded_read_len);
		break;
	default:
		CPRINTS("Unhandled VB reg %x", *batt_cmd_head);
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;
}
