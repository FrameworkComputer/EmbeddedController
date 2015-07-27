/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "acpi.h"
#include "common.h"
#include "console.h"
#include "dptf.h"
#include "hooks.h"
#include "host_command.h"
#include "lpc.h"
#include "ec_commands.h"
#include "pwm.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTF(format, args...) cprintf(CC_LPC, format, ## args)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)

static uint8_t acpi_cmd;         /* Last received ACPI command */
static uint8_t acpi_addr;        /* First byte of data after ACPI command */
static int acpi_data_count;      /* Number of data writes after command */
static uint8_t acpi_mem_test;    /* Test byte in ACPI memory space */

#ifdef CONFIG_TEMP_SENSOR
static int dptf_temp_sensor_id;			/* last sensor ID written */
static int dptf_temp_threshold;			/* last threshold written */
#endif

/*
 * Keep a read cache of four bytes when burst mode is enabled, which is the
 * size of the largest non-string memmap data type.
 */
#define ACPI_READ_CACHE_SIZE 4

/* Start address that indicates read cache is flushed. */
#define ACPI_READ_CACHE_FLUSHED (EC_ACPI_MEM_MAPPED_BEGIN - 1)

/* Calculate size of valid cache based upon end of memmap data. */
#define ACPI_VALID_CACHE_SIZE(addr) (MIN( \
	EC_ACPI_MEM_MAPPED_SIZE + EC_ACPI_MEM_MAPPED_BEGIN - (addr), \
	ACPI_READ_CACHE_SIZE))

/*
 * In burst mode, read the requested memmap data and the data immediately
 * following it into a cache. For future reads in burst mode, try to grab
 * data from the cache. This ensures the continuity of multi-byte reads,
 * which is important when dealing with data types > 8 bits.
 */
static struct {
	int enabled;
	uint8_t start_addr;
	uint8_t data[ACPI_READ_CACHE_SIZE];
} acpi_read_cache;

/*
 * Deferred function to ensure that ACPI burst mode doesn't remain enabled
 * indefinitely.
 */
static void acpi_disable_burst_deferred(void)
{
	acpi_read_cache.enabled = 0;
	lpc_clear_acpi_status_mask(EC_LPC_STATUS_BURST_MODE);
	CPUTS("ACPI missed burst disable?");
}
DECLARE_DEFERRED(acpi_disable_burst_deferred);

/* Read memmapped data, returns read data or 0xff on error. */
static int acpi_read(uint8_t addr)
{
	uint8_t *memmap_addr = (uint8_t *)(lpc_get_memmap_range() + addr -
					   EC_ACPI_MEM_MAPPED_BEGIN);

	/* Check for out-of-range read. */
	if (addr < EC_ACPI_MEM_MAPPED_BEGIN ||
	    addr >= EC_ACPI_MEM_MAPPED_BEGIN + EC_ACPI_MEM_MAPPED_SIZE) {
		CPRINTS("ACPI read 0x%02x (ignored)",
			acpi_addr);
		return 0xff;
	}

	/* Read from cache if enabled (burst mode). */
	if (acpi_read_cache.enabled) {
		/* Fetch to cache on miss. */
		if (acpi_read_cache.start_addr == ACPI_READ_CACHE_FLUSHED ||
		    acpi_read_cache.start_addr > addr ||
		    addr - acpi_read_cache.start_addr >=
		    ACPI_READ_CACHE_SIZE) {
			memcpy(acpi_read_cache.data,
			       memmap_addr,
			       ACPI_VALID_CACHE_SIZE(addr));
			acpi_read_cache.start_addr = addr;
		}
		/* Return data from cache. */
		return acpi_read_cache.data[addr - acpi_read_cache.start_addr];
	} else {
		/* Read directly from memmap data. */
		return *memmap_addr;
	}
}

/*
 * This handles AP writes to the EC via the ACPI I/O port. There are only a few
 * ACPI commands (EC_CMD_ACPI_*), but they are all handled here.
 */
int acpi_ap_to_ec(int is_cmd, uint8_t value, uint8_t *resultptr)
{
	int data = 0;
	int retval = 0;
	int result = 0xff;			/* value for bogus read */

	/* Read command/data; this clears the FRMH status bit. */
	if (is_cmd) {
		acpi_cmd = value;
		acpi_data_count = 0;
	} else {
		data = value;
		/*
		 * The first data byte is the ACPI memory address for
		 * read/write commands.
		 */
		if (!acpi_data_count++)
			acpi_addr = data;
	}

	/* Process complete commands */
	if (acpi_cmd == EC_CMD_ACPI_READ && acpi_data_count == 1) {
		/* ACPI read cmd + addr */
		switch (acpi_addr) {
		case EC_ACPI_MEM_VERSION:
			result = EC_ACPI_MEM_VERSION_CURRENT;
			break;
		case EC_ACPI_MEM_TEST:
			result = acpi_mem_test;
			break;
		case EC_ACPI_MEM_TEST_COMPLIMENT:
			result = 0xff - acpi_mem_test;
			break;
#ifdef CONFIG_PWM_KBLIGHT
		case EC_ACPI_MEM_KEYBOARD_BACKLIGHT:
			result = pwm_get_duty(PWM_CH_KBLIGHT);
			break;
#endif
#ifdef CONFIG_FANS
		case EC_ACPI_MEM_FAN_DUTY:
			result = dptf_get_fan_duty_target();
			break;
#endif
#ifdef CONFIG_TEMP_SENSOR
		case EC_ACPI_MEM_TEMP_ID:
			result = dptf_query_next_sensor_event();
			break;
#endif
#ifdef CONFIG_CHARGER
		case EC_ACPI_MEM_CHARGING_LIMIT:
			result = dptf_get_charging_current_limit();
			if (result >= 0)
				result /= EC_ACPI_MEM_CHARGING_LIMIT_STEP_MA;
			else
				result = EC_ACPI_MEM_CHARGING_LIMIT_DISABLED;
			break;
#endif
		default:
			result = acpi_read(acpi_addr);
			break;
		}

		/* Send the result byte */
		*resultptr = result;
		retval = 1;

	} else if (acpi_cmd == EC_CMD_ACPI_WRITE && acpi_data_count == 2) {
		/* ACPI write cmd + addr + data */
		switch (acpi_addr) {
		case EC_ACPI_MEM_TEST:
			acpi_mem_test = data;
			break;
#ifdef CONFIG_PWM_KBLIGHT
		case EC_ACPI_MEM_KEYBOARD_BACKLIGHT:
			/*
			 * Debug output with CR not newline, because the host
			 * does a lot of keyboard backlights and it scrolls the
			 * debug console.
			 */
			CPRINTF("\r[%T ACPI kblight %d]", data);
			pwm_set_duty(PWM_CH_KBLIGHT, data);
			break;
#endif
#ifdef CONFIG_FANS
		case EC_ACPI_MEM_FAN_DUTY:
			dptf_set_fan_duty_target(data);
			break;
#endif
#ifdef CONFIG_TEMP_SENSOR
		case EC_ACPI_MEM_TEMP_ID:
			dptf_temp_sensor_id = data;
			break;
		case EC_ACPI_MEM_TEMP_THRESHOLD:
			dptf_temp_threshold = data + EC_TEMP_SENSOR_OFFSET;
			break;
		case EC_ACPI_MEM_TEMP_COMMIT:
		{
			int idx = data & EC_ACPI_MEM_TEMP_COMMIT_SELECT_MASK;
			int enable = data & EC_ACPI_MEM_TEMP_COMMIT_ENABLE_MASK;
			dptf_set_temp_threshold(dptf_temp_sensor_id,
						dptf_temp_threshold,
						idx, enable);
			break;
		}
#endif
#ifdef CONFIG_CHARGER
		case EC_ACPI_MEM_CHARGING_LIMIT:
			if (data == EC_ACPI_MEM_CHARGING_LIMIT_DISABLED) {
				dptf_set_charging_current_limit(-1);
			} else {
				data *= EC_ACPI_MEM_CHARGING_LIMIT_STEP_MA;
				dptf_set_charging_current_limit(data);
			}
			break;
#endif
		default:
			CPRINTS("ACPI write 0x%02x = 0x%02x (ignored)",
				acpi_addr, data);
			break;
		}
	} else if (acpi_cmd == EC_CMD_ACPI_QUERY_EVENT && !acpi_data_count) {
		/* Clear and return the lowest host event */
		int evt_index = lpc_query_host_event_state();
		CPRINTS("ACPI query = %d", evt_index);
		*resultptr = evt_index;
		retval = 1;
	} else if (acpi_cmd == EC_CMD_ACPI_BURST_ENABLE && !acpi_data_count) {
		/*
		 * TODO: The kernel only enables BURST when doing multi-byte
		 * value reads over the ACPI port. We don't do such reads
		 * when our memmap data can be accessed directly over LPC,
		 * so on LM4, for example, this is dead code. We might want
		 * to add a config to skip this code for certain chips.
		 */
		acpi_read_cache.enabled = 1;
		acpi_read_cache.start_addr = ACPI_READ_CACHE_FLUSHED;

		/* Enter burst mode */
		lpc_set_acpi_status_mask(EC_LPC_STATUS_BURST_MODE);

		/*
		 * Disable from deferred function in case burst mode is enabled
		 * for an extremely long time  (ex. kernel bug / crash).
		 */
		hook_call_deferred(acpi_disable_burst_deferred, 1*SECOND);

		/* ACPI 5.0-12.3.3: Burst ACK */
		*resultptr = 0x90;
		retval = 1;
	} else if (acpi_cmd == EC_CMD_ACPI_BURST_DISABLE && !acpi_data_count) {
		acpi_read_cache.enabled = 0;

		/* Leave burst mode */
		hook_call_deferred(acpi_disable_burst_deferred, -1);
		lpc_clear_acpi_status_mask(EC_LPC_STATUS_BURST_MODE);
	}

	return retval;
}
