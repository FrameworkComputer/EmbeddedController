/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "acpi.h"
#include "battery.h"
#include "body_detection.h"
#include "common.h"
#include "console.h"
#include "dptf.h"
#include "ec_commands.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_backlight.h"
#include "lpc.h"
#include "printf.h"
#include "pwm.h"
#include "tablet_mode.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_common.h"
#include "util.h"

#ifdef CONFIG_ZEPHYR
#include <usbc/retimer_fw_update.h>
#endif /* CONFIG_ZEPHYR */

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTF(format, args...) cprintf(CC_LPC, format, ##args)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ##args)

/* Last received ACPI command */
static uint8_t acpi_cmd;
/* First byte of data after ACPI command */
static uint8_t acpi_addr;
/* Number of data writes after command */
static int acpi_data_count;
/* Test byte in ACPI memory space */
static uint8_t acpi_mem_test;

#ifdef CONFIG_DPTF
static int dptf_temp_sensor_id; /* last sensor ID written */
static int dptf_temp_threshold; /* last threshold written */

/*
 * Current DPTF profile number.
 * This is by default initialized to 1 if multi-profile DPTF is not supported.
 * If multi-profile DPTF is supported, this is by default initialized to 2 under
 * the assumption that profile #2 corresponds to lower thresholds and is a safer
 * profile to use until board or some EC driver sets the appropriate profile for
 * device mode.
 */
static int current_dptf_profile = DPTF_PROFILE_DEFAULT;

#endif

/*
 * Keep a read cache of four bytes when burst mode is enabled, which is the
 * size of the largest non-string memmap data type.
 */
#define ACPI_READ_CACHE_SIZE 4

/* Start address that indicates read cache is flushed. */
#define ACPI_READ_CACHE_FLUSHED (EC_ACPI_MEM_MAPPED_BEGIN - 1)

/* Calculate size of valid cache based upon end of memmap data. */
#define ACPI_VALID_CACHE_SIZE(addr)                                       \
	(MIN(EC_ACPI_MEM_MAPPED_SIZE + EC_ACPI_MEM_MAPPED_BEGIN - (addr), \
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

#ifdef CONFIG_DPTF

static int acpi_dptf_is_profile_valid(int n)
{
#ifdef CONFIG_DPTF_MULTI_PROFILE
	if ((n < DPTF_PROFILE_VALID_FIRST) || (n > DPTF_PROFILE_VALID_LAST))
		return EC_ERROR_INVAL;
#else
	if (n != DPTF_PROFILE_DEFAULT)
		return EC_ERROR_INVAL;
#endif

	return EC_SUCCESS;
}

int acpi_dptf_set_profile_num(int n)
{
	int ret = acpi_dptf_is_profile_valid(n);

	if (ret == EC_SUCCESS) {
		current_dptf_profile = n;
		if (IS_ENABLED(CONFIG_DPTF_MULTI_PROFILE) &&
		    IS_ENABLED(CONFIG_HOSTCMD_EVENTS)) {
			/* Notify kernel to update DPTF profile */
			host_set_single_event(EC_HOST_EVENT_MODE_CHANGE);
		}
	}
	return ret;
}

int acpi_dptf_get_profile_num(void)
{
	return current_dptf_profile;
}

#endif

/* Read memmapped data, returns read data or 0xff on error. */
static int acpi_read(uint8_t addr)
{
	uint8_t *memmap_addr = (uint8_t *)(lpc_get_memmap_range() + addr -
					   EC_ACPI_MEM_MAPPED_BEGIN);

	DISABLE_CLANG_WARNING("-Wtautological-constant-out-of-range-compare");
	/* Check for out-of-range read. */
	if (addr < EC_ACPI_MEM_MAPPED_BEGIN ||
	    addr >= EC_ACPI_MEM_MAPPED_BEGIN + EC_ACPI_MEM_MAPPED_SIZE) {
		CPRINTS("ACPI read 0x%02x (ignored)", acpi_addr);
		return 0xff;
	}
	ENABLE_CLANG_WARNING("-Wtautological-constant-out-of-range-compare");

	/* Read from cache if enabled (burst mode). */
	if (acpi_read_cache.enabled) {
		/* Fetch to cache on miss. */
		if (acpi_read_cache.start_addr == ACPI_READ_CACHE_FLUSHED ||
		    acpi_read_cache.start_addr > addr ||
		    addr - acpi_read_cache.start_addr >= ACPI_READ_CACHE_SIZE) {
			memcpy(acpi_read_cache.data, memmap_addr,
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
	int result = 0xff; /* value for bogus read */

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
#ifdef CONFIG_KEYBOARD_BACKLIGHT
		case EC_ACPI_MEM_KEYBOARD_BACKLIGHT:
			result = kblight_get();
			break;
#endif
#ifdef CONFIG_FANS
		case EC_ACPI_MEM_FAN_DUTY:
			result = dptf_get_fan_duty_target();
			break;
#endif
#ifdef CONFIG_DPTF
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

		case EC_ACPI_MEM_DEVICE_ORIENTATION:
			result = 0;

#ifdef CONFIG_TABLET_MODE
			result = tablet_get_mode() << EC_ACPI_MEM_TBMD_SHIFT;
#endif

#ifdef CONFIG_DPTF
			result |= (acpi_dptf_get_profile_num() &
				   EC_ACPI_MEM_DDPN_MASK)
				  << EC_ACPI_MEM_DDPN_SHIFT;
#endif

#ifdef CONFIG_BODY_DETECTION_NOTIFY_MODE_CHANGE
			if (body_detect_get_state() == BODY_DETECTION_ON_BODY)
				result |= BIT(EC_ACPI_MEM_STTB_SHIFT);
#endif
			break;

		case EC_ACPI_MEM_DEVICE_FEATURES0:
		case EC_ACPI_MEM_DEVICE_FEATURES1:
		case EC_ACPI_MEM_DEVICE_FEATURES2:
		case EC_ACPI_MEM_DEVICE_FEATURES3: {
			int off = acpi_addr - EC_ACPI_MEM_DEVICE_FEATURES0;
			uint32_t val = get_feature_flags0();

			/* Flush EC_FEATURE_LIMITED bit. Having it reset to 0
			 * means that FEATURES[0-3] are supported in the first
			 * place, and the other bits are valid.
			 */
			val &= ~1;

			result = val >> (8 * off);
			break;
		}
		case EC_ACPI_MEM_DEVICE_FEATURES4:
		case EC_ACPI_MEM_DEVICE_FEATURES5:
		case EC_ACPI_MEM_DEVICE_FEATURES6:
		case EC_ACPI_MEM_DEVICE_FEATURES7: {
			int off = acpi_addr - EC_ACPI_MEM_DEVICE_FEATURES4;
			uint32_t val = get_feature_flags1();

			result = val >> (8 * off);
			break;
		}

#ifdef CONFIG_USB_PORT_POWER_DUMB
		case EC_ACPI_MEM_USB_PORT_POWER: {
			int i;
			const int port_count = MIN(8, USB_PORT_COUNT);

			/*
			 * Convert each USB port power GPIO signal to a bit
			 * field with max size 8 bits. USB port ID (index) 0 is
			 * the least significant bit.
			 */
			result = 0;
			for (i = 0; i < port_count; ++i) {
				if ((usb_port_enable[i] >= 0) &&
				    (gpio_get_level(usb_port_enable[i]) != 0))
					result |= 1 << i;
			}
			break;
		}
#endif
#ifdef CONFIG_USBC_RETIMER_FW_UPDATE
		case EC_ACPI_MEM_USB_RETIMER_FW_UPDATE:
			result = usb_retimer_fw_update_get_result();
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
#ifdef CONFIG_BATTERY_V2
		case EC_ACPI_MEM_BATTERY_INDEX:
			CPRINTS("ACPI battery %d", data);
			battery_memmap_set_index(data);
			break;
#endif
#ifdef CONFIG_KEYBOARD_BACKLIGHT
		case EC_ACPI_MEM_KEYBOARD_BACKLIGHT: {
			char ts_str[PRINTF_TIMESTAMP_BUF_SIZE];
			/*
			 * Debug output with CR not newline, because the host
			 * does a lot of keyboard backlights and it scrolls the
			 * debug console.
			 */
			snprintf_timestamp_now(ts_str, sizeof(ts_str));
			CPRINTF("\r[%s ACPI kblight %d]", ts_str, data);
			kblight_set(data);
			kblight_enable(data > 0);
			break;
		}
#endif
#ifdef CONFIG_FANS
		case EC_ACPI_MEM_FAN_DUTY:
			dptf_set_fan_duty_target(data);
			break;
#endif
#ifdef CONFIG_DPTF
		case EC_ACPI_MEM_TEMP_ID:
			dptf_temp_sensor_id = data;
			break;
		case EC_ACPI_MEM_TEMP_THRESHOLD:
			dptf_temp_threshold = data + EC_TEMP_SENSOR_OFFSET;
			break;
		case EC_ACPI_MEM_TEMP_COMMIT: {
			int idx = data & EC_ACPI_MEM_TEMP_COMMIT_SELECT_MASK;
			int enable = data & EC_ACPI_MEM_TEMP_COMMIT_ENABLE_MASK;
			dptf_set_temp_threshold(dptf_temp_sensor_id,
						dptf_temp_threshold, idx,
						enable);
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

#ifdef CONFIG_USB_PORT_POWER_DUMB
		case EC_ACPI_MEM_USB_PORT_POWER: {
			int i;
			int mode_field = data;
			const int port_count = MIN(8, USB_PORT_COUNT);

			/*
			 * Read the port power bit field (with max size 8 bits)
			 * and set the charge mode of each USB port accordingly.
			 * USB port ID 0 is the least significant bit.
			 */
			for (i = 0; i < port_count; ++i) {
				int mode = USB_CHARGE_MODE_DISABLED;

				if (mode_field & 1)
					mode = USB_CHARGE_MODE_ENABLED;

				if (usb_charge_set_mode(
					    i, mode,
					    USB_ALLOW_SUSPEND_CHARGE)) {
					CPRINTS("ERROR: could not set charge "
						"mode of USB port p%d to %d",
						i, mode);
				}
				mode_field >>= 1;
			}
			break;
		}
#endif
#ifdef CONFIG_USBC_RETIMER_FW_UPDATE
		case EC_ACPI_MEM_USB_RETIMER_FW_UPDATE:
			usb_retimer_fw_update_process_op(
				EC_ACPI_MEM_USB_RETIMER_PORT(data),
				EC_ACPI_MEM_USB_RETIMER_OP(data));
			break;
#endif
		default:
			CPRINTS("ACPI write 0x%02x = 0x%02x (ignored)",
				acpi_addr, data);
			break;
		}
	} else if (acpi_cmd == EC_CMD_ACPI_QUERY_EVENT && !acpi_data_count) {
		/* Clear and return the lowest host event */
		int evt_index = lpc_get_next_host_event();
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
		hook_call_deferred(&acpi_disable_burst_deferred_data,
				   1 * SECOND);

		/* ACPI 5.0-12.3.3: Burst ACK */
		*resultptr = 0x90;
		retval = 1;
	} else if (acpi_cmd == EC_CMD_ACPI_BURST_DISABLE && !acpi_data_count) {
		acpi_read_cache.enabled = 0;

		/* Leave burst mode */
		hook_call_deferred(&acpi_disable_burst_deferred_data, -1);
		lpc_clear_acpi_status_mask(EC_LPC_STATUS_BURST_MODE);
	}

	return retval;
}
