/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "chipset.h"
#include "comm-host.h"
#include "comm-usb.h"
#include "compile_time_macros.h"
#include "crc.h"
#include "cros_ec_dev.h"
#include "ec_flash.h"
#include "ec_version.h"
#include "ectool.h"
#include "i2c.h"
#include "lightbar.h"
#include "lock/gec_lock.h"
#include "misc_util.h"
#include "panic.h"
#include "tablet_mode.h"
#include "usb_pd.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <libchrome/base/json/json_reader.h>
#include <libec/add_entropy_command.h>
#include <libec/ec_panicinfo.h>
#include <libec/fingerprint/fp_encryption_status_command.h>
#include <libec/flash_protect_command.h>
#include <libec/rand_num_command.h>
#include <libec/versions_command.h>
#include <memory>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

/* Maximum flash size (16 MB, conservative) */
#define MAX_FLASH_SIZE 0x1000000

/*
 * Calculate the expected response for a hello ec command.
 */
#define HELLO_RESP(in_data) ((in_data) + 0x01020304)

#define USB_VID_GOOGLE 0x18d1
#define USB_PID_HAMMER 0x5022

/* Command line options */
enum {
	OPT_DEV = 1000,
	OPT_INTERFACE,
	OPT_NAME,
	OPT_ASCII,
	OPT_I2C_BUS,
	OPT_DEVICE,
	OPT_VERBOSE,
};

static struct option long_opts[] = { { "dev", 1, 0, OPT_DEV },
				     { "interface", 1, 0, OPT_INTERFACE },
				     { "name", 1, 0, OPT_NAME },
				     { "ascii", 0, 0, OPT_ASCII },
				     { "i2c_bus", 1, 0, OPT_I2C_BUS },
				     { "device", 1, 0, OPT_DEVICE },
				     { "verbose", no_argument, NULL,
				       OPT_VERBOSE },
				     { NULL, 0, 0, 0 } };

#define GEC_LOCK_TIMEOUT_SECS 30 /* 30 secs */

const char help_str[] = "Commands:";

/* Note: depends on enum ec_image */
static const char *const image_names[] = { "unknown", "RO", "RW" };

/* Note: depends on enum ec_led_colors */
static const char *const led_color_names[] = { "red",	 "green", "blue",
					       "yellow", "white", "amber" };
BUILD_ASSERT(ARRAY_SIZE(led_color_names) == EC_LED_COLOR_COUNT);

/* Note: depends on enum ec_led_id */
static const char *const led_names[] = { "battery",    "power",
					 "adapter",    "left",
					 "right",      "recovery_hwreinit",
					 "sysrq debug" };
BUILD_ASSERT(ARRAY_SIZE(led_names) == EC_LED_ID_COUNT);

/* ASCII mode for printing, default off */
int ascii_mode;

/* Message verbosity */
static int verbose = 0;

/* Check SBS numerical value range */
int is_battery_range(int val)
{
	return (val >= 0 && val <= 65535) ? 1 : 0;
}

int parse_bool(const char *s, int *dest)
{
	if (!strcasecmp(s, "off") || !strncasecmp(s, "dis", 3) ||
	    tolower(*s) == 'f' || tolower(*s) == 'n') {
		*dest = 0;
		return 1;
	} else if (!strcasecmp(s, "on") || !strncasecmp(s, "ena", 3) ||
		   tolower(*s) == 't' || tolower(*s) == 'y') {
		*dest = 1;
		return 1;
	} else {
		return 0;
	}
}

/**
 * @brief Find the enum value associated the string of enum text or value.
 *
 * @param str The input string to parse an enum from.
 * @param enum_text_map The array that maps enum value (index) to text.
 * @param enum_text_map_length The length of the enum_text_map array.
 * @param enum_value Output parsed enum value.
 * @return int 0 on success, -1 if result cannot be found
 */
static int find_enum_from_text(const char *str,
			       const char *const enum_text_map[],
			       long enum_text_map_length, long *enum_value)
{
	char *e;
	long value;

	assert(str);
	assert(enum_value);
	assert(enum_text_map);
	assert(enum_text_map_length >= 0);

	if (*str == '\0')
		return -1;

	value = strtol(str, &e, 0);
	if (!e || !*e) {
		*enum_value = value;
		return 0;
	}

	for (value = 0; value < enum_text_map_length; value++) {
		if (!enum_text_map[value])
			continue;
		if (strcasecmp(str, enum_text_map[value]) == 0) {
			*enum_value = value;
			return 0;
		}
	}

	return -1;
}

static uint8_t read_mapped_mem8(uint8_t offset)
{
	int ret;
	uint8_t val;

	ret = ec_readmem(offset, sizeof(val), &val);
	if (ret <= 0) {
		fprintf(stderr, "failure in %s(): %d\n", __func__, ret);
		exit(1);
	}
	return val;
}

static uint16_t read_mapped_mem16(uint8_t offset)
{
	int ret;
	uint16_t val;

	ret = ec_readmem(offset, sizeof(val), &val);
	if (ret <= 0) {
		fprintf(stderr, "failure in %s(): %d\n", __func__, ret);
		exit(1);
	}
	return val;
}

static uint32_t read_mapped_mem32(uint8_t offset)
{
	int ret;
	uint32_t val;

	ret = ec_readmem(offset, sizeof(val), &val);
	if (ret <= 0) {
		fprintf(stderr, "failure in %s(): %d\n", __func__, ret);
		exit(1);
	}
	return val;
}

static int read_mapped_string(uint8_t offset, char *buffer, int max_size)
{
	int ret;

	ret = ec_readmem(offset, max_size, buffer);
	if (ret <= 0) {
		fprintf(stderr, "failure in %s(): %d\n", __func__, ret);
		exit(1);
	}
	return ret;
}

static int wait_event_mask(unsigned long event_mask,
			   struct ec_response_get_next_event_v1 *buffer,
			   size_t buffer_size, long timeout)
{
	int rv;

	rv = ec_pollevent(event_mask, buffer, buffer_size, timeout);
	if (rv == 0) {
		fprintf(stderr, "Timeout waiting for MKBP event\n");
		return -ETIMEDOUT;
	} else if (rv < 0) {
		perror("Error polling for MKBP event\n");
		return -EIO;
	}

	return rv;
}

static int wait_event(long event_type,
		      struct ec_response_get_next_event_v1 *buffer,
		      size_t buffer_size, long timeout)
{
	return wait_event_mask(1 << event_type, buffer, buffer_size, timeout);
}

int cmd_adc_read(int argc, char *argv[])
{
	char *e;
	struct ec_params_adc_read p;
	struct ec_response_adc_read r;
	int rv;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <adc channel>\n", argv[0]);
		return -1;
	}

	p.adc_channel = (uint8_t)strtoull(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "\"%s\": invalid channel!\n", argv[1]);
		return -1;
	}

	rv = ec_command(EC_CMD_ADC_READ, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv > 0) {
		printf("%s: %d\n", argv[1], r.adc_value);
		return 0;
	}

	return rv;
}

int cmd_add_entropy(int argc, char *argv[])
{
	bool reset = false;
	if (argc >= 2 && !strcmp(argv[1], "reset"))
		reset = true;

	ec::AddEntropyCommand add_entropy_command(reset);
	if (!add_entropy_command.Run(comm_get_fd())) {
		fprintf(stderr, "Failed to run addentropy command\n");
		return -1;
	}

	int rv = add_entropy_command.Result();
	if (rv != EC_RES_SUCCESS) {
		rv = -EECRESULT - add_entropy_command.Result();
		fprintf(stderr, "Failed to add entropy: %d\n", rv);
		return rv;
	}

	printf("Entropy added successfully\n");
	return rv;
}

int cmd_hello(int argc, char *argv[])
{
	struct ec_params_hello p;
	struct ec_response_hello r;
	int rv;

	p.in_data = 0xa0b0c0d0;

	rv = ec_command(EC_CMD_HELLO, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;

	if (r.out_data != 0xa1b2c3d4) {
		fprintf(stderr, "Expected response 0x%08x, got 0x%08x\n",
			0xa1b2c3d4, r.out_data);
		return -1;
	}

	printf("EC says hello!\n");
	return 0;
}

int cmd_hibdelay(int argc, char *argv[])
{
	struct ec_params_hibernation_delay p;
	struct ec_response_hibernation_delay r;
	char *e;
	int rv;

	if (argc < 2) {
		p.seconds = 0; /* Just read the current settings. */
	} else {
		p.seconds = strtoull(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "invalid number\n");
			return -1;
		}
	}

	rv = ec_command(EC_CMD_HIBERNATION_DELAY, 0, &p, sizeof(p), &r,
			sizeof(r));
	if (rv < 0) {
		fprintf(stderr, "err: rv=%d\n", rv);
		return -1;
	}

	printf("Hibernation delay: %u s\n", r.hibernate_delay);
	printf("Time G3: %u s\n", r.time_g3);
	printf("Time left: %u s\n", r.time_remaining);
	return 0;
}

static void cmd_hostevent_help(char *cmd)
{
	fprintf(stderr,
		"  Usage: %s get <type>\n"
		"  Usage: %s set <type> <value>\n"
		"    <type> is one of:\n"
		"      1: EC_HOST_EVENT_B\n"
		"      2: EC_HOST_EVENT_SCI_MASK\n"
		"      3: EC_HOST_EVENT_SMI_MASK\n"
		"      4: EC_HOST_EVENT_ALWAYS_REPORT_MASK\n"
		"      5: EC_HOST_EVENT_ACTIVE_WAKE_MASK\n"
		"      6: EC_HOST_EVENT_LAZY_WAKE_MASK_S0IX\n"
		"      7: EC_HOST_EVENT_LAZY_WAKE_MASK_S3\n"
		"      8: EC_HOST_EVENT_LAZY_WAKE_MASK_S5\n",
		cmd, cmd);
}

static int cmd_hostevent(int argc, char *argv[])
{
	struct ec_params_host_event p;
	struct ec_response_host_event r;
	char *e;
	int rv;

	if (argc < 2) {
		fprintf(stderr, "Invalid number of params\n");
		cmd_hostevent_help(argv[0]);
		return -1;
	}

	if (!strcasecmp(argv[1], "get")) {
		if (argc != 3) {
			fprintf(stderr, "Invalid number of params\n");
			cmd_hostevent_help(argv[0]);
			return -1;
		}
		p.action = EC_HOST_EVENT_GET;
	} else if (!strcasecmp(argv[1], "set")) {
		if (argc != 4) {
			fprintf(stderr, "Invalid number of params\n");
			cmd_hostevent_help(argv[0]);
			return -1;
		}
		p.action = EC_HOST_EVENT_SET;
		p.value = strtoull(argv[3], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad value\n");
			return -1;
		}
	} else {
		fprintf(stderr, "Bad subcommand: %s\n", argv[1]);
		return -1;
	}

	p.mask_type = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad type\n");
		return -1;
	}

	rv = ec_command(EC_CMD_HOST_EVENT, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv == -EC_RES_ACCESS_DENIED - EECRESULT) {
		fprintf(stderr, "%s isn't permitted for mask %d.\n",
			p.action == EC_HOST_EVENT_SET ? "Set" : "Get",
			p.mask_type);
		return rv;
	} else if (rv < 0) {
		return rv;
	}

	if (p.action == EC_HOST_EVENT_GET)
		printf("0x%" PRIx64 "\n", r.value);

	return 0;
}

static int get_latest_cmd_version(uint8_t cmd, int *version)
{
	struct ec_params_get_cmd_versions p;
	struct ec_response_get_cmd_versions r;
	int rv;

	*version = 0;
	/* Figure out the latest version of the given command the EC supports */
	p.cmd = cmd;
	rv = ec_command(EC_CMD_GET_CMD_VERSIONS, 0, &p, sizeof(p), &r,
			sizeof(r));
	if (rv < 0) {
		if (rv == -EC_RES_INVALID_PARAM)
			printf("Command 0x%02x not supported by EC.\n",
			       EC_CMD_GET_CMD_VERSIONS);
		return rv;
	}

	if (r.version_mask)
		*version = __fls(r.version_mask);

	return rv;
}

int cmd_hostsleepstate(int argc, char *argv[])
{
	struct ec_params_host_sleep_event p;
	struct ec_params_host_sleep_event_v1 p1;
	struct ec_response_host_sleep_event_v1 r;
	void *pp = &p;
	size_t psize = sizeof(p), rsize = 0;
	char *afterscan;
	int rv;
	int version = 0, max_version = 0;
	uint32_t timeout, transitions;

	if (argc < 2) {
		fprintf(stderr,
			"Usage: %s "
			"[suspend|wsuspend|resume|freeze|thaw] [timeout]\n",
			argv[0]);
		return -1;
	}

	rv = get_latest_cmd_version(EC_CMD_HOST_SLEEP_EVENT, &max_version);
	if (rv < 0)
		return rv;

	if (!strcmp(argv[1], "suspend"))
		p.sleep_event = HOST_SLEEP_EVENT_S3_SUSPEND;
	else if (!strcmp(argv[1], "wsuspend"))
		p.sleep_event = HOST_SLEEP_EVENT_S3_WAKEABLE_SUSPEND;
	else if (!strcmp(argv[1], "resume"))
		p.sleep_event = HOST_SLEEP_EVENT_S3_RESUME;
	else if (!strcmp(argv[1], "freeze")) {
		p.sleep_event = HOST_SLEEP_EVENT_S0IX_SUSPEND;
		if (max_version >= 1) {
			p1.sleep_event = p.sleep_event;
			p1.reserved = 0;
			p1.suspend_params.sleep_timeout_ms =
				EC_HOST_SLEEP_TIMEOUT_DEFAULT;

			if (argc > 2) {
				p1.suspend_params.sleep_timeout_ms =
					strtoull(argv[2], &afterscan, 0);

				if ((*afterscan != '\0') ||
				    (afterscan == argv[2])) {
					fprintf(stderr, "Invalid value: %s\n",
						argv[2]);

					return -1;
				}
			}

			pp = &p1;
			psize = sizeof(p1);
			version = 1;
		}

	} else if (!strcmp(argv[1], "thaw")) {
		p.sleep_event = HOST_SLEEP_EVENT_S0IX_RESUME;
		if (max_version >= 1) {
			version = 1;
			rsize = sizeof(r);
		}
	} else {
		fprintf(stderr, "Unknown command: %s\n", argv[1]);
		return -1;
	}

	rv = ec_command(EC_CMD_HOST_SLEEP_EVENT, version, pp, psize, &r, rsize);
	if (rv < 0) {
		fprintf(stderr, "EC host sleep command failed: %d\n", rv);
		return rv;
	}

	if (rsize) {
		timeout = r.resume_response.sleep_transitions &
			  EC_HOST_RESUME_SLEEP_TIMEOUT;

		transitions = r.resume_response.sleep_transitions &
			      EC_HOST_RESUME_SLEEP_TRANSITIONS_MASK;

		printf("%s%d sleep line transitions.\n",
		       timeout ? "Timeout: " : "", transitions);
	}

	return 0;
}

int cmd_test(int argc, char *argv[])
{
	struct ec_params_test_protocol p = {
		.buf = { 1,  2,	 3,  4,	 5,  6,	 7,  8,	 9,  10, 11,
			 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
			 23, 24, 25, 26, 27, 28, 29, 30, 31, 32 }
	};
	struct ec_response_test_protocol r;
	int rv, version = 0;
	char *e;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s result length [version]\n", argv[0]);
		return -1;
	}

	p.ec_result = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "invalid param (result)\n");
		return -1;
	}
	p.ret_len = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "invalid param (length)\n");
		return -1;
	}

	if (argc > 3) {
		version = strtol(argv[3], &e, 0);
		if (e && *e) {
			fprintf(stderr, "invalid param (version)\n");
			return -1;
		}
	}

	rv = ec_command(EC_CMD_TEST_PROTOCOL, version, &p, sizeof(p), &r,
			sizeof(r));
	printf("rv = %d\n", rv);

	return rv;
}

int cmd_s5(int argc, char *argv[])
{
	struct ec_params_get_set_value p;
	struct ec_response_get_set_value r;
	int rv, param;

	p.flags = 0;

	if (argc > 1) {
		p.flags |= EC_GSV_SET;
		if (!parse_bool(argv[1], &param)) {
			fprintf(stderr, "invalid arg \"%s\"\n", argv[1]);
			return -1;
		}
		p.value = param;
	}

	rv = ec_command(EC_CMD_GSV_PAUSE_IN_S5, 0, &p, sizeof(p), &r,
			sizeof(r));
	if (rv > 0)
		printf("%s\n", r.value ? "on" : "off");

	return rv < 0;
}

static const char *const ec_feature_names[] = {
	[EC_FEATURE_LIMITED] = "Limited image, load RW for more",
	[EC_FEATURE_FLASH] = "Flash",
	[EC_FEATURE_PWM_FAN] = "Direct Fan power management",
	[EC_FEATURE_PWM_KEYB] = "Keyboard backlight",
	[EC_FEATURE_LIGHTBAR] = "Lightbar",
	[EC_FEATURE_LED] = "LED",
	[EC_FEATURE_MOTION_SENSE] = "Motion Sensors",
	[EC_FEATURE_KEYB] = "Keyboard",
	[EC_FEATURE_PSTORE] = "Host Permanent Storage",
	[EC_FEATURE_PORT80] = "BIOS Port 80h access",
	[EC_FEATURE_THERMAL] = "Thermal management",
	[EC_FEATURE_BKLIGHT_SWITCH] = "Switch backlight on/off",
	[EC_FEATURE_WIFI_SWITCH] = "Switch wifi on/off",
	[EC_FEATURE_HOST_EVENTS] = "Host event",
	[EC_FEATURE_GPIO] = "GPIO",
	[EC_FEATURE_I2C] = "I2C controller",
	[EC_FEATURE_CHARGER] = "Charger",
	[EC_FEATURE_BATTERY] = "Simple Battery",
	[EC_FEATURE_SMART_BATTERY] = "Smart Battery",
	[EC_FEATURE_HANG_DETECT] = "Host hang detection",
	[EC_FEATURE_PMU] = "Power Management",
	[EC_FEATURE_SUB_MCU] = "Control downstream MCU",
	[EC_FEATURE_USB_PD] = "USB Cros Power Delivery",
	[EC_FEATURE_USB_MUX] = "USB Multiplexer",
	[EC_FEATURE_MOTION_SENSE_FIFO] = "FIFO for Motion Sensors events",
	[EC_FEATURE_VSTORE] = "Temporary secure vstore",
	[EC_FEATURE_USBC_SS_MUX_VIRTUAL] = "Host-controlled USB-C SS mux",
	[EC_FEATURE_RTC] = "Real-time clock",
	[EC_FEATURE_FINGERPRINT] = "Fingerprint",
	[EC_FEATURE_TOUCHPAD] = "Touchpad",
	[EC_FEATURE_RWSIG] = "RWSIG task",
	[EC_FEATURE_DEVICE_EVENT] = "Device events reporting",
	[EC_FEATURE_UNIFIED_WAKE_MASKS] = "Unified wake masks for LPC/eSPI",
	[EC_FEATURE_HOST_EVENT64] = "64-bit host events",
	[EC_FEATURE_EXEC_IN_RAM] = "Execute code in RAM",
	[EC_FEATURE_CEC] = "Consumer Electronics Control",
	[EC_FEATURE_MOTION_SENSE_TIGHT_TIMESTAMPS] =
		"Tight timestamp for sensors events",
	[EC_FEATURE_REFINED_TABLET_MODE_HYSTERESIS] =
		"Refined tablet mode hysteresis",
	[EC_FEATURE_EFS2] = "Early Firmware Selection v2",
	[EC_FEATURE_ISH] = "Intel Integrated Sensor Hub",
	[EC_FEATURE_TYPEC_CMD] = "TCPMv2 Type-C commands",
	[EC_FEATURE_TYPEC_REQUIRE_AP_MODE_ENTRY] =
		"Host-controlled Type-C mode entry",
	[EC_FEATURE_TYPEC_MUX_REQUIRE_AP_ACK] =
		"AP ack for Type-C mux configuration",
	[EC_FEATURE_S4_RESIDENCY] = "S4 residency",
	[EC_FEATURE_TYPEC_AP_MUX_SET] = "AP directed mux sets",
	[EC_FEATURE_TYPEC_AP_VDM_SEND] = "AP directed VDM Request messages",
	[EC_FEATURE_SYSTEM_SAFE_MODE] = "System Safe Mode support",
	[EC_FEATURE_ASSERT_REBOOTS] = "Assert reboots",
	[EC_FEATURE_TOKENIZED_LOGGING] = "Tokenized Logging",
	[EC_FEATURE_AMD_STB_DUMP] = "AMD STB dump",
	[EC_FEATURE_MEMORY_DUMP] = "Memory Dump",
};

int cmd_inventory(int argc, char *argv[])
{
	struct ec_response_get_features r;
	int rv, i, j, idx;

	rv = ec_command(EC_CMD_GET_FEATURES, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("EC supported features:\n");
	for (i = 0, idx = 0; i < 2; i++) {
		for (j = 0; j < 32; j++, idx++) {
			if (r.flags[i] & BIT(j)) {
				if (idx >= ARRAY_SIZE(ec_feature_names) ||
				    !ec_feature_names[idx] ||
				    strlen(ec_feature_names[idx]) == 0)
					printf("%-4d: Unknown feature\n", idx);
				else
					printf("%-4d: %s support\n", idx,
					       ec_feature_names[idx]);
			}
		}
	}
	return 0;
}

static int get_cmdversions_v0(uint8_t cmd, uint32_t *version_mask)
{
	struct ec_params_get_cmd_versions p;
	struct ec_response_get_cmd_versions r;
	int rv;

	p.cmd = cmd;
	rv = ec_command(EC_CMD_GET_CMD_VERSIONS, 0, &p, sizeof(p), &r,
			sizeof(r));
	if (rv < 0) {
		if (rv == -EC_RES_INVALID_PARAM)
			printf("Command 0x%02x not supported by EC.\n", cmd);

		return rv;
	}

	*version_mask = r.version_mask;
	return 0;
}

int cmd_cmdversions(int argc, char *argv[])
{
	struct ec_params_get_cmd_versions_v1 p;
	struct ec_response_get_cmd_versions r;
	char *e;
	int cmd;
	int rv;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <cmd>\n", argv[0]);
		return -1;
	}
	cmd = strtol(argv[1], &e, 0);
	if ((e && *e) || cmd < 0 || cmd > 0xffff) {
		fprintf(stderr, "Bad command number.\n");
		return -1;
	}

	if (cmd > 0xff) {
		/* Ensure the EC support GET_CMD_VERSIONS v1. */
		rv = get_cmdversions_v0(EC_CMD_GET_CMD_VERSIONS,
					&r.version_mask);
		if (rv < 0)
			return rv;

		if (!(r.version_mask & EC_VER_MASK(1))) {
			printf("16 bits cmdversions not supported by EC.\n");
			return -1;
		}

		/* Use GET_CMD_VERSIONS v1. */
		p.cmd = cmd;
		rv = ec_command(EC_CMD_GET_CMD_VERSIONS, 1, &p, sizeof(p), &r,
				sizeof(r));
		if (rv < 0) {
			if (rv == -EC_RES_INVALID_PARAM)
				printf("Command 0x%02x not supported by EC.\n",
				       cmd);

			return rv;
		}
	} else {
		rv = get_cmdversions_v0(cmd, &r.version_mask);
		if (rv < 0)
			return rv;
	}

	printf("Command 0x%02x supports version mask 0x%08x\n", cmd,
	       r.version_mask);
	return 0;
}

/*
 * Convert a reset cause ID to human-readable string, providing total coverage
 * of the 'cause' space.  The returned string points to static storage and must
 * not be free()ed.
 */
static const char *reset_cause_to_str(uint16_t cause)
{
	static const char *const reset_causes[] = {
		"(reset unknown)",
		"reset: board custom",
		"reset: ap hang detected",
		"reset: console command",
		"reset: host command",
		"reset: keyboard sysreset",
		"reset: keyboard warm reboot",
		"reset: debug warm reboot",
		"reset: at AP's request",
		"reset: during EC initialization",
		"reset: AP watchdog",
	};
	BUILD_ASSERT(ARRAY_SIZE(reset_causes) == CHIPSET_RESET_COUNT);

	static const char *const shutdown_causes[] = {
		"shutdown: power failure",
		"shutdown: during EC initialization",
		"shutdown: board custom",
		"shutdown: battery voltage startup inhibit",
		"shutdown: power wait asserted",
		"shutdown: critical battery",
		"shutdown: by console command",
		"shutdown: entering G3",
		"shutdown: thermal",
		"shutdown: power button",
	};
	BUILD_ASSERT(ARRAY_SIZE(shutdown_causes) ==
		     CHIPSET_SHUTDOWN_COUNT - CHIPSET_SHUTDOWN_BEGIN);

	if (cause < CHIPSET_RESET_COUNT)
		return reset_causes[cause];

	if (cause < CHIPSET_SHUTDOWN_BEGIN)
		return "(reset unknown)";

	if (cause < CHIPSET_SHUTDOWN_COUNT)
		return shutdown_causes[cause - CHIPSET_SHUTDOWN_BEGIN];

	return "(shutdown unknown)";
}

int cmd_uptimeinfo(int argc, char *argv[])
{
	struct ec_response_uptime_info r;
	int rv;
	int i;
	int flag_count;
	uint32_t flag;
	static const char *const reset_flag_descs[] = {
#include "reset_flag_desc.inc"
	};

	if (argc != 1) {
		fprintf(stderr, "uptimeinfo takes no arguments");
		return -1;
	}

	rv = ec_command(EC_CMD_GET_UPTIME_INFO, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0) {
		fprintf(stderr, "ERROR: EC_CMD_GET_UPTIME_INFO failed; %d\n",
			rv);
		return rv;
	}

	printf("EC uptime: %d.%03d seconds\n", r.time_since_ec_boot_ms / 1000,
	       r.time_since_ec_boot_ms % 1000);

	printf("AP resets since EC boot: %d\n", r.ap_resets_since_ec_boot);

	printf("Most recent AP reset causes:\n");
	for (i = 0; i != ARRAY_SIZE(r.recent_ap_reset); ++i) {
		if (r.recent_ap_reset[i].reset_time_ms == 0)
			continue;

		printf("\t%d.%03d: %s\n",
		       r.recent_ap_reset[i].reset_time_ms / 1000,
		       r.recent_ap_reset[i].reset_time_ms % 1000,
		       reset_cause_to_str(r.recent_ap_reset[i].reset_cause));
	}

	printf("EC reset flags at last EC boot: ");

	if (!r.ec_reset_flags) {
		printf("unknown\n");
		return 0;
	}

	flag_count = 0;
	for (flag = 0; flag < ARRAY_SIZE(reset_flag_descs); ++flag) {
		if ((r.ec_reset_flags & BIT(flag)) != 0) {
			if (flag_count)
				printf(" | ");
			printf(reset_flag_descs[flag]);
			flag_count++;
		}
	}

	if (r.ec_reset_flags >= BIT(flag)) {
		if (flag_count)
			printf(" | ");
		printf("no-desc");
	}
	printf("\n");
	return 0;
}

int cmd_version(int argc, char *argv[])
{
	struct ec_response_get_version_v1 r;
	char *build_string = (char *)ec_inbuf;
	int rv;

	if (ec_cmd_version_supported(EC_CMD_GET_VERSION, 1)) {
		rv = ec_command(EC_CMD_GET_VERSION, 1, NULL, 0, &r,
				sizeof(struct ec_response_get_version_v1));
	} else {
		/* Fall-back to version 0 if version 1 is not supported */
		rv = ec_command(EC_CMD_GET_VERSION, 0, NULL, 0, &r,
				sizeof(struct ec_response_get_version));
		/* These fields are not supported in version 0, ensure empty */
		r.cros_fwid_ro[0] = '\0';
		r.cros_fwid_rw[0] = '\0';
	}
	if (rv < 0) {
		fprintf(stderr, "ERROR: EC_CMD_GET_VERSION failed: %d\n", rv);
		goto exit;
	}

	rv = ec_command(EC_CMD_GET_BUILD_INFO, 0, NULL, 0, ec_inbuf,
			ec_max_insize);
	if (rv < 0) {
		fprintf(stderr, "ERROR: EC_CMD_GET_BUILD_INFO failed: %d\n",
			rv);
		goto exit;
	}

	rv = 0;

	/* Ensure versions are null-terminated before we print them */
	r.version_string_ro[sizeof(r.version_string_ro) - 1] = '\0';
	r.version_string_rw[sizeof(r.version_string_rw) - 1] = '\0';
	build_string[ec_max_insize - 1] = '\0';
	r.cros_fwid_ro[sizeof(r.cros_fwid_ro) - 1] = '\0';
	r.cros_fwid_rw[sizeof(r.cros_fwid_rw) - 1] = '\0';
	/* Print versions */
	printf("RO version:    %s\n", r.version_string_ro);
	if (strlen(r.cros_fwid_ro))
		printf("RO cros fwid:  %s\n", r.cros_fwid_ro);
	printf("RW version:    %s\n", r.version_string_rw);
	if (strlen(r.cros_fwid_rw))
		printf("RW cros fwid:  %s\n", r.cros_fwid_rw);
	printf("Firmware copy: %s\n",
	       (r.current_image < ARRAY_SIZE(image_names) ?
			image_names[r.current_image] :
			"?"));
	printf("Build info:    %s\n", build_string);
exit:
	printf("Tool version:  %s %s %s\n", CROS_ECTOOL_VERSION, DATE, BUILDER);

	return rv;
}

int cmd_reboot_ec(int argc, char *argv[])
{
	struct ec_params_reboot_ec p;
	int rv, i;

	if (argc < 2) {
		/*
		 * No params specified so tell the EC to reboot immediately.
		 * That reboots the AP as well, so unlikely we'll be around
		 * to see a return code from this...
		 */
		rv = ec_command(EC_CMD_REBOOT, 0, NULL, 0, NULL, 0);
		return (rv < 0 ? rv : 0);
	}

	/* Parse command */
	if (!strcmp(argv[1], "cancel"))
		p.cmd = EC_REBOOT_CANCEL;
	else if (!strcmp(argv[1], "RO"))
		p.cmd = EC_REBOOT_JUMP_RO;
	else if (!strcmp(argv[1], "RW"))
		p.cmd = EC_REBOOT_JUMP_RW;
	else if (!strcmp(argv[1], "cold"))
		p.cmd = EC_REBOOT_COLD;
	else if (!strcmp(argv[1], "disable-jump"))
		p.cmd = EC_REBOOT_DISABLE_JUMP;
	else if (!strcmp(argv[1], "hibernate"))
		p.cmd = EC_REBOOT_HIBERNATE;
	else if (!strcmp(argv[1], "hibernate-clear-ap-off")) {
		p.cmd = EC_REBOOT_HIBERNATE_CLEAR_AP_OFF;
		fprintf(stderr, "hibernate-clear-ap-off is deprecated.\n"
				"Use hibernate and clear-ap-idle, instead.\n");
	} else if (!strcmp(argv[1], "cold-ap-off"))
		p.cmd = EC_REBOOT_COLD_AP_OFF;
	else {
		fprintf(stderr, "Unknown command: %s\n", argv[1]);
		return -1;
	}

	/* Parse flags, if any */
	p.flags = 0;
	for (i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "at-shutdown")) {
			p.flags |= EC_REBOOT_FLAG_ON_AP_SHUTDOWN;
		} else if (!strcmp(argv[i], "switch-slot")) {
			p.flags |= EC_REBOOT_FLAG_SWITCH_RW_SLOT;
		} else if (!strcmp(argv[i], "clear-ap-idle")) {
			p.flags |= EC_REBOOT_FLAG_CLEAR_AP_IDLE;
		} else {
			fprintf(stderr, "Unknown flag: %s\n", argv[i]);
			return -1;
		}
	}

	rv = ec_command(EC_CMD_REBOOT_EC, 0, &p, sizeof(p), NULL, 0);
	return (rv < 0 ? rv : 0);
}

int cmd_reboot_ap_on_g3(int argc, char *argv[])
{
	struct ec_params_reboot_ap_on_g3_v1 p;
	int rv;
	char *e;
	int cmdver;

	if (argc < 2) {
		p.reboot_ap_at_g3_delay = 0;
	} else {
		p.reboot_ap_at_g3_delay = strtol(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "invalid number\n");
			return -1;
		}
	}
	if (ec_cmd_version_supported(EC_CMD_REBOOT_AP_ON_G3, 1))
		cmdver = 1;
	else
		cmdver = 0;

	rv = ec_command(EC_CMD_REBOOT_AP_ON_G3, cmdver, &p, sizeof(p), NULL, 0);
	return (rv < 0 ? rv : 0);
}

static void cmd_rgbkbd_help(char *cmd)
{
	fprintf(stderr,
		"  Usage1: %s <key> <RGB> [<RGB> ...]\n"
		"          Set the color of <key> to <RGB>. Multiple colors for\n"
		"          adjacent keys can be set at once.\n"
		"\n"
		"  Usage2: %s clear <RGB>\n"
		"          Set the color of all keys to <RGB>.\n"
		"\n"
		"  Usage3: %s demo <num>\n"
		"          Run demo-<num>. 0: Off, 1: Flow, 2: Dot.\n"
		"\n"
		"  Usage4: %s scale <key> <val>\n"
		"          Set the scale parameter of key_<key> to <val>.\n"
		"          <val> is a 24-bit integer where scale values are encoded\n"
		"          as R=23:16, G=15:8, B=7:0.\n"
		"\n"
		"  Usage5: %s getconfig\n"
		"          Get the HW config supported.\n"
		"\n",
		cmd, cmd, cmd, cmd, cmd);
}

static int cmd_rgbkbd_parse_rgb_text(const char *text, struct rgb_s *color)
{
	uint32_t rgb;
	char *e;

	rgb = strtoul(text, &e, 0);
	if ((e && *e) || rgb > EC_RGBKBD_MAX_RGB_COLOR) {
		fprintf(stderr, "Invalid color '%s'.\n", text);
		return -1;
	}
	color->r = (rgb >> 16) & 0xff;
	color->g = (rgb >> 8) & 0xff;
	color->b = (rgb >> 0) & 0xff;

	return 0;
}

static int cmd_rgbkbd_set_color(int argc, char *argv[])
{
	struct ec_params_rgbkbd_set_color *p;
	int i, key, outlen;
	char *e;
	int rv = -1;

	outlen = sizeof(*p) + sizeof(struct rgb_s) * EC_RGBKBD_MAX_KEY_COUNT;
	p = (struct ec_params_rgbkbd_set_color *)malloc(outlen);
	if (p == NULL)
		return -1;
	memset(p, 0, outlen);

	key = strtol(argv[1], &e, 0);
	if ((e && *e) || key >= EC_RGBKBD_MAX_KEY_COUNT) {
		fprintf(stderr, "Invalid key ID '%s'.\n", argv[1]);
		goto out;
	}
	p->start_key = key;

	if (argc - 2 > EC_RGBKBD_MAX_KEY_COUNT) {
		fprintf(stderr, "# of colors exceed max key count.\n");
		goto out;
	}

	for (i = 2; i < argc; i++) {
		if (cmd_rgbkbd_parse_rgb_text(argv[i], &p->color[p->length]))
			goto out;
		p->length++;
	}

	outlen = sizeof(*p) + sizeof(struct rgb_s) * p->length;
	rv = ec_command(EC_CMD_RGBKBD_SET_COLOR, 0, p, outlen, NULL, 0);

out:
	free(p);

	return rv;
}

static int cmd_rgbkbd(int argc, char *argv[])
{
	int val;
	char *e;
	int rv = -1;
	struct ec_params_rgbkbd p;
	struct ec_response_rgbkbd r;

	if (argc < 2) {
		cmd_rgbkbd_help(argv[0]);
		return -1;
	}

	if (argc == 3 && !strcasecmp(argv[1], "clear")) {
		/* Usage 2 */
		p.subcmd = EC_RGBKBD_SUBCMD_CLEAR;
		if (cmd_rgbkbd_parse_rgb_text(argv[2], &p.color))
			return -1;

		rv = ec_command(EC_CMD_RGBKBD, 0, &p, sizeof(p), &r, sizeof(r));
	} else if (argc == 3 && !strcasecmp(argv[1], "demo")) {
		/* Usage 3 */
		val = strtol(argv[2], &e, 0);
		if ((e && *e) || val >= EC_RGBKBD_DEMO_COUNT) {
			fprintf(stderr, "Invalid demo id: %s\n", argv[2]);
			return -1;
		}
		p.subcmd = EC_RGBKBD_SUBCMD_DEMO;
		p.demo = val;
		rv = ec_command(EC_CMD_RGBKBD, 0, &p, sizeof(p), &r, sizeof(r));
	} else if (argc == 4 && !strcasecmp(argv[1], "scale")) {
		/* Usage 4 */
		val = strtol(argv[2], &e, 0);
		if ((e && *e) || val > EC_RGBKBD_MAX_KEY_COUNT) {
			fprintf(stderr, "Invalid key number: %s\n", argv[2]);
			return -1;
		}
		p.set_scale.key = val;
		if (cmd_rgbkbd_parse_rgb_text(argv[3], &p.set_scale.scale)) {
			fprintf(stderr, "Invalid scale value: %s\n", argv[3]);
			return -1;
		}
		p.subcmd = EC_RGBKBD_SUBCMD_SET_SCALE;
		rv = ec_command(EC_CMD_RGBKBD, 0, &p, sizeof(p), &r, sizeof(r));
	} else if (argc == 2 && !strcasecmp(argv[1], "getconfig")) {
		/* Usage 5 */
		const char *type;

		p.subcmd = EC_RGBKBD_SUBCMD_GET_CONFIG;
		rv = ec_command(EC_CMD_RGBKBD, 0, &p, sizeof(p), &r, sizeof(r));

		if (rv < 0)
			return rv;

		switch ((enum ec_rgbkbd_type)r.rgbkbd_type) {
		case EC_RGBKBD_TYPE_PER_KEY:
			type = "EC_RGBKBD_TYPE_PER_KEY";
			break;
		case EC_RGBKBD_TYPE_FOUR_ZONES_40_LEDS:
			type = "EC_RGBKBD_TYPE_FOUR_ZONES_40_LEDS";
			break;
		case EC_RGBKBD_TYPE_FOUR_ZONES_12_LEDS:
			type = "EC_RGBKBD_TYPE_FOUR_ZONES_12_LEDS";
			break;
		case EC_RGBKBD_TYPE_FOUR_ZONES_4_LEDS:
			type = "EC_RGBKBD_TYPE_FOUR_ZONES_4_LEDS";
			break;
		default:
			type = "EC_RGBKBD_TYPE_UNKNOWN";
		}

		printf("RGBKBD_TYPE: %s\n", type);
	} else {
		/* Usage 1 */
		rv = cmd_rgbkbd_set_color(argc, argv);
	}

	return (rv < 0 ? rv : 0);
}

int cmd_button(int argc, char *argv[])
{
	struct ec_params_button p;
	char *e;
	int argv_idx;
	int button = KEYBOARD_BUTTON_COUNT;
	int rv;

	if (argc < 2) {
		fprintf(stderr, "Invalid num param %d.\n", argc);
		return -1;
	}

	p.press_ms = 50;
	p.btn_mask = 0;

	for (argv_idx = 1; argv_idx < argc; argv_idx++) {
		if (!strcasecmp(argv[argv_idx], "vup"))
			button = KEYBOARD_BUTTON_VOLUME_UP;
		else if (!strcasecmp(argv[argv_idx], "vdown"))
			button = KEYBOARD_BUTTON_VOLUME_DOWN;
		else if (!strcasecmp(argv[argv_idx], "rec"))
			button = KEYBOARD_BUTTON_RECOVERY;
		else {
			/* If last parameter check if it is an integer. */
			if (argv_idx == argc - 1) {
				p.press_ms = strtol(argv[argv_idx], &e, 0);
				/* If integer, break out of the loop. */
				if (!*e)
					break;
			}
			button = KEYBOARD_BUTTON_COUNT;
		}

		if (button == KEYBOARD_BUTTON_COUNT) {
			fprintf(stderr, "Invalid button input.\n");
			return -1;
		}

		p.btn_mask |= (1 << button);
	}
	if (!p.btn_mask)
		return 0;

	rv = ec_command(EC_CMD_BUTTON, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("Button(s) %d set to %d ms\n", p.btn_mask, p.press_ms);
	return 0;
}

int cmd_flash_info(int argc, char *argv[])
{
	struct ec_response_flash_info_1 r;
	int cmdver = 1;
	int rsize = sizeof(r);
	int rv;

	memset(&r, 0, sizeof(r));

	if (!ec_cmd_version_supported(EC_CMD_FLASH_INFO, cmdver)) {
		/* Fall back to version 0 command */
		cmdver = 0;
		rsize = sizeof(struct ec_response_flash_info);
	}

	rv = ec_command(EC_CMD_FLASH_INFO, cmdver, NULL, 0, &r, rsize);
	if (rv < 0)
		return rv;

	printf("FlashSize %d\nWriteSize %d\nEraseSize %d\nProtectSize %d\n",
	       r.flash_size, r.write_block_size, r.erase_block_size,
	       r.protect_block_size);

	if (cmdver >= 1) {
		/* Fields added in ver.1 available */
		printf("WriteIdealSize %d\nFlags 0x%x\n", r.write_ideal_size,
		       r.flags);
	}

	return 0;
}

int cmd_rand(int argc, char *argv[])
{
	int64_t num_bytes;
	int64_t i;
	char *e;
	int rv = 0;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <num_bytes>\n", argv[0]);
		return -1;
	}

	num_bytes = strtol(argv[1], &e, 0);
	if ((e && *e) || (errno == ERANGE)) {
		fprintf(stderr, "Invalid num_bytes argument\n");
		return -1;
	}

	for (i = 0; i < num_bytes; i += ec_max_insize) {
		uint16_t num_rand_bytes = ec_max_insize;
		if (num_bytes - i < num_rand_bytes)
			num_rand_bytes = num_bytes - i;

		ec::RandNumCommand rand_num_command(num_rand_bytes);
		if (!rand_num_command.Run(comm_get_fd())) {
			int rv = -EECRESULT - rand_num_command.Result();
			fprintf(stderr, "Rand Num returned with errors: %d\n",
				rv);
			return rv;
		}

		rv = write(STDOUT_FILENO,
			   rand_num_command.GetRandNumData().data(),
			   num_rand_bytes);
		if (rv != num_rand_bytes) {
			fprintf(stderr, "Failed to write stdout\n");
			return -1;
		}
	}

	return 0;
}

int cmd_flash_spi_info(int argc, char *argv[])
{
	struct ec_response_flash_spi_info r;
	int rv;

	memset(&r, 0, sizeof(r));

	/* Print SPI flash info if available */
	if (!ec_cmd_version_supported(EC_CMD_FLASH_SPI_INFO, 0)) {
		printf("EC has no info (does not use SPI flash?)\n");
		return -1;
	}

	rv = ec_command(EC_CMD_FLASH_SPI_INFO, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("JEDECManufacturerID 0x%02x\n", r.jedec[0]);
	printf("JEDECDeviceID 0x%02x 0x%02x\n", r.jedec[1], r.jedec[2]);
	printf("JEDECCapacity %d\n", 1 << r.jedec[2]);
	printf("ManufacturerID 0x%02x\n", r.mfr_dev_id[0]);
	printf("DeviceID 0x%02x\n", r.mfr_dev_id[1]);
	printf("StatusRegister1 0x%02x\n", r.sr1);
	printf("StatusRegister2 0x%02x\n", r.sr2);
	return 0;
}

int cmd_flash_read(int argc, char *argv[])
{
	int offset, size;
	int rv;
	char *e;
	uint8_t *buf;

	if (argc < 4) {
		fprintf(stderr, "Usage: %s <offset> <size> <filename>\n",
			argv[0]);
		return -1;
	}
	offset = strtol(argv[1], &e, 0);
	if ((e && *e) || offset < 0 || offset > MAX_FLASH_SIZE) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}
	size = strtol(argv[2], &e, 0);
	if ((e && *e) || size <= 0 || size > MAX_FLASH_SIZE) {
		fprintf(stderr, "Bad size.\n");
		return -1;
	}
	printf("Reading %d bytes at offset %d...\n", size, offset);

	buf = (uint8_t *)malloc(size);
	if (!buf) {
		fprintf(stderr, "Unable to allocate buffer.\n");
		return -1;
	}

	/* Read data in chunks */
	rv = ec_flash_read(buf, offset, size);
	if (rv < 0) {
		free(buf);
		return rv;
	}

	rv = write_file(argv[3], (const char *)(buf), size);
	free(buf);
	if (rv)
		return rv;

	printf("done.\n");
	return 0;
}

int cmd_flash_write(int argc, char *argv[])
{
	int offset, size;
	int rv;
	char *e;
	char *buf;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <offset> <filename>\n", argv[0]);
		return -1;
	}

	offset = strtol(argv[1], &e, 0);
	if ((e && *e) || offset < 0 || offset > MAX_FLASH_SIZE) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}

	/* Read the input file */
	buf = read_file(argv[2], &size);
	if (!buf)
		return -1;

	printf("Writing to offset %d...\n", offset);

	/* Write data in chunks */
	rv = ec_flash_write((const uint8_t *)(buf), offset, size);

	free(buf);

	if (rv < 0)
		return rv;

	printf("done.\n");
	return 0;
}

int cmd_flash_erase(int argc, char *argv[])
{
	int offset, size;
	char *e;
	int rv;
	bool async = false;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <offset> <size>\n", argv[0]);
		return -1;
	}

	if (strcmp(argv[0], "flasheraseasync") == 0)
		async = true;

	offset = strtol(argv[1], &e, 0);
	if ((e && *e) || offset < 0 || offset > MAX_FLASH_SIZE) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}

	size = strtol(argv[2], &e, 0);
	if ((e && *e) || size <= 0 || size > MAX_FLASH_SIZE) {
		fprintf(stderr, "Bad size.\n");
		return -1;
	}

	printf("Erasing %d bytes at offset %d...\n", size, offset);
	if (async)
		rv = ec_flash_erase_async(offset, size);
	else
		rv = ec_flash_erase(offset, size);
	if (rv < 0)
		return rv;

	printf("done.\n");
	return 0;
}

int cmd_flash_protect(int argc, char *argv[])
{
	/*
	 * Set up requested flags.  If no flags were specified, mask will
	 * be flash_protect::Flags::kNone and nothing will change.
	 */
	ec::flash_protect::Flags flags = ec::flash_protect::Flags::kNone;
	ec::flash_protect::Flags mask = ec::flash_protect::Flags::kNone;

	for (int i = 1; i < argc; i++) {
		if (!strcasecmp(argv[i], "now")) {
			mask |= ec::flash_protect::Flags::kAllNow;
			flags |= ec::flash_protect::Flags::kAllNow;
		} else if (!strcasecmp(argv[i], "enable")) {
			mask |= ec::flash_protect::Flags::kRoAtBoot;
			flags |= ec::flash_protect::Flags::kRoAtBoot;
		} else if (!strcasecmp(argv[i], "disable"))
			mask |= ec::flash_protect::Flags::kRoAtBoot;
	}

	// TODO(b/287519577) Use FlashProtectCommandFactory after removing its
	// dependency on CrosFpDeviceInterface.
	uint32_t version = 1;
	ec::VersionsCommand flash_protect_versions_command(
		EC_CMD_FLASH_PROTECT);

	if (!flash_protect_versions_command.RunWithMultipleAttempts(
		    comm_get_fd(), 20)) {
		fprintf(stderr, "Flash Protect Versions Command failed:\n");
		return -1;
	}

	if (flash_protect_versions_command.IsVersionSupported(2) ==
	    ec::EcCmdVersionSupportStatus::SUPPORTED) {
		version = 2;
	}

	ec::FlashProtectCommand flash_protect_command(flags, mask, version);
	if (!flash_protect_command.Run(comm_get_fd())) {
		int rv = -EECRESULT - flash_protect_command.Result();
		fprintf(stderr, "Flash protect returned with errors: %d\n", rv);
		return rv;
	}

	/* Print returned flags */
	printf("Flash protect flags: 0x%08x%s\n",
	       static_cast<int>(flash_protect_command.GetFlags()),
	       (ec::FlashProtectCommand::ParseFlags(
			flash_protect_command.GetFlags()))
		       .c_str());
	printf("Valid flags:         0x%08x%s\n",
	       static_cast<int>(flash_protect_command.GetValidFlags()),
	       (ec::FlashProtectCommand::ParseFlags(
			flash_protect_command.GetValidFlags()))
		       .c_str());
	printf("Writable flags:      0x%08x%s\n",
	       static_cast<int>(flash_protect_command.GetWritableFlags()),

	       (ec::FlashProtectCommand::ParseFlags(
			flash_protect_command.GetWritableFlags()))
		       .c_str());

	/* Check if we got all the flags we asked for */
	if ((flash_protect_command.GetFlags() & mask) != (flags & mask)) {
		fprintf(stderr,
			"Unable to set requested flags "
			"(wanted mask 0x%08x flags 0x%08x)\n",
			static_cast<int>(mask), static_cast<int>(flags));
		if ((mask & ~flash_protect_command.GetWritableFlags()) !=
		    ec::flash_protect::Flags::kNone)
			fprintf(stderr,
				"Which is expected, because writable "
				"mask is 0x%08x.\n",
				static_cast<int>(flash_protect_command
							 .GetWritableFlags()));

		return -1;
	}

	return 0;
}

int cmd_rw_hash_pd(int argc, char *argv[])
{
	struct ec_params_usb_pd_rw_hash_entry *p =
		(struct ec_params_usb_pd_rw_hash_entry *)ec_outbuf;
	int i, rv;
	char *e;
	uint32_t val;
	uint8_t *rwp;

	if (argc < 7) {
		fprintf(stderr, "Usage: %s <dev_id> <HASH[0]> ... <HASH[4]>\n",
			argv[0]);
		return -1;
	}

	p->dev_id = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad device ID\n");
		return -1;
	}

	rwp = p->dev_rw_hash;
	for (i = 2; i < 7; i++) {
		val = strtol(argv[i], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad RW hash\n");
			return -1;
		}
		rwp[0] = (uint8_t)(val >> 0) & 0xff;
		rwp[1] = (uint8_t)(val >> 8) & 0xff;
		rwp[2] = (uint8_t)(val >> 16) & 0xff;
		rwp[3] = (uint8_t)(val >> 24) & 0xff;
		rwp += 4;
	}
	rv = ec_command(EC_CMD_USB_PD_RW_HASH_ENTRY, 0, p, sizeof(*p), NULL, 0);

	return rv;
}

int cmd_rwsig_status(int argc, char *argv[])
{
	int rv;
	struct ec_response_rwsig_check_status resp;

	rv = ec_command(EC_CMD_RWSIG_CHECK_STATUS, 0, NULL, 0, &resp,
			sizeof(resp));
	if (rv < 0)
		return rv;

	printf("RW signature check: %s\n", resp.status ? "OK" : "FAILED");

	return 0;
}

static int rwsig_action(const char *command)
{
	struct ec_params_rwsig_action req;

	if (!strcasecmp(command, "abort"))
		req.action = RWSIG_ACTION_ABORT;
	else if (!strcasecmp(command, "continue"))
		req.action = RWSIG_ACTION_CONTINUE;
	else
		return -1;

	return ec_command(EC_CMD_RWSIG_ACTION, 0, &req, sizeof(req), NULL, 0);
}

int cmd_rwsig_action_legacy(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s [abort | continue]\n", argv[0]);
		return -1;
	}

	return rwsig_action(argv[1]);
}

int cmd_rwsig_action(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: ectool rwsig action [abort | "
				"continue]\n");
		return -1;
	}

	return rwsig_action(argv[1]);
}

enum rwsig_info_fields {
	RWSIG_INFO_FIELD_SIG_ALG = BIT(0),
	RWSIG_INFO_FIELD_KEY_VERSION = BIT(1),
	RWSIG_INFO_FIELD_HASH_ALG = BIT(2),
	RWSIG_INFO_FIELD_KEY_IS_VALID = BIT(3),
	RWSIG_INFO_FIELD_KEY_ID = BIT(4),
	RWSIG_INFO_FIELD_ALL =
		RWSIG_INFO_FIELD_SIG_ALG | RWSIG_INFO_FIELD_KEY_VERSION |
		RWSIG_INFO_FIELD_HASH_ALG | RWSIG_INFO_FIELD_KEY_IS_VALID |
		RWSIG_INFO_FIELD_KEY_ID
};

static int rwsig_info(enum rwsig_info_fields fields)
{
	int i;
	int rv;
	struct ec_response_rwsig_info r;
	bool print_prefix = false;

	rv = ec_command(EC_CMD_RWSIG_INFO, EC_VER_RWSIG_INFO, NULL, 0, &r,
			sizeof(r));
	if (rv < 0) {
		fprintf(stderr, "rwsig info command failed\n");
		return -1;
	}

	if ((fields & RWSIG_INFO_FIELD_ALL) == RWSIG_INFO_FIELD_ALL)
		print_prefix = true;

	if (fields & RWSIG_INFO_FIELD_SIG_ALG) {
		if (print_prefix)
			printf("sig_alg: ");

		printf("%d\n", r.sig_alg);
	}
	if (fields & RWSIG_INFO_FIELD_KEY_VERSION) {
		if (print_prefix)
			printf("key_version: ");

		printf("%d\n", r.key_version);
	}
	if (fields & RWSIG_INFO_FIELD_HASH_ALG) {
		if (print_prefix)
			printf("hash_alg: ");

		printf("%d\n", r.hash_alg);
	}
	if (fields & RWSIG_INFO_FIELD_KEY_IS_VALID) {
		if (print_prefix)
			printf("key_is_valid: ");

		printf("%d\n", r.key_is_valid);
	}
	if (fields & RWSIG_INFO_FIELD_KEY_ID) {
		if (print_prefix)
			printf("key_id: ");

		for (i = 0; i < sizeof(r.key_id); i++)
			printf("%02x", r.key_id[i]);
		printf("\n");
	}

	return 0;
}

static int cmd_rwsig_info(int argc, char *argv[])
{
	int i;

	struct rwsig_dump_cmds {
		const char *cmd;
		enum rwsig_info_fields field;
	};

	struct rwsig_dump_cmds cmd_map[] = {
		{ "sig_alg", RWSIG_INFO_FIELD_SIG_ALG },
		{ "key_version", RWSIG_INFO_FIELD_KEY_VERSION },
		{ "hash_alg", RWSIG_INFO_FIELD_HASH_ALG },
		{ "key_valid", RWSIG_INFO_FIELD_KEY_IS_VALID },
		{ "key_id", RWSIG_INFO_FIELD_KEY_ID },
	};

	if (argc == 0)
		return -1;

	if (strcmp(argv[0], "info") == 0)
		return rwsig_info(RWSIG_INFO_FIELD_ALL);

	if (strcmp(argv[0], "dump") == 0) {
		if (argc != 2) {
			fprintf(stderr,
				"Usage: rwsig dump "
				"[sig_alg|key_version|hash_alg|key_valid|key_id]\n");
			return -1;
		}
		for (i = 0; i < ARRAY_SIZE(cmd_map); i++)
			if (strcmp(argv[1], cmd_map[i].cmd) == 0)
				return rwsig_info(cmd_map[i].field);

		return -1;
	}

	return -1;
}

int cmd_rwsig(int argc, char **argv)
{
	struct rwsig_subcommand {
		const char *subcommand;
		int (*handler)(int argc, char *argv[]);
	};

	const struct rwsig_subcommand rwsig_subcommands[] = {
		{ "info", cmd_rwsig_info },
		{ "dump", cmd_rwsig_info },
		{ "action", cmd_rwsig_action },
		{ "status", cmd_rwsig_status }
	};

	int i;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <info|dump|action|status>\n",
			argv[0]);
		return -1;
	}

	for (i = 0; i < ARRAY_SIZE(rwsig_subcommands); i++)
		if (strcmp(argv[1], rwsig_subcommands[i].subcommand) == 0)
			return rwsig_subcommands[i].handler(--argc, &argv[1]);

	return -1;
}

enum sysinfo_fields {
	SYSINFO_FIELD_NONE = 0,
	SYSINFO_FIELD_RESET_FLAGS = BIT(0),
	SYSINFO_FIELD_CURRENT_IMAGE = BIT(1),
	SYSINFO_FIELD_FLAGS = BIT(2),
	SYSINFO_INFO_FIELD_ALL = SYSINFO_FIELD_RESET_FLAGS |
				 SYSINFO_FIELD_CURRENT_IMAGE |
				 SYSINFO_FIELD_FLAGS
};

static int sysinfo(struct ec_response_sysinfo *info)
{
	int rv;

	rv = ec_command(EC_CMD_SYSINFO, 0, NULL, 0, info, sizeof(*info));
	if (rv < 0) {
		fprintf(stderr, "ERROR: EC_CMD_SYSINFO failed: %d\n", rv);
		return rv;
	}

	return 0;
}

int cmd_sysinfo(int argc, char **argv)
{
	struct ec_response_sysinfo r;
	enum sysinfo_fields fields = SYSINFO_FIELD_NONE;
	bool print_prefix = false;

	if (argc != 1 && argc != 2)
		goto sysinfo_error_usage;

	if (argc == 1) {
		fields = SYSINFO_INFO_FIELD_ALL;
		print_prefix = true;
	} else if (argc == 2) {
		if (strcmp(argv[1], "flags") == 0)
			fields = SYSINFO_FIELD_FLAGS;
		else if (strcmp(argv[1], "reset_flags") == 0)
			fields = SYSINFO_FIELD_RESET_FLAGS;
		else if (strcmp(argv[1], "firmware_copy") == 0)
			fields = SYSINFO_FIELD_CURRENT_IMAGE;
		else
			goto sysinfo_error_usage;
	}

	memset(&r, '\0', sizeof(r));
	if (sysinfo(&r) != 0)
		return -1;

	if (fields & SYSINFO_FIELD_RESET_FLAGS) {
		if (print_prefix)
			printf("Reset flags: ");
		printf("0x%08x\n", r.reset_flags);
	}

	if (fields & SYSINFO_FIELD_FLAGS) {
		if (print_prefix)
			printf("Flags: ");
		printf("0x%08x\n", r.flags);
	}

	if (fields & SYSINFO_FIELD_CURRENT_IMAGE) {
		if (print_prefix)
			printf("Firmware copy: ");
		printf("%d\n", r.current_image);
	}

	return 0;

sysinfo_error_usage:
	fprintf(stderr,
		"Usage: %s "
		"[flags|reset_flags|firmware_copy]\n",
		argv[0]);
	return -1;
}

int cmd_rollback_info(int argc, char *argv[])
{
	struct ec_response_rollback_info r;
	int rv;

	rv = ec_command(EC_CMD_ROLLBACK_INFO, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0) {
		fprintf(stderr, "ERROR: EC_CMD_ROLLBACK_INFO failed: %d\n", rv);
		return rv;
	}

	/* Print versions */
	printf("Rollback block id:    %d\n", r.id);
	printf("Rollback min version: %d\n", r.rollback_min_version);
	printf("RW rollback version:  %d\n", r.rw_rollback_version);

	return 0;
}

int cmd_apreset(int argc, char *argv[])
{
	return ec_command(EC_CMD_AP_RESET, 0, NULL, 0, NULL, 0);
}

#define FP_FRAME_INDEX_SIMPLE_IMAGE -1

/*
 * Download a frame buffer from the FPMCU.
 *
 * Might be either the finger image or a finger template depending on 'index'.
 *
 * @param info a pointer to store the struct ec_response_fp_info retrieved by
 * this command.
 * @param index the specific frame to retrieve, might be:
 *  -1 (aka FP_FRAME_INDEX_SIMPLE_IMAGE) for the a single grayscale image.
 *   0  (aka FP_FRAME_INDEX_RAW_IMAGE) for the full vendor raw finger image.
 *   1..n for a finger template.
 *
 * @returns a pointer to the buffer allocated to contain the frame or NULL
 * if case of error. The caller must call free() once it no longer needs the
 * buffer.
 */
static void *fp_download_frame(struct ec_response_fp_info *info, int index)
{
	struct ec_params_fp_frame p;
	int rv = 0;
	size_t stride, size;
	void *buffer;
	uint8_t *ptr;
	int cmdver = ec_cmd_version_supported(EC_CMD_FP_INFO, 1) ? 1 : 0;
	int rsize = cmdver == 1 ? sizeof(*info) :
				  sizeof(struct ec_response_fp_info_v0);
	const int max_attempts = 3;
	int num_attempts;

	/* templates not supported in command v0 */
	if (index > 0 && cmdver == 0)
		return NULL;

	rv = ec_command(EC_CMD_FP_INFO, cmdver, NULL, 0, info, rsize);
	if (rv < 0)
		return NULL;

	if (index == FP_FRAME_INDEX_SIMPLE_IMAGE) {
		size = (size_t)info->width * info->bpp / 8 * info->height;
		index = FP_FRAME_INDEX_RAW_IMAGE;
	} else if (index == FP_FRAME_INDEX_RAW_IMAGE) {
		size = info->frame_size;
	} else {
		size = info->template_size;
	}

	buffer = malloc(size);
	if (!buffer) {
		fprintf(stderr, "Cannot allocate memory for the image\n");
		return NULL;
	}

	ptr = (uint8_t *)(buffer);
	p.offset = index << FP_FRAME_INDEX_SHIFT;
	while (size) {
		stride = MIN(ec_max_insize, size);
		p.size = stride;
		num_attempts = 0;
		while (num_attempts < max_attempts) {
			num_attempts++;
			rv = ec_command(EC_CMD_FP_FRAME, 0, &p, sizeof(p), ptr,
					stride);
			if (rv >= 0)
				break;
			if (rv == -EECRESULT - EC_RES_ACCESS_DENIED)
				break;
			usleep(100000);
		}
		if (rv < 0) {
			free(buffer);
			return NULL;
		}
		p.offset += stride;
		size -= stride;
		ptr += stride;
	}

	return buffer;
}

int cmd_fp_mode(int argc, char *argv[])
{
	struct ec_params_fp_mode p;
	struct ec_response_fp_mode r;
	uint32_t mode = 0;
	uint32_t capture_type = FP_CAPTURE_SIMPLE_IMAGE;
	int i, rv;

	if (argc == 1)
		mode = FP_MODE_DONT_CHANGE;
	for (i = 1; i < argc; i++) {
		/* modes */
		if (!strncmp(argv[i], "deepsleep", 9))
			mode |= FP_MODE_DEEPSLEEP;
		else if (!strncmp(argv[i], "fingerdown", 10))
			mode |= FP_MODE_FINGER_DOWN;
		else if (!strncmp(argv[i], "fingerup", 8))
			mode |= FP_MODE_FINGER_UP;
		else if (!strncmp(argv[i], "enroll", 6))
			mode |= FP_MODE_ENROLL_IMAGE | FP_MODE_ENROLL_SESSION;
		else if (!strncmp(argv[i], "match", 5))
			mode |= FP_MODE_MATCH;
		else if (!strncmp(argv[i], "reset_sensor", 12))
			mode = FP_MODE_RESET_SENSOR;
		else if (!strncmp(argv[i], "reset", 5))
			mode = 0;
		else if (!strncmp(argv[i], "maintenance", 11))
			mode |= FP_MODE_SENSOR_MAINTENANCE;
		else if (!strncmp(argv[i], "capture", 7))
			mode |= FP_MODE_CAPTURE;
		/* capture types */
		else if (!strncmp(argv[i], "vendor", 6))
			capture_type = FP_CAPTURE_VENDOR_FORMAT;
		else if (!strncmp(argv[i], "pattern0", 8))
			capture_type = FP_CAPTURE_PATTERN0;
		else if (!strncmp(argv[i], "pattern1", 8))
			capture_type = FP_CAPTURE_PATTERN1;
		else if (!strncmp(argv[i], "qual", 4))
			capture_type = FP_CAPTURE_QUALITY_TEST;
		else if (!strncmp(argv[i], "test_reset", 10))
			capture_type = FP_CAPTURE_RESET_TEST;
	}
	if (mode & FP_MODE_CAPTURE)
		mode |= capture_type << FP_MODE_CAPTURE_TYPE_SHIFT;

	p.mode = mode;
	rv = ec_command(EC_CMD_FP_MODE, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("FP mode: (0x%x) ", r.mode);
	if (r.mode & FP_MODE_DEEPSLEEP)
		printf("deepsleep ");
	if (r.mode & FP_MODE_FINGER_DOWN)
		printf("finger-down ");
	if (r.mode & FP_MODE_FINGER_UP)
		printf("finger-up ");
	if (r.mode & FP_MODE_ENROLL_SESSION)
		printf("enroll%s ",
		       r.mode & FP_MODE_ENROLL_IMAGE ? "+image" : "");
	if (r.mode & FP_MODE_MATCH)
		printf("match ");
	if (r.mode & FP_MODE_CAPTURE)
		printf("capture ");
	printf("\n");
	return 0;
}

int cmd_fp_seed(int argc, char *argv[])
{
	struct ec_params_fp_seed p;
	char *seed;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <seed>\n", argv[0]);
		return 1;
	}
	seed = argv[1];
	if (strlen(seed) != FP_CONTEXT_TPM_BYTES) {
		printf("Invalid seed '%s' is %zd bytes long instead of %d.\n",
		       seed, strlen(seed), FP_CONTEXT_TPM_BYTES);
		return 1;
	}
	printf("Setting seed '%s'\n", seed);
	p.struct_version = FP_TEMPLATE_FORMAT_VERSION;
	memcpy(p.seed, seed, FP_CONTEXT_TPM_BYTES);

	return ec_command(EC_CMD_FP_SEED, 0, &p, sizeof(p), NULL, 0);
}

int cmd_fp_stats(int argc, char *argv[])
{
	struct ec_response_fp_stats r;
	int rv;
	unsigned long long ts;

	rv = ec_command(EC_CMD_FP_STATS, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;

	ts = (uint64_t)r.overall_t0.hi << 32 | r.overall_t0.lo;
	printf("FP stats (t0=%llu us):\n", ts);
	printf("Last capture time:  ");
	if (r.timestamps_invalid & FPSTATS_CAPTURE_INV)
		printf("Invalid\n");
	else
		printf("%d us\n", r.capture_time_us);

	printf("Last matching time: ");
	if (r.timestamps_invalid & FPSTATS_MATCHING_INV)
		printf("Invalid\n");
	else
		printf("%d us (finger: %d)\n", r.matching_time_us,
		       r.template_matched);

	printf("Last overall time:  ");
	if (r.timestamps_invalid)
		printf("Invalid\n");
	else
		printf("%d us\n", r.overall_time_us);

	return 0;
}

int cmd_fp_info(int argc, char *argv[])
{
	struct ec_response_fp_info r;
	int rv;
	int cmdver = ec_cmd_version_supported(EC_CMD_FP_INFO, 1) ? 1 : 0;
	int rsize = cmdver == 1 ? sizeof(r) :
				  sizeof(struct ec_response_fp_info_v0);
	uint16_t dead;

	rv = ec_command(EC_CMD_FP_INFO, cmdver, NULL, 0, &r, rsize);
	if (rv < 0)
		return rv;

	printf("Fingerprint sensor: vendor %x product %x model %x version %x\n",
	       r.vendor_id, r.product_id, r.model_id, r.version);
	printf("Image: size %dx%d %d bpp\n", r.width, r.height, r.bpp);
	printf("Error flags: %s%s%s%s\n",
	       r.errors & FP_ERROR_NO_IRQ ? "NO_IRQ " : "",
	       r.errors & FP_ERROR_SPI_COMM ? "SPI_COMM " : "",
	       r.errors & FP_ERROR_BAD_HWID ? "BAD_HWID " : "",
	       r.errors & FP_ERROR_INIT_FAIL ? "INIT_FAIL " : "");
	dead = FP_ERROR_DEAD_PIXELS(r.errors);
	if (dead == FP_ERROR_DEAD_PIXELS_UNKNOWN) {
		printf("Dead pixels: UNKNOWN\n");
	} else {
		printf("Dead pixels: %u\n", dead);
	}

	if (cmdver == 1) {
		printf("Templates: version %d size %d count %d/%d"
		       " dirty bitmap %x\n",
		       r.template_version, r.template_size, r.template_valid,
		       r.template_max, r.template_dirty);
	}

	return 0;
}

static int cmd_fp_context(int argc, char *argv[])
{
	struct ec_params_fp_context_v1 p;
	int rv;
	int tries = 20; /* Wait at most two seconds */

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <context>\n", argv[0]);
		return -1;
	}

	/*
	 * Note that we treat the resulting "userid" as raw byte array, so we
	 * don't want to copy the NUL from the end of the string.
	 */
	if (strlen(argv[1]) != sizeof(p.userid)) {
		fprintf(stderr, "Context must be exactly %zu bytes\n",
			sizeof(p.userid));
		return -1;
	}

	p.action = FP_CONTEXT_ASYNC;
	memcpy(p.userid, argv[1], sizeof(p.userid));

	rv = ec_command(EC_CMD_FP_CONTEXT, 1, &p, sizeof(p), NULL, 0);

	if (rv != EC_RES_SUCCESS)
		goto out;

	while (tries--) {
		usleep(100000);

		p.action = FP_CONTEXT_GET_RESULT;
		rv = ec_command(EC_CMD_FP_CONTEXT, 1, &p, sizeof(p), NULL, 0);

		if (rv == EC_RES_SUCCESS) {
			printf("Set context successfully\n");
			return EC_RES_SUCCESS;
		}

		/* Abort if EC returns an error other than EC_RES_BUSY. */
		if (rv <= -EECRESULT && rv != -EECRESULT - EC_RES_BUSY)
			goto out;
	}

	rv = -EECRESULT - EC_RES_TIMEOUT;

out:
	fprintf(stderr, "Failed to reset context: %d\n", rv);
	return rv;
}

int cmd_fp_enc_status(int argc, char *argv[])
{
	int rv;

	ec::FpEncryptionStatusCommand fp_encryptionstatus_command;

	if (!fp_encryptionstatus_command.Run(comm_get_fd())) {
		int rv = -EECRESULT - fp_encryptionstatus_command.Result();
		fprintf(stderr,
			"FP Encryption Status returned with errors: %d\n", rv);
		return rv;
	}
	printf("FPMCU encryption status: 0x%08x%s",
	       fp_encryptionstatus_command.GetStatus(),
	       (ec::FpEncryptionStatusCommand::ParseFlags(
			fp_encryptionstatus_command.GetStatus()))
		       .c_str());
	printf("Valid flags:             0x%08x%s",
	       fp_encryptionstatus_command.GetValidFlags(),
	       (ec::FpEncryptionStatusCommand::ParseFlags(
			fp_encryptionstatus_command.GetValidFlags()))
		       .c_str());

	rv = 0;

	return rv;
}

int cmd_fp_frame(int argc, char *argv[])
{
	struct ec_response_fp_info r;
	int idx = (argc == 2 && !strcasecmp(argv[1], "raw")) ?
			  FP_FRAME_INDEX_RAW_IMAGE :
			  FP_FRAME_INDEX_SIMPLE_IMAGE;
	uint8_t *buffer = (uint8_t *)(fp_download_frame(&r, idx));
	uint8_t *ptr = buffer;
	int x, y;

	if (!buffer) {
		fprintf(stderr, "Failed to get FP sensor frame\n");
		return -1;
	}

	if (idx == FP_FRAME_INDEX_RAW_IMAGE) {
		fwrite(buffer, r.frame_size, 1, stdout);
		goto frame_done;
	}

	/* Print 8-bpp PGM ASCII header */
	printf("P2\n%d %d\n%d\n", r.width, r.height, (1 << r.bpp) - 1);

	for (y = 0; y < r.height; y++) {
		for (x = 0; x < r.width; x++, ptr++)
			printf("%d ", *ptr);
		printf("\n");
	}
	printf("# END OF FILE\n");
frame_done:
	free(buffer);
	return 0;
}

int cmd_fp_template(int argc, char *argv[])
{
	struct ec_response_fp_info r;
	struct ec_params_fp_template *p =
		(struct ec_params_fp_template *)(ec_outbuf);
	/* TODO(b/78544921): removing 32 bits is a workaround for the MCU bug */
	int max_chunk = ec_max_outsize -
			offsetof(struct ec_params_fp_template, data) - 4;
	int idx = -1;
	char *e;
	int size;
	char *buffer = NULL;
	uint32_t offset = 0;
	int rv = 0;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s [<infile>|<index>]\n", argv[0]);
		return -1;
	}

	idx = strtol(argv[1], &e, 0);
	if (!(e && *e)) {
		buffer = (char *)(fp_download_frame(&r, idx + 1));
		if (!buffer) {
			fprintf(stderr, "Failed to get FP template %d\n", idx);
			return -1;
		}
		fwrite(buffer, r.template_size, 1, stdout);
		free(buffer);
		return 0;
	}
	/* not an index, is it a filename ? */
	buffer = read_file(argv[1], &size);
	if (!buffer) {
		fprintf(stderr, "Invalid parameter: %s\n", argv[1]);
		return -1;
	}
	printf("sending template from: %s (%d bytes)\n", argv[1], size);
	while (size) {
		uint32_t tlen = MIN(max_chunk, size);

		p->offset = offset;
		p->size = tlen;
		size -= tlen;
		if (!size)
			p->size |= FP_TEMPLATE_COMMIT;
		memcpy(p->data, buffer + offset, tlen);
		rv = ec_command(EC_CMD_FP_TEMPLATE, 0, p,
				tlen + offsetof(struct ec_params_fp_template,
						data),
				NULL, 0);
		if (rv < 0)
			break;
		offset += tlen;
	}
	if (rv < 0)
		fprintf(stderr, "Failed with %d\n", rv);
	else
		rv = 0;
	free(buffer);
	return rv;
}

/**
 * determine if in GFU mode or not.
 *
 * NOTE, Sends HOST commands that modify ec_outbuf contents.
 *
 * @opos return value of GFU mode object position or zero if not found
 * @port port number to query
 * @return 1 if in GFU mode, 0 if not, -1 if error
 */
static int in_gfu_mode(int *opos, int port)
{
	int i;
	struct ec_params_usb_pd_get_mode_request *p =
		(struct ec_params_usb_pd_get_mode_request *)ec_outbuf;
	struct ec_params_usb_pd_get_mode_response *r =
		(struct ec_params_usb_pd_get_mode_response *)ec_inbuf;
	p->port = port;
	p->svid_idx = 0;
	do {
		ec_command(EC_CMD_USB_PD_GET_AMODE, 0, p, sizeof(*p), ec_inbuf,
			   ec_max_insize);
		if (!r->svid || (r->svid == USB_VID_GOOGLE))
			break;
		p->svid_idx++;
	} while (p->svid_idx < SVID_DISCOVERY_MAX);

	if (r->svid != USB_VID_GOOGLE) {
		fprintf(stderr, "Google VID not returned\n");
		return -1;
	}

	*opos = 0; /* invalid ... must be 1 thru 6 */
	for (i = 0; i < VDO_MAX_OBJECTS; i++) {
		if (r->vdo[i] == MODE_GOOGLE_FU) {
			*opos = i + 1;
			break;
		}
	}

	return r->opos == *opos;
}

/**
 * Enter GFU mode.
 *
 * NOTE, Sends HOST commands that modify ec_outbuf contents.
 *
 * @port port number to enter GFU on.
 * @return 1 if entered GFU mode, 0 if not, -1 if error
 */
static int enter_gfu_mode(int port)
{
	int opos;
	struct ec_params_usb_pd_set_mode_request *p =
		(struct ec_params_usb_pd_set_mode_request *)ec_outbuf;
	int gfu_mode = in_gfu_mode(&opos, port);

	if (gfu_mode < 0) {
		fprintf(stderr, "Failed to query GFU mode support\n");
		return 0;
	} else if (!gfu_mode) {
		if (!opos) {
			fprintf(stderr, "Invalid object position %d\n", opos);
			return 0;
		}
		p->port = port;
		p->svid = USB_VID_GOOGLE;
		p->opos = opos;
		p->cmd = PD_ENTER_MODE;

		ec_command(EC_CMD_USB_PD_SET_AMODE, 0, p, sizeof(*p), NULL, 0);
		usleep(500000); /* sleep to allow time for set mode */
		gfu_mode = in_gfu_mode(&opos, port);
	}
	return gfu_mode;
}

int cmd_pd_device_info(int argc, char *argv[])
{
	int i, rv, port;
	char *e;
	struct ec_params_usb_pd_info_request *p =
		(struct ec_params_usb_pd_info_request *)ec_outbuf;
	struct ec_params_usb_pd_rw_hash_entry *r0 =
		(struct ec_params_usb_pd_rw_hash_entry *)ec_inbuf;
	struct ec_params_usb_pd_discovery_entry *r1;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <port>\n", argv[0]);
		return -1;
	}

	port = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port\n");
		return -1;
	}

	p->port = port;
	r1 = (struct ec_params_usb_pd_discovery_entry *)ec_inbuf;
	rv = ec_command(EC_CMD_USB_PD_DISCOVERY, 0, p, sizeof(*p), r1,
			sizeof(*r1));
	if (rv < 0)
		return rv;

	if (!r1->vid)
		printf("Port:%d has no discovered device\n", port);
	else {
		printf("Port:%d ptype:%d vid:0x%04x pid:0x%04x\n", port,
		       r1->ptype, r1->vid, r1->pid);
	}

	if (enter_gfu_mode(port) != 1) {
		fprintf(stderr, "Failed to enter GFU mode\n");
		return -1;
	}

	p->port = port;
	rv = ec_command(EC_CMD_USB_PD_DEV_INFO, 0, p, sizeof(*p), r0,
			sizeof(*r0));
	if (rv < 0)
		return rv;

	if (!r0->dev_id)
		printf("Port:%d has no valid device\n", port);
	else {
		uint8_t *rwp = r0->dev_rw_hash;
		printf("Port:%d DevId:%d.%d Hash:", port,
		       HW_DEV_ID_MAJ(r0->dev_id), HW_DEV_ID_MIN(r0->dev_id));
		for (i = 0; i < 5; i++) {
			printf(" 0x%02x%02x%02x%02x", rwp[3], rwp[2], rwp[1],
			       rwp[0]);
			rwp += 4;
		}
		printf(" CurImg:%s\n", image_names[r0->current_image]);
	}

	return rv;
}

int cmd_flash_pd(int argc, char *argv[])
{
	struct ec_params_usb_pd_fw_update *p =
		(struct ec_params_usb_pd_fw_update *)ec_outbuf;
	int i, dev_id, port;
	int rv, fsize, step = 96;
	char *e;
	char *buf;
	char *data = (char *)p + sizeof(*p);

	if (argc < 4) {
		fprintf(stderr, "Usage: %s <dev_id> <port> <filename>\n",
			argv[0]);
		return -1;
	}

	dev_id = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad device ID\n");
		return -1;
	}

	port = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port\n");
		return -1;
	}

	if (enter_gfu_mode(port) != 1) {
		fprintf(stderr, "Failed to enter GFU mode\n");
		return -1;
	}

	/* Read the input file */
	buf = read_file(argv[3], &fsize);
	if (!buf)
		return -1;

	/* Erase the current RW RSA signature */
	fprintf(stderr, "Erasing expected RW hash\n");
	p->dev_id = dev_id;
	p->port = port;
	p->cmd = USB_PD_FW_ERASE_SIG;
	p->size = 0;
	rv = ec_command(EC_CMD_USB_PD_FW_UPDATE, 0, p, p->size + sizeof(*p),
			NULL, 0);

	if (rv < 0)
		goto pd_flash_error;

	/* Reboot */
	fprintf(stderr, "Rebooting\n");
	p->dev_id = dev_id;
	p->port = port;
	p->cmd = USB_PD_FW_REBOOT;
	p->size = 0;
	rv = ec_command(EC_CMD_USB_PD_FW_UPDATE, 0, p, p->size + sizeof(*p),
			NULL, 0);

	if (rv < 0)
		goto pd_flash_error;

	usleep(3000000); /* 3sec to reboot and get CC line idle */

	/* re-enter GFU after reboot */
	if (enter_gfu_mode(port) != 1) {
		fprintf(stderr, "Failed to enter GFU mode\n");
		goto pd_flash_error;
	}

	/* Erase RW flash */
	fprintf(stderr, "Erasing RW flash\n");
	p->dev_id = dev_id;
	p->port = port;
	p->cmd = USB_PD_FW_FLASH_ERASE;
	p->size = 0;
	rv = ec_command(EC_CMD_USB_PD_FW_UPDATE, 0, p, p->size + sizeof(*p),
			NULL, 0);

	/* 3 secs should allow ample time for 2KB page erases at 40ms */
	usleep(3000000);

	if (rv < 0)
		goto pd_flash_error;

	/* Write RW flash */
	fprintf(stderr, "Writing RW flash\n");
	p->dev_id = dev_id;
	p->port = port;
	p->cmd = USB_PD_FW_FLASH_WRITE;
	p->size = step;

	for (i = 0; i < fsize; i += step) {
		p->size = MIN(fsize - i, step);
		memcpy(data, buf + i, p->size);
		rv = ec_command(EC_CMD_USB_PD_FW_UPDATE, 0, p,
				p->size + sizeof(*p), NULL, 0);
		if (rv < 0)
			goto pd_flash_error;

		/*
		 * TODO(crosbug.com/p/33905) throttle so EC doesn't watchdog on
		 * other tasks.  Remove once issue resolved.
		 */
		usleep(10000);
	}

	/* 100msec to guarantee writes finish */
	usleep(100000);

	/* Reboot into new RW */
	fprintf(stderr, "Rebooting PD into new RW\n");
	p->cmd = USB_PD_FW_REBOOT;
	p->size = 0;
	rv = ec_command(EC_CMD_USB_PD_FW_UPDATE, 0, p, p->size + sizeof(*p),
			NULL, 0);

	if (rv < 0)
		goto pd_flash_error;

	free(buf);
	fprintf(stderr, "Complete\n");
	return 0;

pd_flash_error:
	free(buf);
	fprintf(stderr, "PD flash error\n");
	return -1;
}

int cmd_pd_set_amode(int argc, char *argv[])
{
	char *e;
	struct ec_params_usb_pd_set_mode_request *p =
		(struct ec_params_usb_pd_set_mode_request *)ec_outbuf;

	if (argc < 5) {
		fprintf(stderr, "Usage: %s <port> <svid> <opos> <cmd>\n",
			argv[0]);
		return -1;
	}

	p->port = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port\n");
		return -1;
	}

	p->svid = strtol(argv[2], &e, 0);
	if ((e && *e) || !p->svid) {
		fprintf(stderr, "Bad svid\n");
		return -1;
	}

	p->opos = strtol(argv[3], &e, 0);
	if ((e && *e) || !p->opos) {
		fprintf(stderr, "Bad opos\n");
		return -1;
	}

	p->cmd = strtol(argv[4], &e, 0);
	if ((e && *e) || (p->cmd >= PD_MODE_CMD_COUNT)) {
		fprintf(stderr, "Bad cmd\n");
		return -1;
	}
	return ec_command(EC_CMD_USB_PD_SET_AMODE, 0, p, sizeof(*p), NULL, 0);
}

int cmd_pd_get_amode(int argc, char *argv[])
{
	int i;
	char *e;
	struct ec_params_usb_pd_get_mode_request *p =
		(struct ec_params_usb_pd_get_mode_request *)ec_outbuf;
	struct ec_params_usb_pd_get_mode_response *r =
		(struct ec_params_usb_pd_get_mode_response *)ec_inbuf;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <port>\n", argv[0]);
		return -1;
	}

	p->port = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port\n");
		return -1;
	}

	p->svid_idx = 0;
	do {
		ec_command(EC_CMD_USB_PD_GET_AMODE, 0, p, sizeof(*p), ec_inbuf,
			   ec_max_insize);
		if (!r->svid)
			break;
		printf("%cSVID:0x%04x ", (r->opos) ? '*' : ' ', r->svid);
		for (i = 0; i < VDO_MAX_OBJECTS; i++) {
			printf("%c0x%08x ",
			       (r->opos && (r->opos == i + 1)) ? '*' : ' ',
			       r->vdo[i]);
		}
		printf("\n");
		p->svid_idx++;
	} while (p->svid_idx < SVID_DISCOVERY_MAX);
	return -1;
}

/* The I/O asm funcs exist only on x86. */
#if defined(__i386__) || defined(__x86_64__)
#include <sys/io.h>

int cmd_serial_test(int argc, char *argv[])
{
	const char *c = "COM2 sample serial output from host!\r\n";

	printf("Writing sample serial output to COM2\n");

	while (*c) {
		/* Wait for space in transmit FIFO */
		while (!(inb(0x2fd) & 0x20))
			;

		/* Put the next character */
		outb(*c++, 0x2f8);
	}

	printf("done.\n");
	return 0;
}

int cmd_port_80_flood(int argc, char *argv[])
{
	int i;

	for (i = 0; i < 256; i++)
		outb(i, 0x80);
	return 0;
}
#else
int cmd_serial_test(int argc, char *argv[])
{
	printf("x86 specific command\n");
	return -1;
}

int cmd_port_80_flood(int argc, char *argv[])
{
	printf("x86 specific command\n");
	return -1;
}
#endif

static void cmd_smart_discharge_usage(const char *command)
{
	printf("Usage: %s [hours_to_zero [hibern] [cutoff]]\n", command);
	printf("\n");
	printf("Set/Get smart discharge parameters\n");
	printf("hours_to_zero: Desired hours for state of charge to zero\n");
	printf("hibern: Discharge rate in hibernation (uA)\n");
	printf("cutoff: Discharge rate in battery cutoff (uA)\n");
}

int cmd_smart_discharge(int argc, char *argv[])
{
	struct ec_params_smart_discharge *p =
		(struct ec_params_smart_discharge *)(ec_outbuf);
	struct ec_response_smart_discharge *r =
		(struct ec_response_smart_discharge *)(ec_inbuf);
	uint32_t cap;
	char *e;
	int rv;

	if (argc > 1) {
		if (strcmp(argv[1], "help") == 0) {
			cmd_smart_discharge_usage(argv[0]);
			return 0;
		}
		p->flags = EC_SMART_DISCHARGE_FLAGS_SET;
		p->hours_to_zero = strtol(argv[1], &e, 0);
		if (p->hours_to_zero < 0 || (e && *e)) {
			perror("Bad value for [hours_to_zero]");
			return -1;
		}
		if (argc == 4) {
			p->drate.hibern = strtol(argv[2], &e, 0);
			if (p->drate.hibern < 0 || (e && *e)) {
				perror("Bad value for [hibern]");
				return -1;
			}
			p->drate.cutoff = strtol(argv[3], &e, 0);
			if (p->drate.cutoff < 0 || (e && *e)) {
				perror("Bad value for [cutoff]");
				return -1;
			}
		} else if (argc != 2) {
			/* If argc != 4, it has to be 2. */
			perror("Invalid number of parameters");
			return -1;
		}
	}

	rv = ec_command(EC_CMD_SMART_DISCHARGE, 0, p, sizeof(*p), r,
			sizeof(*r));
	if (rv < 0) {
		perror("ERROR: EC_CMD_SMART_DISCHARGE failed");
		return rv;
	}

	cap = read_mapped_mem32(EC_MEMMAP_BATT_LFCC);
	if (!is_battery_range(cap)) {
		perror("WARN: Failed to read battery capacity");
		cap = 0;
	}

	printf("%-27s %5d h\n", "Hours to zero capacity:", r->hours_to_zero);
	printf("%-27s %5d mAh (%d %%)\n", "Stay-up threshold:", r->dzone.stayup,
	       cap > 0 ? r->dzone.stayup * 100 / cap : -1);
	printf("%-27s %5d mAh (%d %%)\n", "Cutoff threshold:", r->dzone.cutoff,
	       cap > 0 ? r->dzone.cutoff * 100 / cap : -1);
	printf("%-27s %5d uA\n", "Hibernate discharge rate:", r->drate.hibern);
	printf("%-27s %5d uA\n", "Cutoff discharge rate:", r->drate.cutoff);

	return 0;
}

/*
 * This boolean variable and handler are used for
 * catching signals that translate into a quit/shutdown
 * of a runtime loop.
 * This is used in cmd_stress_test.
 */
static bool sig_quit;
static void sig_quit_handler(int sig)
{
	sig_quit = true;
}

int cmd_stress_test(int argc, char *argv[])
{
	int i;
	bool reboot = false;
	time_t now;
	time_t start_time, last_update_time;
	unsigned int rand_seed = 0;
	uint64_t round = 1, attempt = 1;
	uint64_t failures = 0;

	const int max_sleep_usec = 1000; /* 1ms */
	const int loop_update_interval = 10000;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "help") == 0) {
			printf("Usage: %s [reboot] [help]\n", argv[0]);
			printf("Stress tests the host command interface by"
			       " repeatedly issuing common host commands.\n");
			printf("The intent is to expose errors in kernel<->mcu"
			       " communication, such as exceeding timeouts.\n");
			printf("\n");
			printf("reboot - Reboots the target before"
			       " starting the stress test.\n");
			printf("         This may force restart the host,"
			       " if the main ec is the target.\n");
			return 0;
		} else if (strcmp(argv[i], "reboot") == 0) {
			reboot = true;
		} else {
			fprintf(stderr, "Error - Unknown argument '%s'\n",
				argv[i]);
			return 1;
		}
	}

	printf("Stress test tool version: %s %s %s\n", CROS_ECTOOL_VERSION,
	       DATE, BUILDER);

	start_time = time(NULL);
	last_update_time = start_time;
	printf("Start time: %s\n", ctime(&start_time));

	if (reboot) {
		printf("Issuing ec reboot. Expect a few early failed"
		       " ioctl messages.\n");
		ec_command(EC_CMD_REBOOT, 0, NULL, 0, NULL, 0);
		sleep(2);
	}

	sig_quit = false;
	signal(SIGINT, sig_quit_handler);
	while (!sig_quit) {
		int rv;
		struct ec_response_get_version ver_r;
		char *build_string = (char *)ec_inbuf;
		struct ec_params_flash_protect flash_p;
		struct ec_response_flash_protect flash_r;
		struct ec_params_hello hello_p;
		struct ec_response_hello hello_r;

		/* Request EC Version Strings */
		rv = ec_command(EC_CMD_GET_VERSION, 0, NULL, 0, &ver_r,
				sizeof(ver_r));
		if (rv < 0) {
			failures++;
			perror("ERROR: EC_CMD_GET_VERSION failed");
		}
		ver_r.version_string_ro[sizeof(ver_r.version_string_ro) - 1] =
			'\0';
		ver_r.version_string_rw[sizeof(ver_r.version_string_rw) - 1] =
			'\0';
		if (strlen(ver_r.version_string_ro) == 0) {
			failures++;
			fprintf(stderr, "RO version string is empty\n");
		}
		if (strlen(ver_r.version_string_rw) == 0) {
			failures++;
			fprintf(stderr, "RW version string is empty\n");
		}

		usleep(rand_r(&rand_seed) % max_sleep_usec);

		/* Request EC Build String */
		rv = ec_command(EC_CMD_GET_BUILD_INFO, 0, NULL, 0, ec_inbuf,
				ec_max_insize);
		if (rv < 0) {
			failures++;
			perror("ERROR: EC_CMD_GET_BUILD_INFO failed");
		}
		build_string[ec_max_insize - 1] = '\0';
		if (strlen(build_string) == 0) {
			failures++;
			fprintf(stderr, "Build string is empty\n");
		}

		usleep(rand_r(&rand_seed) % max_sleep_usec);

		/* Request Flash Protect Status */
		rv = ec_command(EC_CMD_FLASH_PROTECT, EC_VER_FLASH_PROTECT,
				&flash_p, sizeof(flash_p), &flash_r,
				sizeof(flash_r));
		if (rv < 0) {
			failures++;
			perror("ERROR: EC_CMD_FLASH_PROTECT failed");
		}

		usleep(rand_r(&rand_seed) % max_sleep_usec);

		/* Request Hello */
		hello_p.in_data = 0xa0b0c0d0;
		rv = ec_command(EC_CMD_HELLO, 0, &hello_p, sizeof(hello_p),
				&hello_r, sizeof(hello_r));
		if (rv < 0) {
			failures++;
			perror("ERROR: EC_CMD_HELLO failed");
		}
		if (hello_r.out_data != HELLO_RESP(hello_p.in_data)) {
			failures++;
			fprintf(stderr, "Hello response was invalid.\n");
		}

		usleep(rand_r(&rand_seed) % max_sleep_usec);

		if ((attempt % loop_update_interval) == 0) {
			now = time(NULL);
			printf("Update: attempt %" PRIu64 " round %" PRIu64
			       " | took %.f seconds\n",
			       attempt, round, difftime(now, last_update_time));
			last_update_time = now;
		}

		if (attempt++ == UINT64_MAX)
			round++;
	}
	printf("\n");

	now = time(NULL);
	printf("End time:        %s\n", ctime(&now));
	printf("Total runtime:   %.f seconds\n",
	       difftime(time(NULL), start_time));
	printf("Total failures:  %" PRIu64 "\n", failures);
	return 0;
}

int read_mapped_temperature(int id)
{
	int rv;

	if (!read_mapped_mem8(EC_MEMMAP_THERMAL_VERSION)) {
		/*
		 *  The temp_sensor_init() is not called, which implies no
		 * temp sensor is defined.
		 */
		rv = EC_TEMP_SENSOR_NOT_PRESENT;
	} else if (id < EC_TEMP_SENSOR_ENTRIES)
		rv = read_mapped_mem8(EC_MEMMAP_TEMP_SENSOR + id);
	else if (read_mapped_mem8(EC_MEMMAP_THERMAL_VERSION) >= 2)
		rv = read_mapped_mem8(EC_MEMMAP_TEMP_SENSOR_B + id -
				      EC_TEMP_SENSOR_ENTRIES);
	else {
		/* Sensor in second bank, but second bank isn't supported */
		rv = EC_TEMP_SENSOR_NOT_PRESENT;
	}
	return rv;
}

static int get_temp_ratio(int temp, int fan_off, int fan_max)
{
	if (temp < fan_off)
		return 0;
	if (temp > fan_max)
		return 100;
	return 100 * (temp - fan_off) / (fan_max - fan_off);
}

static int cmd_temperature_print(int id, int mtemp)
{
	struct ec_response_temp_sensor_get_info temp_r;
	struct ec_params_temp_sensor_get_info temp_p;
	struct ec_params_thermal_get_threshold_v1 p;
	struct ec_thermal_config r;
	int rc;
	int temp = mtemp + EC_TEMP_SENSOR_OFFSET;

	temp_p.id = id;
	rc = ec_command(EC_CMD_TEMP_SENSOR_GET_INFO, 0, &temp_p, sizeof(temp_p),
			&temp_r, sizeof(temp_r));
	if (rc < 0)
		return rc;

	p.sensor_num = id;
	rc = ec_command(EC_CMD_THERMAL_GET_THRESHOLD, 1, &p, sizeof(p), &r,
			sizeof(r));

	printf("%-20s  %d K (= %d C)", temp_r.sensor_name, temp, K_TO_C(temp));

	if (rc >= 0)
		/*
		 * Check for fan_off == fan_max when their
		 * values are either zero or non-zero
		 */
		if (r.temp_fan_off == r.temp_fan_max)
			printf("        N/A (fan_off=%d K, fan_max=%d K)",
			       r.temp_fan_off, r.temp_fan_max);
		else
			printf("  %10d%% (%d K and %d K)",
			       get_temp_ratio(temp, r.temp_fan_off,
					      r.temp_fan_max),
			       r.temp_fan_off, r.temp_fan_max);
	else
		printf("%20s(rc=%d)", "error", rc);

	return 0;
}

int cmd_temperature(int argc, char *argv[])
{
	int mtemp;
	int id;
	char *e;
	const char header[] = "--sensor name -------- temperature "
			      "-------- ratio (fan_off and fan_max) --\n";

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <sensorid> | all\n", argv[0]);
		return -1;
	}

	if (strcmp(argv[1], "all") == 0) {
		fprintf(stdout, header);
		for (id = 0; id < EC_MAX_TEMP_SENSOR_ENTRIES; id++) {
			mtemp = read_mapped_temperature(id);
			switch (mtemp) {
			case EC_TEMP_SENSOR_NOT_PRESENT:
				break;
			case EC_TEMP_SENSOR_ERROR:
				fprintf(stderr, "Sensor %d error\n", id);
				break;
			case EC_TEMP_SENSOR_NOT_POWERED:
				fprintf(stderr, "Sensor %d disabled\n", id);
				break;
			case EC_TEMP_SENSOR_NOT_CALIBRATED:
				fprintf(stderr, "Sensor %d not calibrated\n",
					id);
				break;
			default:
				cmd_temperature_print(id, mtemp);
				printf("\n");
			}
		}
		return 0;
	}

	id = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad sensor ID.\n");
		return -1;
	}

	if (id < 0 || id >= EC_MAX_TEMP_SENSOR_ENTRIES) {
		printf("Sensor ID invalid.\n");
		return -1;
	}

	printf("Reading temperature...");
	mtemp = read_mapped_temperature(id);

	switch (mtemp) {
	case EC_TEMP_SENSOR_NOT_PRESENT:
		printf("Sensor not present\n");
		return -1;
	case EC_TEMP_SENSOR_ERROR:
		printf("Error\n");
		return -1;
	case EC_TEMP_SENSOR_NOT_POWERED:
		printf("Sensor disabled/unpowered\n");
		return -1;
	case EC_TEMP_SENSOR_NOT_CALIBRATED:
		fprintf(stderr, "Sensor not calibrated\n");
		return -1;
	default:
		fprintf(stdout, "\n");
		fprintf(stdout, header);
		return cmd_temperature_print(id, mtemp);
	}
}

int cmd_temp_sensor_info(int argc, char *argv[])
{
	struct ec_params_temp_sensor_get_info p;
	struct ec_response_temp_sensor_get_info r;
	int rv;
	char *e;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <sensorid> | all\n", argv[0]);
		return -1;
	}

	if (strcmp(argv[1], "all") == 0) {
		for (p.id = 0; p.id < EC_MAX_TEMP_SENSOR_ENTRIES; p.id++) {
			if (read_mapped_temperature(p.id) ==
			    EC_TEMP_SENSOR_NOT_PRESENT)
				continue;
			rv = ec_command(EC_CMD_TEMP_SENSOR_GET_INFO, 0, &p,
					sizeof(p), &r, sizeof(r));
			if (rv < 0)
				continue;
			printf("%d: %d %s\n", p.id, r.sensor_type,
			       r.sensor_name);
		}
		return 0;
	}

	p.id = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad sensor ID.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_TEMP_SENSOR_GET_INFO, 0, &p, sizeof(p), &r,
			sizeof(r));
	if (rv < 0)
		return rv;

	printf("Sensor name: %s\n", r.sensor_name);
	printf("Sensor type: %d\n", r.sensor_type);

	return 0;
}

int cmd_thermal_get_threshold_v0(int argc, char *argv[])
{
	struct ec_params_thermal_get_threshold p;
	struct ec_response_thermal_get_threshold r;
	char *e;
	int rv;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <sensortypeid> <thresholdid>\n",
			argv[0]);
		return -1;
	}

	p.sensor_type = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad sensor type ID.\n");
		return -1;
	}

	p.threshold_id = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad threshold ID.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_THERMAL_GET_THRESHOLD, 0, &p, sizeof(p), &r,
			sizeof(r));
	if (rv < 0)
		return rv;

	printf("Threshold %d for sensor type %d is %d K.\n", p.threshold_id,
	       p.sensor_type, r.value);

	return 0;
}

int cmd_thermal_set_threshold_v0(int argc, char *argv[])
{
	struct ec_params_thermal_set_threshold p;
	char *e;
	int rv;

	if (argc != 4) {
		fprintf(stderr,
			"Usage: %s <sensortypeid> <thresholdid> <value>\n",
			argv[0]);
		return -1;
	}

	p.sensor_type = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad sensor type ID.\n");
		return -1;
	}

	p.threshold_id = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad threshold ID.\n");
		return -1;
	}

	p.value = strtol(argv[3], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad threshold value.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_THERMAL_SET_THRESHOLD, 0, &p, sizeof(p), NULL,
			0);
	if (rv < 0)
		return rv;

	printf("Threshold %d for sensor type %d set to %d.\n", p.threshold_id,
	       p.sensor_type, p.value);

	return 0;
}

int cmd_thermal_get_threshold_v1(int argc, char *argv[])
{
	struct ec_params_thermal_get_threshold_v1 p;
	struct ec_thermal_config r;
	struct ec_params_temp_sensor_get_info pi;
	struct ec_response_temp_sensor_get_info ri;
	int rv;
	int i;

	printf("sensor  warn  high  halt   fan_off fan_max   name\n");
	for (i = 0; i < EC_MAX_TEMP_SENSOR_ENTRIES; i++) {
		if (read_mapped_temperature(i) == EC_TEMP_SENSOR_NOT_PRESENT)
			continue;

		/* ask for one */
		p.sensor_num = i;
		rv = ec_command(EC_CMD_THERMAL_GET_THRESHOLD, 1, &p, sizeof(p),
				&r, sizeof(r));
		if (rv <= 0) /* stop on first failure */
			break;

		/* ask for its name, too */
		pi.id = i;
		rv = ec_command(EC_CMD_TEMP_SENSOR_GET_INFO, 0, &pi, sizeof(pi),
				&ri, sizeof(ri));

		/* print what we know */
		printf(" %2d      %3d   %3d    %3d    %3d     %3d     %s\n", i,
		       r.temp_host[EC_TEMP_THRESH_WARN],
		       r.temp_host[EC_TEMP_THRESH_HIGH],
		       r.temp_host[EC_TEMP_THRESH_HALT], r.temp_fan_off,
		       r.temp_fan_max, rv > 0 ? ri.sensor_name : "?");
	}
	if (i)
		printf("(all temps in degrees Kelvin)\n");

	return 0;
}

int cmd_thermal_set_threshold_v1(int argc, char *argv[])
{
	struct ec_params_thermal_get_threshold_v1 p;
	struct ec_thermal_config r;
	struct ec_params_thermal_set_threshold_v1 s;
	int i, n, val, rv;
	char *e;

	if (argc < 3 || argc > 7) {
		printf("Usage: %s"
		       " sensor warn [high [shutdown [fan_off [fan_max]]]]\n",
		       argv[0]);
		return 1;
	}

	n = strtod(argv[1], &e);
	if (e && *e) {
		printf("arg %d is invalid\n", 1);
		return 1;
	}

	p.sensor_num = n;
	rv = ec_command(EC_CMD_THERMAL_GET_THRESHOLD, 1, &p, sizeof(p), &r,
			sizeof(r));
	if (rv <= 0)
		return rv;

	s.sensor_num = n;
	s.cfg = r;

	for (i = 2; i < argc; i++) {
		val = strtod(argv[i], &e);
		if (e && *e) {
			printf("arg %d is invalid\n", i);
			return 1;
		}

		if (val < 0)
			continue;
		switch (i) {
		case 2:
		case 3:
		case 4:
			s.cfg.temp_host[i - 2] = val;
			break;
		case 5:
			s.cfg.temp_fan_off = val;
			break;
		case 6:
			s.cfg.temp_fan_max = val;
			break;
		}
	}

	rv = ec_command(EC_CMD_THERMAL_SET_THRESHOLD, 1, &s, sizeof(s), NULL,
			0);

	return rv;
}

int cmd_thermal_get_threshold(int argc, char *argv[])
{
	if (ec_cmd_version_supported(EC_CMD_THERMAL_GET_THRESHOLD, 1))
		return cmd_thermal_get_threshold_v1(argc, argv);
	else if (ec_cmd_version_supported(EC_CMD_THERMAL_GET_THRESHOLD, 0))
		return cmd_thermal_get_threshold_v0(argc, argv);

	printf("I got nuthin.\n");
	return -1;
}

int cmd_thermal_set_threshold(int argc, char *argv[])
{
	if (ec_cmd_version_supported(EC_CMD_THERMAL_SET_THRESHOLD, 1))
		return cmd_thermal_set_threshold_v1(argc, argv);
	else if (ec_cmd_version_supported(EC_CMD_THERMAL_SET_THRESHOLD, 0))
		return cmd_thermal_set_threshold_v0(argc, argv);

	printf("I got nuthin.\n");
	return -1;
}

static int get_num_fans(void)
{
	int idx, rv;
	struct ec_response_get_features r;

	/*
	 * iff the EC supports the GET_FEATURES,
	 * check whether it has fan support enabled.
	 */
	rv = ec_command(EC_CMD_GET_FEATURES, 0, NULL, 0, &r, sizeof(r));
	if (rv >= 0 && !(r.flags[0] & BIT(EC_FEATURE_PWM_FAN)))
		return 0;

	for (idx = 0; idx < EC_FAN_SPEED_ENTRIES; idx++) {
		rv = read_mapped_mem16(EC_MEMMAP_FAN + 2 * idx);
		if (rv == EC_FAN_SPEED_NOT_PRESENT)
			break;
	}

	return idx;
}

int cmd_thermal_auto_fan_ctrl(int argc, char *argv[])
{
	int rv, num_fans;
	struct ec_params_auto_fan_ctrl_v1 p_v1;
	char *e;
	int cmdver = 1;

	if (!ec_cmd_version_supported(EC_CMD_THERMAL_AUTO_FAN_CTRL, cmdver) ||
	    (argc == 1)) {
		/* If no argument is provided then enable auto fan ctrl */
		/* for all fans by using version 0 of the host command */

		rv = ec_command(EC_CMD_THERMAL_AUTO_FAN_CTRL, 0, NULL, 0, NULL,
				0);
		if (rv < 0)
			return rv;

		printf("Automatic fan control is now on for all fans.\n");
		return 0;
	}

	if (argc > 2 || !strcmp(argv[1], "help")) {
		printf("Usage: %s [idx]\n", argv[0]);
		return -1;
	}

	num_fans = get_num_fans();
	p_v1.fan_idx = strtol(argv[1], &e, 0);
	if ((e && *e) || (p_v1.fan_idx >= num_fans)) {
		fprintf(stderr, "Bad fan index.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_THERMAL_AUTO_FAN_CTRL, cmdver, &p_v1,
			sizeof(p_v1), NULL, 0);
	if (rv < 0)
		return rv;

	printf("Automatic fan control is now on for fan %d\n", p_v1.fan_idx);

	return 0;
}

static int print_fan(int idx)
{
	int rv = read_mapped_mem16(EC_MEMMAP_FAN + 2 * idx);

	switch (rv) {
	case EC_FAN_SPEED_NOT_PRESENT:
		return -1;
	case EC_FAN_SPEED_STALLED_DEPRECATED:
		printf("Fan %d stalled (RPM: %d)\n", idx, rv);
		break;
	default:
		printf("Fan %d RPM: %d\n", idx, rv);
		break;
	}

	return 0;
}

int cmd_pwm_get_num_fans(int argc, char *argv[])
{
	int num_fans;

	num_fans = get_num_fans();

	printf("Number of fans = %d\n", num_fans);

	return 0;
}

int cmd_pwm_get_fan_rpm(int argc, char *argv[])
{
	int i, num_fans;

	num_fans = get_num_fans();
	if (argc < 2 || !strcasecmp(argv[1], "all")) {
		/* Print all the fan speeds */
		for (i = 0; i < num_fans; i++)
			print_fan(i);
	} else {
		char *e;
		int idx;

		idx = strtol(argv[1], &e, 0);
		if ((e && *e) || idx < 0 || idx >= num_fans) {
			fprintf(stderr, "Bad index.\n");
			return -1;
		}

		print_fan(idx);
	}

	return 0;
}

int cmd_pwm_set_fan_rpm(int argc, char *argv[])
{
	struct ec_params_pwm_set_fan_target_rpm_v1 p_v1;
	char *e;
	int rv, num_fans;
	int cmdver = 1;

	if (!ec_cmd_version_supported(EC_CMD_PWM_SET_FAN_TARGET_RPM, cmdver)) {
		struct ec_params_pwm_set_fan_target_rpm_v0 p_v0;

		/* Fall back to command version 0 command */
		cmdver = 0;

		if (argc != 2) {
			fprintf(stderr, "Usage: %s <targetrpm>\n", argv[0]);
			return -1;
		}
		p_v0.rpm = strtol(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad RPM.\n");
			return -1;
		}

		rv = ec_command(EC_CMD_PWM_SET_FAN_TARGET_RPM, cmdver, &p_v0,
				sizeof(p_v0), NULL, 0);
		if (rv < 0)
			return rv;

		printf("Fan target RPM set for all fans.\n");
		return 0;
	}

	if (argc > 3 || (argc == 2 && !strcmp(argv[1], "help")) || argc == 1) {
		printf("Usage: %s [idx] <targetrpm>\n", argv[0]);
		printf("'%s 0 3000' - Set fan 0 RPM to 3000\n", argv[0]);
		printf("'%s 3000' - Set all fans RPM to 3000\n", argv[0]);
		return -1;
	}

	num_fans = get_num_fans();
	p_v1.rpm = strtol(argv[argc - 1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad RPM.\n");
		return -1;
	}

	if (argc == 2) {
		/* Reuse version 0 command if we're setting targetrpm
		 * for all fans */
		struct ec_params_pwm_set_fan_target_rpm_v0 p_v0;

		cmdver = 0;
		p_v0.rpm = p_v1.rpm;

		rv = ec_command(EC_CMD_PWM_SET_FAN_TARGET_RPM, cmdver, &p_v0,
				sizeof(p_v0), NULL, 0);
		if (rv < 0)
			return rv;

		printf("Fan target RPM set for all fans.\n");
	} else {
		p_v1.fan_idx = strtol(argv[1], &e, 0);
		if ((e && *e) || (p_v1.fan_idx >= num_fans)) {
			fprintf(stderr, "Bad fan index.\n");
			return -1;
		}

		rv = ec_command(EC_CMD_PWM_SET_FAN_TARGET_RPM, cmdver, &p_v1,
				sizeof(p_v1), NULL, 0);
		if (rv < 0)
			return rv;

		printf("Fan %d target RPM set.\n", p_v1.fan_idx);
	}

	return 0;
}

int cmd_pwm_get_keyboard_backlight(int argc, char *argv[])
{
	struct ec_response_pwm_get_keyboard_backlight r;
	int rv;

	rv = ec_command(EC_CMD_PWM_GET_KEYBOARD_BACKLIGHT, 0, NULL, 0, &r,
			sizeof(r));
	if (rv < 0)
		return rv;

	if (r.enabled == 1)
		printf("Current keyboard backlight percent: %d\n", r.percent);
	else
		printf("Keyboard backlight disabled.\n");

	return 0;
}

int cmd_pwm_set_keyboard_backlight(int argc, char *argv[])
{
	struct ec_params_pwm_set_keyboard_backlight p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <percent>\n", argv[0]);
		return -1;
	}
	p.percent = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad percent.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT, 0, &p, sizeof(p),
			NULL, 0);
	if (rv < 0)
		return rv;

	printf("Keyboard backlight set.\n");
	return 0;
}

int cmd_pwm_get_duty(int argc, char *argv[])
{
	struct ec_params_pwm_get_duty p;
	struct ec_response_pwm_get_duty r;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <pwm_idx> | kb | disp\n", argv[0]);
		return -1;
	}

	if (!strcmp(argv[1], "kb")) {
		p.pwm_type = EC_PWM_TYPE_KB_LIGHT;
		p.index = 0;
	} else if (!strcmp(argv[1], "disp")) {
		p.pwm_type = EC_PWM_TYPE_DISPLAY_LIGHT;
		p.index = 0;
	} else {
		p.pwm_type = EC_PWM_TYPE_GENERIC;
		p.index = strtol(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad pwm_idx\n");
			return -1;
		}
	}

	rv = ec_command(EC_CMD_PWM_GET_DUTY, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("Current PWM duty: %d\n", r.duty);
	return 0;
}

int cmd_pwm_set_duty(int argc, char *argv[])
{
	struct ec_params_pwm_set_duty p;
	char *e;
	int rv;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <pwm_idx> | kb | disp <duty>\n",
			argv[0]);
		return -1;
	}

	if (!strcmp(argv[1], "kb")) {
		p.pwm_type = EC_PWM_TYPE_KB_LIGHT;
		p.index = 0;
	} else if (!strcmp(argv[1], "disp")) {
		p.pwm_type = EC_PWM_TYPE_DISPLAY_LIGHT;
		p.index = 0;
	} else {
		p.pwm_type = EC_PWM_TYPE_GENERIC;
		p.index = strtol(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad pwm_idx\n");
			return -1;
		}
	}

	p.duty = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad duty.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_PWM_SET_DUTY, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("PWM set.\n");
	return 0;
}

int cmd_fanduty(int argc, char *argv[])
{
	struct ec_params_pwm_set_fan_duty_v1 p_v1;
	char *e;
	int rv, num_fans;
	int cmdver = 1;

	if (!ec_cmd_version_supported(EC_CMD_PWM_SET_FAN_DUTY, cmdver)) {
		struct ec_params_pwm_set_fan_duty_v0 p_v0;

		if (argc != 2) {
			fprintf(stderr, "Usage: %s <percent>\n", argv[0]);
			return -1;
		}
		p_v0.percent = strtol(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad percent arg.\n");
			return -1;
		}

		rv = ec_command(EC_CMD_PWM_SET_FAN_DUTY, 0, &p_v0, sizeof(p_v0),
				NULL, 0);
		if (rv < 0)
			return rv;

		printf("Fan duty cycle set.\n");
		return 0;
	}

	if (argc > 3 || (argc == 2 && !strcmp(argv[1], "help")) || argc == 1) {
		printf("Usage: %s [idx] <percent>\n", argv[0]);
		printf("'%s 0 50' - Set fan 0 duty cycle to 50 percent\n",
		       argv[0]);
		printf("'%s 30' - Set all fans duty cycle to 30 percent\n",
		       argv[0]);
		return -1;
	}

	num_fans = get_num_fans();
	p_v1.percent = strtol(argv[argc - 1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad percent arg.\n");
		return -1;
	}

	if (argc == 2) {
		/* Reuse version 0 command if we're setting duty cycle
		 * for all fans */
		struct ec_params_pwm_set_fan_duty_v0 p_v0;

		cmdver = 0;
		p_v0.percent = p_v1.percent;

		rv = ec_command(EC_CMD_PWM_SET_FAN_DUTY, cmdver, &p_v0,
				sizeof(p_v0), NULL, 0);
		if (rv < 0)
			return rv;

		printf("Fan duty cycle set for all fans.\n");
	} else {
		p_v1.fan_idx = strtol(argv[1], &e, 0);
		if ((e && *e) || (p_v1.fan_idx >= num_fans)) {
			fprintf(stderr, "Bad fan index.\n");
			return -1;
		}

		rv = ec_command(EC_CMD_PWM_SET_FAN_DUTY, cmdver, &p_v1,
				sizeof(p_v1), NULL, 0);
		if (rv < 0)
			return rv;

		printf("Fan %d duty cycle set.\n", p_v1.fan_idx);
	}

	return 0;
}

#define LBMSG(state) #state
#include "lightbar_msg_list.h"
static const char *const lightbar_cmds[] = { LIGHTBAR_MSG_LIST };
#undef LBMSG

/* Size of field <FLD> in structure <ST> */
#define ST_FLD_SIZE(ST, FLD) sizeof(((struct ST *)0)->FLD)

#define ST_CMD_SIZE ST_FLD_SIZE(ec_params_lightbar, cmd)
#define ST_PRM_SIZE(SUBCMD) \
	(ST_CMD_SIZE + ST_FLD_SIZE(ec_params_lightbar, SUBCMD))
#define ST_RSP_SIZE(SUBCMD) ST_FLD_SIZE(ec_response_lightbar, SUBCMD)

static const struct {
	uint8_t insize;
	uint8_t outsize;
} lb_command_paramcount[] = {
	{ ST_CMD_SIZE, ST_RSP_SIZE(dump) },
	{ ST_CMD_SIZE, 0 },
	{ ST_CMD_SIZE, 0 },
	{ ST_CMD_SIZE, 0 },
	{ ST_PRM_SIZE(set_brightness), 0 },
	{ ST_PRM_SIZE(seq), 0 },
	{ ST_PRM_SIZE(reg), 0 },
	{ ST_PRM_SIZE(set_rgb), 0 },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_seq) },
	{ ST_PRM_SIZE(demo), 0 },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v0) },
	{ ST_PRM_SIZE(set_params_v0), 0 },
	{ ST_CMD_SIZE, ST_RSP_SIZE(version) },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_brightness) },
	{ ST_PRM_SIZE(get_rgb), ST_RSP_SIZE(get_rgb) },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_demo) },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v1) },
	{ ST_PRM_SIZE(set_params_v1), 0 },
	{ ST_PRM_SIZE(set_program), 0 },
	{ ST_PRM_SIZE(manual_suspend_ctrl), 0 },
	{ ST_CMD_SIZE, 0 },
	{ ST_CMD_SIZE, 0 },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v2_timing) },
	{ ST_PRM_SIZE(set_v2par_timing), 0 },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v2_tap) },
	{ ST_PRM_SIZE(set_v2par_tap), 0 },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v2_osc) },
	{ ST_PRM_SIZE(set_v2par_osc), 0 },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v2_bright) },
	{ ST_PRM_SIZE(set_v2par_bright), 0 },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v2_thlds) },
	{ ST_PRM_SIZE(set_v2par_thlds), 0 },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v2_colors) },
	{ ST_PRM_SIZE(set_v2par_colors), 0 },
};
BUILD_ASSERT(ARRAY_SIZE(lb_command_paramcount) == LIGHTBAR_NUM_CMDS);

#undef ST_CMD_SIZE
#undef ST_PRM_SIZE
#undef ST_RSP_SIZE

static int lb_help(const char *cmd)
{
	printf("Usage:\n");
	printf("  %s                         - dump all regs\n", cmd);
	printf("  %s off                     - enter standby\n", cmd);
	printf("  %s on                      - leave standby\n", cmd);
	printf("  %s init                    - load default vals\n", cmd);
	printf("  %s brightness [NUM]        - get/set intensity(0-ff)\n", cmd);
	printf("  %s seq [NUM|SEQUENCE]      - run given pattern"
	       " (no arg for list)\n",
	       cmd);
	printf("  %s CTRL REG VAL            - set LED controller regs\n", cmd);
	printf("  %s LED RED GREEN BLUE      - set color manually"
	       " (LED=4 for all)\n",
	       cmd);
	printf("  %s LED                     - get current LED color\n", cmd);
	printf("  %s demo [0|1]              - turn demo mode on & off\n", cmd);
	printf("  %s params [setfile]        - get params"
	       " (or set from file)\n",
	       cmd);
	printf("  %s params2 group [setfile] - get params by group\n"
	       " (or set from file)\n",
	       cmd);
	printf("  %s program file            - load program from file\n", cmd);
	return 0;
}

static uint8_t lb_find_msg_by_name(const char *str)
{
	uint8_t i;
	for (i = 0; i < LIGHTBAR_NUM_SEQUENCES; i++)
		if (!strcasecmp(str, lightbar_cmds[i]))
			return i;

	return LIGHTBAR_NUM_SEQUENCES;
}

static int lb_do_cmd(enum lightbar_command cmd, struct ec_params_lightbar *in,
		     struct ec_response_lightbar *out)
{
	int rv;
	in->cmd = cmd;
	rv = ec_command(EC_CMD_LIGHTBAR_CMD, 0, in,
			lb_command_paramcount[cmd].insize, out,
			lb_command_paramcount[cmd].outsize);
	return (rv < 0 ? rv : 0);
}

static int lb_show_msg_names(void)
{
	int i, current_state;
	struct ec_params_lightbar param;
	struct ec_response_lightbar resp;

	i = lb_do_cmd(LIGHTBAR_CMD_GET_SEQ, &param, &resp);
	if (i < 0)
		return i;
	current_state = resp.get_seq.num;

	printf("sequence names:");
	for (i = 0; i < LIGHTBAR_NUM_SEQUENCES; i++)
		printf(" %s", lightbar_cmds[i]);
	printf("\nCurrent = 0x%x %s\n", current_state,
	       lightbar_cmds[current_state]);

	return 0;
}

static int lb_read_params_v0_from_file(const char *filename,
				       struct lightbar_params_v0 *p)
{
	FILE *fp;
	char buf[80];
	int val[4];
	int r = 1;
	int line = 0;
	int want, got;
	int i;

	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open %s: %s\n", filename,
			strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N)                                                             \
	do {                                                                \
		line++;                                                     \
		want = (N);                                                 \
		got = -1;                                                   \
		if (!fgets(buf, sizeof(buf), fp))                           \
			goto done;                                          \
		got = sscanf(buf, "%i %i %i %i", &val[0], &val[1], &val[2], \
			     &val[3]);                                      \
		if (want != got)                                            \
			goto done;                                          \
	} while (0)

	/* Do it */
	READ(1);
	p->google_ramp_up = val[0];
	READ(1);
	p->google_ramp_down = val[0];
	READ(1);
	p->s3s0_ramp_up = val[0];
	READ(1);
	p->s0_tick_delay[0] = val[0];
	READ(1);
	p->s0_tick_delay[1] = val[0];
	READ(1);
	p->s0a_tick_delay[0] = val[0];
	READ(1);
	p->s0a_tick_delay[1] = val[0];
	READ(1);
	p->s0s3_ramp_down = val[0];
	READ(1);
	p->s3_sleep_for = val[0];
	READ(1);
	p->s3_ramp_up = val[0];
	READ(1);
	p->s3_ramp_down = val[0];
	READ(1);
	p->new_s0 = val[0];

	READ(2);
	p->osc_min[0] = val[0];
	p->osc_min[1] = val[1];
	READ(2);
	p->osc_max[0] = val[0];
	p->osc_max[1] = val[1];
	READ(2);
	p->w_ofs[0] = val[0];
	p->w_ofs[1] = val[1];

	READ(2);
	p->bright_bl_off_fixed[0] = val[0];
	p->bright_bl_off_fixed[1] = val[1];

	READ(2);
	p->bright_bl_on_min[0] = val[0];
	p->bright_bl_on_min[1] = val[1];

	READ(2);
	p->bright_bl_on_max[0] = val[0];
	p->bright_bl_on_max[1] = val[1];

	READ(3);
	p->battery_threshold[0] = val[0];
	p->battery_threshold[1] = val[1];
	p->battery_threshold[2] = val[2];

	READ(4);
	p->s0_idx[0][0] = val[0];
	p->s0_idx[0][1] = val[1];
	p->s0_idx[0][2] = val[2];
	p->s0_idx[0][3] = val[3];

	READ(4);
	p->s0_idx[1][0] = val[0];
	p->s0_idx[1][1] = val[1];
	p->s0_idx[1][2] = val[2];
	p->s0_idx[1][3] = val[3];

	READ(4);
	p->s3_idx[0][0] = val[0];
	p->s3_idx[0][1] = val[1];
	p->s3_idx[0][2] = val[2];
	p->s3_idx[0][3] = val[3];

	READ(4);
	p->s3_idx[1][0] = val[0];
	p->s3_idx[1][1] = val[1];
	p->s3_idx[1][2] = val[2];
	p->s3_idx[1][3] = val[3];

	for (i = 0; i < ARRAY_SIZE(p->color); i++) {
		READ(3);
		p->color[i].r = val[0];
		p->color[i].g = val[1];
		p->color[i].b = val[2];
	}

#undef READ

	/* Yay */
	r = 0;
done:
	if (r)
		fprintf(stderr, "problem with line %d: wanted %d, got %d\n",
			line, want, got);
	fclose(fp);
	return r;
}

static void lb_show_params_v0(const struct lightbar_params_v0 *p)
{
	int i;

	printf("%d\t\t# .google_ramp_up\n", p->google_ramp_up);
	printf("%d\t\t# .google_ramp_down\n", p->google_ramp_down);
	printf("%d\t\t# .s3s0_ramp_up\n", p->s3s0_ramp_up);
	printf("%d\t\t# .s0_tick_delay (battery)\n", p->s0_tick_delay[0]);
	printf("%d\t\t# .s0_tick_delay (AC)\n", p->s0_tick_delay[1]);
	printf("%d\t\t# .s0a_tick_delay (battery)\n", p->s0a_tick_delay[0]);
	printf("%d\t\t# .s0a_tick_delay (AC)\n", p->s0a_tick_delay[1]);
	printf("%d\t\t# .s0s3_ramp_down\n", p->s0s3_ramp_down);
	printf("%d\t# .s3_sleep_for\n", p->s3_sleep_for);
	printf("%d\t\t# .s3_ramp_up\n", p->s3_ramp_up);
	printf("%d\t\t# .s3_ramp_down\n", p->s3_ramp_down);
	printf("%d\t\t# .new_s0\n", p->new_s0);
	printf("0x%02x 0x%02x\t# .osc_min (battery, AC)\n", p->osc_min[0],
	       p->osc_min[1]);
	printf("0x%02x 0x%02x\t# .osc_max (battery, AC)\n", p->osc_max[0],
	       p->osc_max[1]);
	printf("%d %d\t\t# .w_ofs (battery, AC)\n", p->w_ofs[0], p->w_ofs[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_off_fixed (battery, AC)\n",
	       p->bright_bl_off_fixed[0], p->bright_bl_off_fixed[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_on_min (battery, AC)\n",
	       p->bright_bl_on_min[0], p->bright_bl_on_min[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_on_max (battery, AC)\n",
	       p->bright_bl_on_max[0], p->bright_bl_on_max[1]);
	printf("%d %d %d\t\t# .battery_threshold\n", p->battery_threshold[0],
	       p->battery_threshold[1], p->battery_threshold[2]);
	printf("%d %d %d %d\t\t# .s0_idx[] (battery)\n", p->s0_idx[0][0],
	       p->s0_idx[0][1], p->s0_idx[0][2], p->s0_idx[0][3]);
	printf("%d %d %d %d\t\t# .s0_idx[] (AC)\n", p->s0_idx[1][0],
	       p->s0_idx[1][1], p->s0_idx[1][2], p->s0_idx[1][3]);
	printf("%d %d %d %d\t# .s3_idx[] (battery)\n", p->s3_idx[0][0],
	       p->s3_idx[0][1], p->s3_idx[0][2], p->s3_idx[0][3]);
	printf("%d %d %d %d\t# .s3_idx[] (AC)\n", p->s3_idx[1][0],
	       p->s3_idx[1][1], p->s3_idx[1][2], p->s3_idx[1][3]);
	for (i = 0; i < ARRAY_SIZE(p->color); i++)
		printf("0x%02x 0x%02x 0x%02x\t# color[%d]\n", p->color[i].r,
		       p->color[i].g, p->color[i].b, i);
}

static int lb_read_params_v1_from_file(const char *filename,
				       struct lightbar_params_v1 *p)
{
	FILE *fp;
	char buf[80];
	int val[4];
	int r = 1;
	int line = 0;
	int want, got;
	int i;

	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open %s: %s\n", filename,
			strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N)                                                             \
	do {                                                                \
		line++;                                                     \
		want = (N);                                                 \
		got = -1;                                                   \
		if (!fgets(buf, sizeof(buf), fp))                           \
			goto done;                                          \
		got = sscanf(buf, "%i %i %i %i", &val[0], &val[1], &val[2], \
			     &val[3]);                                      \
		if (want != got)                                            \
			goto done;                                          \
	} while (0)

	/* Do it */
	READ(1);
	p->google_ramp_up = val[0];
	READ(1);
	p->google_ramp_down = val[0];
	READ(1);
	p->s3s0_ramp_up = val[0];
	READ(1);
	p->s0_tick_delay[0] = val[0];
	READ(1);
	p->s0_tick_delay[1] = val[0];
	READ(1);
	p->s0a_tick_delay[0] = val[0];
	READ(1);
	p->s0a_tick_delay[1] = val[0];
	READ(1);
	p->s0s3_ramp_down = val[0];
	READ(1);
	p->s3_sleep_for = val[0];
	READ(1);
	p->s3_ramp_up = val[0];
	READ(1);
	p->s3_ramp_down = val[0];
	READ(1);
	p->tap_tick_delay = val[0];
	READ(1);
	p->tap_gate_delay = val[0];
	READ(1);
	p->tap_display_time = val[0];

	READ(1);
	p->tap_pct_red = val[0];
	READ(1);
	p->tap_pct_green = val[0];
	READ(1);
	p->tap_seg_min_on = val[0];
	READ(1);
	p->tap_seg_max_on = val[0];
	READ(1);
	p->tap_seg_osc = val[0];
	READ(3);
	p->tap_idx[0] = val[0];
	p->tap_idx[1] = val[1];
	p->tap_idx[2] = val[2];

	READ(2);
	p->osc_min[0] = val[0];
	p->osc_min[1] = val[1];
	READ(2);
	p->osc_max[0] = val[0];
	p->osc_max[1] = val[1];
	READ(2);
	p->w_ofs[0] = val[0];
	p->w_ofs[1] = val[1];

	READ(2);
	p->bright_bl_off_fixed[0] = val[0];
	p->bright_bl_off_fixed[1] = val[1];

	READ(2);
	p->bright_bl_on_min[0] = val[0];
	p->bright_bl_on_min[1] = val[1];

	READ(2);
	p->bright_bl_on_max[0] = val[0];
	p->bright_bl_on_max[1] = val[1];

	READ(3);
	p->battery_threshold[0] = val[0];
	p->battery_threshold[1] = val[1];
	p->battery_threshold[2] = val[2];

	READ(4);
	p->s0_idx[0][0] = val[0];
	p->s0_idx[0][1] = val[1];
	p->s0_idx[0][2] = val[2];
	p->s0_idx[0][3] = val[3];

	READ(4);
	p->s0_idx[1][0] = val[0];
	p->s0_idx[1][1] = val[1];
	p->s0_idx[1][2] = val[2];
	p->s0_idx[1][3] = val[3];

	READ(4);
	p->s3_idx[0][0] = val[0];
	p->s3_idx[0][1] = val[1];
	p->s3_idx[0][2] = val[2];
	p->s3_idx[0][3] = val[3];

	READ(4);
	p->s3_idx[1][0] = val[0];
	p->s3_idx[1][1] = val[1];
	p->s3_idx[1][2] = val[2];
	p->s3_idx[1][3] = val[3];

	for (i = 0; i < ARRAY_SIZE(p->color); i++) {
		READ(3);
		p->color[i].r = val[0];
		p->color[i].g = val[1];
		p->color[i].b = val[2];
	}

#undef READ

	/* Yay */
	r = 0;
done:
	if (r)
		fprintf(stderr, "problem with line %d: wanted %d, got %d\n",
			line, want, got);
	fclose(fp);
	return r;
}

static void lb_show_params_v1(const struct lightbar_params_v1 *p)
{
	int i;

	printf("%d\t\t# .google_ramp_up\n", p->google_ramp_up);
	printf("%d\t\t# .google_ramp_down\n", p->google_ramp_down);
	printf("%d\t\t# .s3s0_ramp_up\n", p->s3s0_ramp_up);
	printf("%d\t\t# .s0_tick_delay (battery)\n", p->s0_tick_delay[0]);
	printf("%d\t\t# .s0_tick_delay (AC)\n", p->s0_tick_delay[1]);
	printf("%d\t\t# .s0a_tick_delay (battery)\n", p->s0a_tick_delay[0]);
	printf("%d\t\t# .s0a_tick_delay (AC)\n", p->s0a_tick_delay[1]);
	printf("%d\t\t# .s0s3_ramp_down\n", p->s0s3_ramp_down);
	printf("%d\t\t# .s3_sleep_for\n", p->s3_sleep_for);
	printf("%d\t\t# .s3_ramp_up\n", p->s3_ramp_up);
	printf("%d\t\t# .s3_ramp_down\n", p->s3_ramp_down);
	printf("%d\t\t# .tap_tick_delay\n", p->tap_tick_delay);
	printf("%d\t\t# .tap_gate_delay\n", p->tap_gate_delay);
	printf("%d\t\t# .tap_display_time\n", p->tap_display_time);
	printf("%d\t\t# .tap_pct_red\n", p->tap_pct_red);
	printf("%d\t\t# .tap_pct_green\n", p->tap_pct_green);
	printf("%d\t\t# .tap_seg_min_on\n", p->tap_seg_min_on);
	printf("%d\t\t# .tap_seg_max_on\n", p->tap_seg_max_on);
	printf("%d\t\t# .tap_seg_osc\n", p->tap_seg_osc);
	printf("%d %d %d\t\t# .tap_idx\n", p->tap_idx[0], p->tap_idx[1],
	       p->tap_idx[2]);
	printf("0x%02x 0x%02x\t# .osc_min (battery, AC)\n", p->osc_min[0],
	       p->osc_min[1]);
	printf("0x%02x 0x%02x\t# .osc_max (battery, AC)\n", p->osc_max[0],
	       p->osc_max[1]);
	printf("%d %d\t\t# .w_ofs (battery, AC)\n", p->w_ofs[0], p->w_ofs[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_off_fixed (battery, AC)\n",
	       p->bright_bl_off_fixed[0], p->bright_bl_off_fixed[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_on_min (battery, AC)\n",
	       p->bright_bl_on_min[0], p->bright_bl_on_min[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_on_max (battery, AC)\n",
	       p->bright_bl_on_max[0], p->bright_bl_on_max[1]);
	printf("%d %d %d\t# .battery_threshold\n", p->battery_threshold[0],
	       p->battery_threshold[1], p->battery_threshold[2]);
	printf("%d %d %d %d\t\t# .s0_idx[] (battery)\n", p->s0_idx[0][0],
	       p->s0_idx[0][1], p->s0_idx[0][2], p->s0_idx[0][3]);
	printf("%d %d %d %d\t\t# .s0_idx[] (AC)\n", p->s0_idx[1][0],
	       p->s0_idx[1][1], p->s0_idx[1][2], p->s0_idx[1][3]);
	printf("%d %d %d %d\t# .s3_idx[] (battery)\n", p->s3_idx[0][0],
	       p->s3_idx[0][1], p->s3_idx[0][2], p->s3_idx[0][3]);
	printf("%d %d %d %d\t# .s3_idx[] (AC)\n", p->s3_idx[1][0],
	       p->s3_idx[1][1], p->s3_idx[1][2], p->s3_idx[1][3]);
	for (i = 0; i < ARRAY_SIZE(p->color); i++)
		printf("0x%02x 0x%02x 0x%02x\t# color[%d]\n", p->color[i].r,
		       p->color[i].g, p->color[i].b, i);
}

static int lb_rd_timing_v2par_from_file(const char *filename,
					struct lightbar_params_v2_timing *p)
{
	FILE *fp;
	char buf[80];
	int val[4];
	int r = 1;
	int line = 0;
	int want, got;

	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open %s: %s\n", filename,
			strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N)                                                             \
	do {                                                                \
		line++;                                                     \
		want = (N);                                                 \
		got = -1;                                                   \
		if (!fgets(buf, sizeof(buf), fp))                           \
			goto done;                                          \
		got = sscanf(buf, "%i %i %i %i", &val[0], &val[1], &val[2], \
			     &val[3]);                                      \
		if (want != got)                                            \
			goto done;                                          \
	} while (0)

	READ(1);
	p->google_ramp_up = val[0];
	READ(1);
	p->google_ramp_down = val[0];
	READ(1);
	p->s3s0_ramp_up = val[0];
	READ(1);
	p->s0_tick_delay[0] = val[0];
	READ(1);
	p->s0_tick_delay[1] = val[0];
	READ(1);
	p->s0a_tick_delay[0] = val[0];
	READ(1);
	p->s0a_tick_delay[1] = val[0];
	READ(1);
	p->s0s3_ramp_down = val[0];
	READ(1);
	p->s3_sleep_for = val[0];
	READ(1);
	p->s3_ramp_up = val[0];
	READ(1);
	p->s3_ramp_down = val[0];
	READ(1);
	p->tap_tick_delay = val[0];
	READ(1);
	p->tap_gate_delay = val[0];
	READ(1);
	p->tap_display_time = val[0];
#undef READ

	/* Yay */
	r = 0;
done:
	if (r)
		fprintf(stderr, "problem with line %d: wanted %d, got %d\n",
			line, want, got);
	fclose(fp);
	return r;
}

static int lb_rd_tap_v2par_from_file(const char *filename,
				     struct lightbar_params_v2_tap *p)
{
	FILE *fp;
	char buf[80];
	int val[4];
	int r = 1;
	int line = 0;
	int want, got;

	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open %s: %s\n", filename,
			strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N)                                                             \
	do {                                                                \
		line++;                                                     \
		want = (N);                                                 \
		got = -1;                                                   \
		if (!fgets(buf, sizeof(buf), fp))                           \
			goto done;                                          \
		got = sscanf(buf, "%i %i %i %i", &val[0], &val[1], &val[2], \
			     &val[3]);                                      \
		if (want != got)                                            \
			goto done;                                          \
	} while (0)

	READ(1);
	p->tap_pct_red = val[0];
	READ(1);
	p->tap_pct_green = val[0];
	READ(1);
	p->tap_seg_min_on = val[0];
	READ(1);
	p->tap_seg_max_on = val[0];
	READ(1);
	p->tap_seg_osc = val[0];
	READ(3);
	p->tap_idx[0] = val[0];
	p->tap_idx[1] = val[1];
	p->tap_idx[2] = val[2];
#undef READ

	/* Yay */
	r = 0;
done:
	if (r)
		fprintf(stderr, "problem with line %d: wanted %d, got %d\n",
			line, want, got);
	fclose(fp);
	return r;
}

static int lb_rd_osc_v2par_from_file(const char *filename,
				     struct lightbar_params_v2_oscillation *p)
{
	FILE *fp;
	char buf[80];
	int val[4];
	int r = 1;
	int line = 0;
	int want, got;

	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open %s: %s\n", filename,
			strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N)                                                             \
	do {                                                                \
		line++;                                                     \
		want = (N);                                                 \
		got = -1;                                                   \
		if (!fgets(buf, sizeof(buf), fp))                           \
			goto done;                                          \
		got = sscanf(buf, "%i %i %i %i", &val[0], &val[1], &val[2], \
			     &val[3]);                                      \
		if (want != got)                                            \
			goto done;                                          \
	} while (0)

	READ(2);
	p->osc_min[0] = val[0];
	p->osc_min[1] = val[1];
	READ(2);
	p->osc_max[0] = val[0];
	p->osc_max[1] = val[1];
	READ(2);
	p->w_ofs[0] = val[0];
	p->w_ofs[1] = val[1];
#undef READ

	/* Yay */
	r = 0;
done:
	if (r)
		fprintf(stderr, "problem with line %d: wanted %d, got %d\n",
			line, want, got);
	fclose(fp);
	return r;
}

static int lb_rd_bright_v2par_from_file(const char *filename,
					struct lightbar_params_v2_brightness *p)
{
	FILE *fp;
	char buf[80];
	int val[4];
	int r = 1;
	int line = 0;
	int want, got;

	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open %s: %s\n", filename,
			strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N)                                                             \
	do {                                                                \
		line++;                                                     \
		want = (N);                                                 \
		got = -1;                                                   \
		if (!fgets(buf, sizeof(buf), fp))                           \
			goto done;                                          \
		got = sscanf(buf, "%i %i %i %i", &val[0], &val[1], &val[2], \
			     &val[3]);                                      \
		if (want != got)                                            \
			goto done;                                          \
	} while (0)

	READ(2);
	p->bright_bl_off_fixed[0] = val[0];
	p->bright_bl_off_fixed[1] = val[1];

	READ(2);
	p->bright_bl_on_min[0] = val[0];
	p->bright_bl_on_min[1] = val[1];

	READ(2);
	p->bright_bl_on_max[0] = val[0];
	p->bright_bl_on_max[1] = val[1];
#undef READ

	/* Yay */
	r = 0;
done:
	if (r)
		fprintf(stderr, "problem with line %d: wanted %d, got %d\n",
			line, want, got);
	fclose(fp);
	return r;
}

static int lb_rd_thlds_v2par_from_file(const char *filename,
				       struct lightbar_params_v2_thresholds *p)
{
	FILE *fp;
	char buf[80];
	int val[4];
	int r = 1;
	int line = 0;
	int want, got;

	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open %s: %s\n", filename,
			strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N)                                                             \
	do {                                                                \
		line++;                                                     \
		want = (N);                                                 \
		got = -1;                                                   \
		if (!fgets(buf, sizeof(buf), fp))                           \
			goto done;                                          \
		got = sscanf(buf, "%i %i %i %i", &val[0], &val[1], &val[2], \
			     &val[3]);                                      \
		if (want != got)                                            \
			goto done;                                          \
	} while (0)

	READ(3);
	p->battery_threshold[0] = val[0];
	p->battery_threshold[1] = val[1];
	p->battery_threshold[2] = val[2];
#undef READ

	/* Yay */
	r = 0;
done:
	if (r)
		fprintf(stderr, "problem with line %d: wanted %d, got %d\n",
			line, want, got);
	fclose(fp);
	return r;
}

static int lb_rd_colors_v2par_from_file(const char *filename,
					struct lightbar_params_v2_colors *p)
{
	FILE *fp;
	char buf[80];
	int val[4];
	int r = 1;
	int line = 0;
	int want, got;
	int i;

	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open %s: %s\n", filename,
			strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N)                                                             \
	do {                                                                \
		line++;                                                     \
		want = (N);                                                 \
		got = -1;                                                   \
		if (!fgets(buf, sizeof(buf), fp))                           \
			goto done;                                          \
		got = sscanf(buf, "%i %i %i %i", &val[0], &val[1], &val[2], \
			     &val[3]);                                      \
		if (want != got)                                            \
			goto done;                                          \
	} while (0)

	READ(4);
	p->s0_idx[0][0] = val[0];
	p->s0_idx[0][1] = val[1];
	p->s0_idx[0][2] = val[2];
	p->s0_idx[0][3] = val[3];

	READ(4);
	p->s0_idx[1][0] = val[0];
	p->s0_idx[1][1] = val[1];
	p->s0_idx[1][2] = val[2];
	p->s0_idx[1][3] = val[3];

	READ(4);
	p->s3_idx[0][0] = val[0];
	p->s3_idx[0][1] = val[1];
	p->s3_idx[0][2] = val[2];
	p->s3_idx[0][3] = val[3];

	READ(4);
	p->s3_idx[1][0] = val[0];
	p->s3_idx[1][1] = val[1];
	p->s3_idx[1][2] = val[2];
	p->s3_idx[1][3] = val[3];
	for (i = 0; i < ARRAY_SIZE(p->color); i++) {
		READ(3);
		p->color[i].r = val[0];
		p->color[i].g = val[1];
		p->color[i].b = val[2];
	}

#undef READ

	/* Yay */
	r = 0;
done:
	if (r)
		fprintf(stderr, "problem with line %d: wanted %d, got %d\n",
			line, want, got);
	fclose(fp);
	return r;
}

static void lb_show_v2par_timing(const struct lightbar_params_v2_timing *p)
{
	printf("%d\t\t# .google_ramp_up\n", p->google_ramp_up);
	printf("%d\t\t# .google_ramp_down\n", p->google_ramp_down);
	printf("%d\t\t# .s3s0_ramp_up\n", p->s3s0_ramp_up);
	printf("%d\t\t# .s0_tick_delay (battery)\n", p->s0_tick_delay[0]);
	printf("%d\t\t# .s0_tick_delay (AC)\n", p->s0_tick_delay[1]);
	printf("%d\t\t# .s0a_tick_delay (battery)\n", p->s0a_tick_delay[0]);
	printf("%d\t\t# .s0a_tick_delay (AC)\n", p->s0a_tick_delay[1]);
	printf("%d\t\t# .s0s3_ramp_down\n", p->s0s3_ramp_down);
	printf("%d\t\t# .s3_sleep_for\n", p->s3_sleep_for);
	printf("%d\t\t# .s3_ramp_up\n", p->s3_ramp_up);
	printf("%d\t\t# .s3_ramp_down\n", p->s3_ramp_down);
	printf("%d\t\t# .tap_tick_delay\n", p->tap_tick_delay);
	printf("%d\t\t# .tap_gate_delay\n", p->tap_gate_delay);
	printf("%d\t\t# .tap_display_time\n", p->tap_display_time);
}

static void lb_show_v2par_tap(const struct lightbar_params_v2_tap *p)
{
	printf("%d\t\t# .tap_pct_red\n", p->tap_pct_red);
	printf("%d\t\t# .tap_pct_green\n", p->tap_pct_green);
	printf("%d\t\t# .tap_seg_min_on\n", p->tap_seg_min_on);
	printf("%d\t\t# .tap_seg_max_on\n", p->tap_seg_max_on);
	printf("%d\t\t# .tap_seg_osc\n", p->tap_seg_osc);
	printf("%d %d %d\t\t# .tap_idx\n", p->tap_idx[0], p->tap_idx[1],
	       p->tap_idx[2]);
}

static void lb_show_v2par_osc(const struct lightbar_params_v2_oscillation *p)
{
	printf("0x%02x 0x%02x\t# .osc_min (battery, AC)\n", p->osc_min[0],
	       p->osc_min[1]);
	printf("0x%02x 0x%02x\t# .osc_max (battery, AC)\n", p->osc_max[0],
	       p->osc_max[1]);
	printf("%d %d\t\t# .w_ofs (battery, AC)\n", p->w_ofs[0], p->w_ofs[1]);
}

static void lb_show_v2par_bright(const struct lightbar_params_v2_brightness *p)
{
	printf("0x%02x 0x%02x\t# .bright_bl_off_fixed (battery, AC)\n",
	       p->bright_bl_off_fixed[0], p->bright_bl_off_fixed[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_on_min (battery, AC)\n",
	       p->bright_bl_on_min[0], p->bright_bl_on_min[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_on_max (battery, AC)\n",
	       p->bright_bl_on_max[0], p->bright_bl_on_max[1]);
}

static void lb_show_v2par_thlds(const struct lightbar_params_v2_thresholds *p)
{
	printf("%d %d %d\t# .battery_threshold\n", p->battery_threshold[0],
	       p->battery_threshold[1], p->battery_threshold[2]);
}

static void lb_show_v2par_colors(const struct lightbar_params_v2_colors *p)
{
	int i;

	printf("%d %d %d %d\t\t# .s0_idx[] (battery)\n", p->s0_idx[0][0],
	       p->s0_idx[0][1], p->s0_idx[0][2], p->s0_idx[0][3]);
	printf("%d %d %d %d\t\t# .s0_idx[] (AC)\n", p->s0_idx[1][0],
	       p->s0_idx[1][1], p->s0_idx[1][2], p->s0_idx[1][3]);
	printf("%d %d %d %d\t# .s3_idx[] (battery)\n", p->s3_idx[0][0],
	       p->s3_idx[0][1], p->s3_idx[0][2], p->s3_idx[0][3]);
	printf("%d %d %d %d\t# .s3_idx[] (AC)\n", p->s3_idx[1][0],
	       p->s3_idx[1][1], p->s3_idx[1][2], p->s3_idx[1][3]);

	for (i = 0; i < ARRAY_SIZE(p->color); i++)
		printf("0x%02x 0x%02x 0x%02x\t# color[%d]\n", p->color[i].r,
		       p->color[i].g, p->color[i].b, i);
}

static int lb_load_program(const char *filename, struct lightbar_program *prog)
{
	FILE *fp;
	size_t got;
	int rc;

	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open %s: %s\n", filename,
			strerror(errno));
		return 1;
	}

	rc = fseek(fp, 0, SEEK_END);
	if (rc) {
		fprintf(stderr, "Couldn't find end of file %s", filename);
		fclose(fp);
		return 1;
	}
	rc = (int)ftell(fp);
	if (rc > EC_LB_PROG_LEN) {
		fprintf(stderr, "File %s is too long, aborting\n", filename);
		fclose(fp);
		return 1;
	}
	rewind(fp);

	memset(prog->data, 0, EC_LB_PROG_LEN);
	got = fread(prog->data, 1, EC_LB_PROG_LEN, fp);
	if (rc != got)
		fprintf(stderr, "Warning: did not read entire file\n");
	prog->size = got;
	fclose(fp);
	return 0;
}

static int cmd_lightbar_params_v0(int argc, char **argv)
{
	struct ec_params_lightbar param;
	struct ec_response_lightbar resp;
	int r;

	if (argc > 2) {
		r = lb_read_params_v0_from_file(argv[2], &param.set_params_v0);
		if (r)
			return r;
		return lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V0, &param, &resp);
	}
	r = lb_do_cmd(LIGHTBAR_CMD_GET_PARAMS_V0, &param, &resp);
	if (!r)
		lb_show_params_v0(&resp.get_params_v0);
	return r;
}

static int cmd_lightbar_params_v1(int argc, char **argv)
{
	struct ec_params_lightbar param;
	struct ec_response_lightbar resp;
	int r;

	if (argc > 2) {
		r = lb_read_params_v1_from_file(argv[2], &param.set_params_v1);
		if (r)
			return r;
		return lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V1, &param, &resp);
	}
	r = lb_do_cmd(LIGHTBAR_CMD_GET_PARAMS_V1, &param, &resp);
	if (!r)
		lb_show_params_v1(&resp.get_params_v1);
	return r;
}

static void lb_param_v2_help(void)
{
	printf("Usage:\n");
	printf("lightbar params2 group [setfile]\n");
	printf("group list:\n");
	printf("  timing\n");
	printf("  tap\n");
	printf("  oscillation\n");
	printf("  brightness\n");
	printf("  thresholds\n");
	printf("  colors\n");

	return;
}

static int cmd_lightbar_params_v2(int argc, char **argv)
{
	struct ec_params_lightbar p;
	struct ec_response_lightbar resp;
	int r = 0;
	int set = 0;

	memset(&p, 0, sizeof(struct ec_params_lightbar));
	memset(&resp, 0, sizeof(struct ec_response_lightbar));

	if (argc < 3) {
		lb_param_v2_help();
		return 1;
	}

	/* Set new params if provided with a setfile */
	if (argc > 3)
		set = 1;

	/* Show selected v2 params */
	if (!strncasecmp(argv[2], "timing", 6)) {
		if (set) {
			r = lb_rd_timing_v2par_from_file(argv[3],
							 &p.set_v2par_timing);
			if (r)
				return r;
			r = lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V2_TIMING, &p,
				      &resp);
			if (r)
				return r;
		}
		r = lb_do_cmd(LIGHTBAR_CMD_GET_PARAMS_V2_TIMING, &p, &resp);
		if (r)
			return r;
		lb_show_v2par_timing(&resp.get_params_v2_timing);
	} else if (!strcasecmp(argv[2], "tap")) {
		if (set) {
			r = lb_rd_tap_v2par_from_file(argv[3],
						      &p.set_v2par_tap);
			if (r)
				return r;
			r = lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V2_TAP, &p,
				      &resp);
			if (r)
				return r;
		}
		r = lb_do_cmd(LIGHTBAR_CMD_GET_PARAMS_V2_TAP, &p, &resp);
		if (r)
			return r;
		lb_show_v2par_tap(&resp.get_params_v2_tap);
	} else if (!strncasecmp(argv[2], "oscillation", 11)) {
		if (set) {
			r = lb_rd_osc_v2par_from_file(argv[3],
						      &p.set_v2par_osc);
			if (r)
				return r;
			r = lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V2_OSCILLATION,
				      &p, &resp);
			if (r)
				return r;
		}
		r = lb_do_cmd(LIGHTBAR_CMD_GET_PARAMS_V2_OSCILLATION, &p,
			      &resp);
		if (r)
			return r;
		lb_show_v2par_osc(&resp.get_params_v2_osc);
	} else if (!strncasecmp(argv[2], "brightness", 10)) {
		if (set) {
			r = lb_rd_bright_v2par_from_file(argv[3],
							 &p.set_v2par_bright);
			if (r)
				return r;
			r = lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V2_BRIGHTNESS, &p,
				      &resp);
			if (r)
				return r;
		}
		r = lb_do_cmd(LIGHTBAR_CMD_GET_PARAMS_V2_BRIGHTNESS, &p, &resp);
		if (r)
			return r;
		lb_show_v2par_bright(&resp.get_params_v2_bright);
	} else if (!strncasecmp(argv[2], "thresholds", 10)) {
		if (set) {
			r = lb_rd_thlds_v2par_from_file(argv[3],
							&p.set_v2par_thlds);
			if (r)
				return r;
			r = lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V2_THRESHOLDS, &p,
				      &resp);
			if (r)
				return r;
		}
		r = lb_do_cmd(LIGHTBAR_CMD_GET_PARAMS_V2_THRESHOLDS, &p, &resp);
		if (r)
			return r;
		lb_show_v2par_thlds(&resp.get_params_v2_thlds);
	} else if (!strncasecmp(argv[2], "colors", 6)) {
		if (set) {
			r = lb_rd_colors_v2par_from_file(argv[3],
							 &p.set_v2par_colors);
			if (r)
				return r;
			r = lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V2_COLORS, &p,
				      &resp);
			if (r)
				return r;
		}
		r = lb_do_cmd(LIGHTBAR_CMD_GET_PARAMS_V2_COLORS, &p, &resp);
		if (r)
			return r;
		lb_show_v2par_colors(&resp.get_params_v2_colors);
	} else {
		lb_param_v2_help();
	}

	return r;
}

static int cmd_lightbar(int argc, char **argv)
{
	int i, r;
	struct ec_params_lightbar param;
	struct ec_response_lightbar resp;

	if (1 == argc) { /* no args = dump 'em all */
		r = lb_do_cmd(LIGHTBAR_CMD_DUMP, &param, &resp);
		if (r)
			return r;
		for (i = 0; i < ARRAY_SIZE(resp.dump.vals); i++) {
			printf(" %02x     %02x     %02x\n",
			       resp.dump.vals[i].reg, resp.dump.vals[i].ic0,
			       resp.dump.vals[i].ic1);
		}
		return 0;
	}

	if (argc == 2 && !strcasecmp(argv[1], "init"))
		return lb_do_cmd(LIGHTBAR_CMD_INIT, &param, &resp);

	if (argc == 2 && !strcasecmp(argv[1], "off"))
		return lb_do_cmd(LIGHTBAR_CMD_OFF, &param, &resp);

	if (argc == 2 && !strcasecmp(argv[1], "on"))
		return lb_do_cmd(LIGHTBAR_CMD_ON, &param, &resp);

	if (!strcasecmp(argv[1], "params0"))
		return cmd_lightbar_params_v0(argc, argv);

	if (!strcasecmp(argv[1], "params1"))
		return cmd_lightbar_params_v1(argc, argv);

	if (!strcasecmp(argv[1], "params2"))
		return cmd_lightbar_params_v2(argc, argv);

	if (!strcasecmp(argv[1], "params")) {
		/* Just try them both */
		fprintf(stderr, "trying params1 ...\n");
		if (0 == cmd_lightbar_params_v1(argc, argv))
			return 0;
		fprintf(stderr, "trying params0 ...\n");
		return cmd_lightbar_params_v0(argc, argv);
	}

	if (!strcasecmp(argv[1], "version")) {
		r = lb_do_cmd(LIGHTBAR_CMD_VERSION, &param, &resp);
		if (!r)
			printf("version %d flags 0x%x\n", resp.version.num,
			       resp.version.flags);
		return r;
	}

	if (argc > 1 && !strcasecmp(argv[1], "brightness")) {
		char *e;
		int rv;
		if (argc > 2) {
			param.set_brightness.num = 0xff &
						   strtoull(argv[2], &e, 16);
			return lb_do_cmd(LIGHTBAR_CMD_SET_BRIGHTNESS, &param,
					 &resp);
		}
		rv = lb_do_cmd(LIGHTBAR_CMD_GET_BRIGHTNESS, &param, &resp);
		if (rv)
			return rv;
		printf("%02x\n", resp.get_brightness.num);
		return 0;
	}

	if (argc > 1 && !strcasecmp(argv[1], "demo")) {
		int rv;
		if (argc > 2) {
			if (!strcasecmp(argv[2], "on") || argv[2][0] == '1')
				param.demo.num = 1;
			else if (!strcasecmp(argv[2], "off") ||
				 argv[2][0] == '0')
				param.demo.num = 0;
			else {
				fprintf(stderr, "Invalid arg\n");
				return -1;
			}
			return lb_do_cmd(LIGHTBAR_CMD_DEMO, &param, &resp);
		}

		rv = lb_do_cmd(LIGHTBAR_CMD_GET_DEMO, &param, &resp);
		if (rv)
			return rv;
		printf("%s\n", resp.get_demo.num ? "on" : "off");
		return 0;
	}

	if (argc >= 2 && !strcasecmp(argv[1], "seq")) {
		char *e;
		uint8_t num;
		if (argc == 2)
			return lb_show_msg_names();
		num = 0xff & strtoull(argv[2], &e, 16);
		if (e && *e)
			num = lb_find_msg_by_name(argv[2]);
		if (num >= LIGHTBAR_NUM_SEQUENCES) {
			fprintf(stderr, "Invalid arg\n");
			return -1;
		}
		param.seq.num = num;
		return lb_do_cmd(LIGHTBAR_CMD_SEQ, &param, &resp);
	}

	if (argc >= 3 && !strcasecmp(argv[1], "program")) {
		lb_load_program(argv[2], &param.set_program);
		return lb_do_cmd(LIGHTBAR_CMD_SET_PROGRAM, &param, &resp);
	}

	if (argc == 4) {
		char *e;
		param.reg.ctrl = 0xff & strtoull(argv[1], &e, 16);
		param.reg.reg = 0xff & strtoull(argv[2], &e, 16);
		param.reg.value = 0xff & strtoull(argv[3], &e, 16);
		return lb_do_cmd(LIGHTBAR_CMD_REG, &param, &resp);
	}

	if (argc == 5) {
		char *e;
		param.set_rgb.led = strtoull(argv[1], &e, 16);
		param.set_rgb.red = strtoull(argv[2], &e, 16);
		param.set_rgb.green = strtoull(argv[3], &e, 16);
		param.set_rgb.blue = strtoull(argv[4], &e, 16);
		return lb_do_cmd(LIGHTBAR_CMD_SET_RGB, &param, &resp);
	}

	/* Only thing left is to try to read an LED value */
	if (argc == 2) {
		char *e;
		param.get_rgb.led = strtoull(argv[1], &e, 0);
		if (!(e && *e)) {
			r = lb_do_cmd(LIGHTBAR_CMD_GET_RGB, &param, &resp);
			if (r)
				return r;
			printf("%02x %02x %02x\n", resp.get_rgb.red,
			       resp.get_rgb.green, resp.get_rgb.blue);
			return 0;
		}
	}

	return lb_help(argv[0]);
}

/* Create an array to store sizes of motion sense param and response structs. */
#define ST_CMD_SIZE ST_FLD_SIZE(ec_params_motion_sense, cmd)
#define ST_PRM_SIZE(SUBCMD) \
	(ST_CMD_SIZE + ST_FLD_SIZE(ec_params_motion_sense, SUBCMD))
#define ST_RSP_SIZE(SUBCMD) ST_FLD_SIZE(ec_response_motion_sense, SUBCMD)
#define ST_BOTH_SIZES(SUBCMD)                            \
	{                                                \
		ST_PRM_SIZE(SUBCMD), ST_RSP_SIZE(SUBCMD) \
	}

/*
 * For ectool only, assume no more than 16 sensors.  More advanced
 * implementation would allocate the right amount of memory depending on the
 * number of sensors.
 */
#define ECTOOL_MAX_SENSOR 16

static const struct {
	uint8_t outsize;
	uint8_t insize;
} ms_command_sizes[] = {
	{ ST_PRM_SIZE(dump),
	  ST_RSP_SIZE(dump) + sizeof(struct ec_response_motion_sensor_data) *
				      ECTOOL_MAX_SENSOR },
	ST_BOTH_SIZES(info_4),
	ST_BOTH_SIZES(ec_rate),
	ST_BOTH_SIZES(sensor_odr),
	ST_BOTH_SIZES(sensor_range),
	ST_BOTH_SIZES(kb_wake_angle),
	ST_BOTH_SIZES(data),
	{ ST_CMD_SIZE,
	  ST_RSP_SIZE(fifo_info) + sizeof(uint16_t) * ECTOOL_MAX_SENSOR },
	ST_BOTH_SIZES(fifo_flush),
	ST_BOTH_SIZES(fifo_read),
	ST_BOTH_SIZES(perform_calib),
	ST_BOTH_SIZES(sensor_offset),
	ST_BOTH_SIZES(list_activities),
	{ ST_PRM_SIZE(set_activity), 0 },
	{ ST_CMD_SIZE, ST_RSP_SIZE(lid_angle) },
	ST_BOTH_SIZES(fifo_int_enable),
	ST_BOTH_SIZES(spoof),
	ST_BOTH_SIZES(tablet_mode_threshold),
	ST_BOTH_SIZES(sensor_scale),
	ST_BOTH_SIZES(online_calib_read),
	ST_BOTH_SIZES(get_activity),
};
BUILD_ASSERT(ARRAY_SIZE(ms_command_sizes) == MOTIONSENSE_NUM_CMDS);

#undef ST_CMD_SIZE
#undef ST_PRM_SIZE
#undef ST_RSP_SIZE
#undef ST_BOTH_SIZES

static int ms_help(const char *cmd)
{
	printf("Usage:\n");
	printf("  %s                              - dump all motion data\n",
	       cmd);
	printf("  %s active                       - print active flag\n", cmd);
	printf("  %s info [NUM]                   - print sensor info\n", cmd);
	printf("  %s ec_rate NUM [RATE_MS]        - set/get sample rate\n",
	       cmd);
	printf("  %s odr NUM [ODR [ROUNDUP]]      - set/get sensor ODR\n", cmd);
	printf("  %s range NUM [RANGE [ROUNDUP]]  - set/get sensor range\n",
	       cmd);
	printf("  %s offset NUM [X Y Z [TEMP]]    - set/get sensor offset\n",
	       cmd);
	printf("  %s kb_wake NUM                  - set/get KB wake ang\n",
	       cmd);
	printf("  %s fifo_info                    - print fifo info\n", cmd);
	printf("  %s fifo_int_enable [0/1]        - enable/disable/get fifo "
	       "interrupt status\n",
	       cmd);
	printf("  %s fifo_read MAX_DATA           - read fifo data\n", cmd);
	printf("  %s fifo_flush NUM               - trigger fifo interrupt\n",
	       cmd);
	printf("  %s list_activities              - list supported "
	       "activities\n",
	       cmd);
	printf("  %s set_activity ACT EN          - enable/disable activity\n",
	       cmd);
	printf("  %s get_activity ACT             - get activity status\n",
	       cmd);
	printf("  %s lid_angle                    - print lid angle\n", cmd);
	printf("  %s spoof NUM [0/1] [X Y Z]      - enable/disable spoofing\n",
	       cmd);
	printf("  %s spoof NUM activity ACT [0/1] [STATE] - enable/disable "
	       "activity spoofing\n",
	       cmd);
	printf("  %s tablet_mode_angle ANG HYS    - set/get tablet mode "
	       "angle\n",
	       cmd);
	printf("  %s calibrate NUM                - run sensor calibration\n",
	       cmd);

	return 0;
}

static void motionsense_display_activities(uint32_t activities)
{
	if (activities & BIT(MOTIONSENSE_ACTIVITY_SIG_MOTION))
		printf("%d: Significant motion\n",
		       MOTIONSENSE_ACTIVITY_SIG_MOTION);
	if (activities & BIT(MOTIONSENSE_ACTIVITY_DOUBLE_TAP))
		printf("%d: Double tap\n", MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
	if (activities & BIT(MOTIONSENSE_ACTIVITY_ORIENTATION))
		printf("%d: Orientation\n", MOTIONSENSE_ACTIVITY_ORIENTATION);
	if (activities & BIT(MOTIONSENSE_ACTIVITY_BODY_DETECTION))
		printf("%d: Body Detection\n",
		       MOTIONSENSE_ACTIVITY_BODY_DETECTION);
}

static int cmd_motionsense(int argc, char **argv)
{
	int i, rv, status_only = (argc == 2);
	struct ec_params_motion_sense param;
	/* The largest size using resp as a response buffer */
	std::unique_ptr<uint8_t[]> resp_buffer_ptr =
		std::make_unique<uint8_t[]>(
			ms_command_sizes[MOTIONSENSE_CMD_DUMP].insize);
	struct ec_response_motion_sense *resp =
		(struct ec_response_motion_sense *)resp_buffer_ptr.get();
	char *e;
	/*
	 * Warning: the following strings printed out are read in an
	 * autotest. Do not change string without consulting autotest
	 * for kernel_CrosECSysfsAccel.
	 */
	const char *motion_status_string[2][2] = {
		{ "Motion sensing inactive", "0" },
		{ "Motion sensing active", "1" },
	};
	/* No motionsense command has more than 7 args. */
	if (argc > 7)
		return ms_help(argv[0]);

	if ((argc == 1) || (argc == 2 && !strcasecmp(argv[1], "active"))) {
		param.cmd = MOTIONSENSE_CMD_DUMP;
		param.dump.max_sensor_count = ECTOOL_MAX_SENSOR;
		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);
		if (rv > 0) {
			printf("%s\n", motion_status_string[!!(
					       resp->dump.module_flags &
					       MOTIONSENSE_MODULE_FLAG_ACTIVE)]
							   [status_only]);
			if (status_only)
				return 0;

			if (resp->dump.sensor_count > ECTOOL_MAX_SENSOR) {
				printf("Too many sensors to handle: %d",
				       resp->dump.sensor_count);
				return -1;
			}
			for (i = 0; i < resp->dump.sensor_count; i++) {
				/*
				 * Warning: the following string printed out
				 * is read by an autotest. Do not change string
				 * without consulting autotest for
				 * kernel_CrosECSysfsAccel.
				 */
				printf("Sensor %d: ", i);
				if (resp->dump.sensor[i].flags &
				    MOTIONSENSE_SENSOR_FLAG_PRESENT)
					printf("%d\t%d\t%d\n",
					       resp->dump.sensor[i].data[0],
					       resp->dump.sensor[i].data[1],
					       resp->dump.sensor[i].data[2]);
				else
					printf("None\n");
			}
			return 0;
		} else {
			return rv;
		}
	}

	if ((argc == 2 || argc == 3) && !strcasecmp(argv[1], "info")) {
		int version = 0;
		int loop_start;
		int loop_end;
		int i;

		rv = get_latest_cmd_version(EC_CMD_MOTION_SENSE_CMD, &version);
		if (rv < 0)
			return rv;

		if (argc == 2) {
			param.cmd = MOTIONSENSE_CMD_DUMP;
			param.dump.max_sensor_count = ECTOOL_MAX_SENSOR;
			rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1, &param,
					ms_command_sizes[param.cmd].outsize,
					resp,
					ms_command_sizes[param.cmd].insize);
			if (rv < 0)
				return rv;
			if (resp->dump.sensor_count > ECTOOL_MAX_SENSOR)
				return -1;

			loop_start = 0;
			loop_end = resp->dump.sensor_count;
		} else {
			loop_start = strtol(argv[2], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad %s arg.\n", argv[2]);
				return -1;
			}
			loop_end = loop_start + 1;
		}
		param.cmd = MOTIONSENSE_CMD_INFO;

		for (i = loop_start; i < loop_end; i++) {
			param.sensor_odr.sensor_num = i;

			if (argc == 2) {
				if (i != loop_start)
					printf("\n");
				printf("Index:    %d\n", i);
			}

			rv = ec_command(EC_CMD_MOTION_SENSE_CMD, version,
					&param,
					ms_command_sizes[param.cmd].outsize,
					resp,
					ms_command_sizes[param.cmd].insize);
			if (rv < 0) {
				/*
				 * Return the error code to a higher level if
				 * we're querying about a specific sensor; else
				 * just print the error.
				 */
				if (argc == 3)
					return rv;

				printf("Error: %d\n", rv);
				continue;
			}
			printf("Type:     ");
			switch (resp->info.type) {
			case MOTIONSENSE_TYPE_ACCEL:
				printf("accel\n");
				break;
			case MOTIONSENSE_TYPE_GYRO:
				printf("gyro\n");
				break;
			case MOTIONSENSE_TYPE_MAG:
				printf("magnetometer\n");
				break;
			case MOTIONSENSE_TYPE_LIGHT:
				printf("light\n");
				break;
			case MOTIONSENSE_TYPE_LIGHT_RGB:
				printf("rgb light\n");
				break;
			case MOTIONSENSE_TYPE_PROX:
				printf("proximity\n");
				break;
			case MOTIONSENSE_TYPE_ACTIVITY:
				printf("activity\n");
				break;
			case MOTIONSENSE_TYPE_BARO:
				printf("barometer\n");
				break;
			case MOTIONSENSE_TYPE_SYNC:
				printf("sync\n");
				break;
			default:
				printf("unknown\n");
			}

			printf("Location: ");
			switch (resp->info.location) {
			case MOTIONSENSE_LOC_BASE:
				printf("base\n");
				break;
			case MOTIONSENSE_LOC_LID:
				printf("lid\n");
				break;
			case MOTIONSENSE_LOC_CAMERA:
				printf("camera\n");
				break;
			default:
				printf("unknown\n");
			}

			printf("Chip:     ");
			switch (resp->info.chip) {
			case MOTIONSENSE_CHIP_KXCJ9:
				printf("kxcj9\n");
				break;
			case MOTIONSENSE_CHIP_LSM6DS0:
				printf("lsm6ds0\n");
				break;
			case MOTIONSENSE_CHIP_BMI160:
				printf("bmi160\n");
				break;
			case MOTIONSENSE_CHIP_SI1141:
				printf("si1141\n");
				break;
			case MOTIONSENSE_CHIP_KX022:
				printf("kx022\n");
				break;
			case MOTIONSENSE_CHIP_L3GD20H:
				printf("l3gd20h\n");
				break;
			case MOTIONSENSE_CHIP_BMA255:
				printf("bma255\n");
				break;
			case MOTIONSENSE_CHIP_BMP280:
				printf("bmp280\n");
				break;
			case MOTIONSENSE_CHIP_OPT3001:
				printf("opt3001\n");
				break;
			case MOTIONSENSE_CHIP_CM32183:
				printf("cm32183\n");
				break;
			case MOTIONSENSE_CHIP_BH1730:
				printf("bh1730\n");
				break;
			case MOTIONSENSE_CHIP_GPIO:
				printf("gpio\n");
				break;
			case MOTIONSENSE_CHIP_LIS2DH:
				printf("lis2dh\n");
				break;
			case MOTIONSENSE_CHIP_LSM6DSM:
				printf("lsm6dsm\n");
				break;
			case MOTIONSENSE_CHIP_LIS2DE:
				printf("lis2de\n");
				break;
			case MOTIONSENSE_CHIP_LIS2MDL:
				printf("lis2mdl\n");
				break;
			case MOTIONSENSE_CHIP_LSM6DS3:
				printf("lsm6ds3\n");
				break;
			case MOTIONSENSE_CHIP_LSM6DSO:
				printf("lsm6dso\n");
				break;
			case MOTIONSENSE_CHIP_LNG2DM:
				printf("lng2dm\n");
				break;
			case MOTIONSENSE_CHIP_TCS3400:
				printf("tcs3400\n");
				break;
			case MOTIONSENSE_CHIP_LIS2DW12:
				printf("lis2dw12\n");
				break;
			case MOTIONSENSE_CHIP_LIS2DWL:
				printf("lis2dwl\n");
				break;
			case MOTIONSENSE_CHIP_LIS2DS:
				printf("lis2ds\n");
				break;
			case MOTIONSENSE_CHIP_BMI260:
				printf("bmi260\n");
				break;
			case MOTIONSENSE_CHIP_ICM426XX:
				printf("icm426xx\n");
				break;
			case MOTIONSENSE_CHIP_ICM42607:
				printf("icm42607\n");
				break;
			case MOTIONSENSE_CHIP_BMI323:
				printf("bmi323\n");
				break;
			case MOTIONSENSE_CHIP_BMA422:
				printf("bma422\n");
				break;
			case MOTIONSENSE_CHIP_BMI220:
				printf("bmi220\n");
				break;
			default:
				printf("unknown\n");
			}

			if (version >= 3) {
				printf("Min Frequency:              %d mHz\n",
				       resp->info_3.min_frequency);
				printf("Max Frequency:              %d mHz\n",
				       resp->info_3.max_frequency);
				printf("FIFO Max Event Count:       %d\n",
				       resp->info_3.fifo_max_event_count);
			}
			if (version >= 4) {
				printf("Flags:                      %d\n",
				       resp->info_4.flags);
			}
		}

		return 0;
	}

	if (argc > 2 && !strcasecmp(argv[1], "ec_rate")) {
		param.cmd = MOTIONSENSE_CMD_EC_RATE;
		param.ec_rate.data = EC_MOTION_SENSE_NO_VALUE;
		param.sensor_odr.sensor_num = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad %s arg.\n", argv[2]);
			return -1;
		}
		if (argc == 4) {
			param.ec_rate.data = strtol(argv[3], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad %s arg.\n", argv[3]);
				return -1;
			}
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);

		if (rv < 0)
			return rv;

		printf("%d\n", resp->ec_rate.ret);
		return 0;
	}

	if (argc > 2 && !strcasecmp(argv[1], "odr")) {
		param.cmd = MOTIONSENSE_CMD_SENSOR_ODR;
		param.sensor_odr.data = EC_MOTION_SENSE_NO_VALUE;
		param.sensor_odr.roundup = 1;

		param.sensor_odr.sensor_num = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad %s arg.\n", argv[2]);
			return -1;
		}

		if (argc >= 4) {
			param.sensor_odr.data = strtol(argv[3], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad %s arg.\n", argv[3]);
				return -1;
			}
		}

		if (argc == 5) {
			param.sensor_odr.roundup = strtol(argv[4], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad %s arg.\n", argv[4]);
				return -1;
			}
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);

		if (rv < 0)
			return rv;

		printf("%d\n", resp->sensor_odr.ret);
		return 0;
	}

	if (argc > 2 && !strcasecmp(argv[1], "range")) {
		param.cmd = MOTIONSENSE_CMD_SENSOR_RANGE;
		param.sensor_range.data = EC_MOTION_SENSE_NO_VALUE;
		param.sensor_range.roundup = 1;

		param.sensor_range.sensor_num = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad %s arg.\n", argv[2]);
			return -1;
		}

		if (argc >= 4) {
			param.sensor_range.data = strtol(argv[3], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad %s arg.\n", argv[3]);
				return -1;
			}
		}

		if (argc == 5) {
			param.sensor_odr.roundup = strtol(argv[4], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad %s arg.\n", argv[4]);
				return -1;
			}
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);

		if (rv < 0)
			return rv;

		printf("%d\n", resp->sensor_range.ret);
		return 0;
	}

	if (argc < 4 && !strcasecmp(argv[1], "kb_wake")) {
		param.cmd = MOTIONSENSE_CMD_KB_WAKE_ANGLE;
		param.kb_wake_angle.data = EC_MOTION_SENSE_NO_VALUE;

		if (argc == 3) {
			param.kb_wake_angle.data = strtol(argv[2], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad %s arg.\n", argv[2]);
				return -1;
			}
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);

		if (rv < 0)
			return rv;

		printf("%d\n", resp->kb_wake_angle.ret);
		return 0;
	}

	if (argc < 5 && !strcasecmp(argv[1], "tablet_mode_angle")) {
		param.cmd = MOTIONSENSE_CMD_TABLET_MODE_LID_ANGLE;
		/*
		 * EC_MOTION_SENSE_NO_VALUE indicates to the EC that host is
		 * attempting to only read the current values.
		 */
		param.tablet_mode_threshold.lid_angle =
			EC_MOTION_SENSE_NO_VALUE;
		param.tablet_mode_threshold.hys_degree =
			EC_MOTION_SENSE_NO_VALUE;

		if (argc == 4) {
			param.tablet_mode_threshold.lid_angle =
				strtol(argv[2], &e, 0);

			if (e && *e) {
				fprintf(stderr, "Bad %s arg.\n", argv[2]);
				return -1;
			}

			param.tablet_mode_threshold.hys_degree =
				strtol(argv[3], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad %s arg.\n", argv[3]);
				return -1;
			}
		} else if (argc != 2) {
			return ms_help(argv[0]);
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);

		if (rv < 0)
			return rv;

		printf("tablet_mode_angle=%d hys=%d\n",
		       resp->tablet_mode_threshold.lid_angle,
		       resp->tablet_mode_threshold.hys_degree);

		return 0;
	}

	if (argc == 2 && !strcasecmp(argv[1], "fifo_info")) {
		int sensor_count;

		param.cmd = MOTIONSENSE_CMD_DUMP;
		param.dump.max_sensor_count = 0;
		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);
		if (rv < 0)
			return rv;
		sensor_count = resp->dump.sensor_count;

		param.cmd = MOTIONSENSE_CMD_FIFO_INFO;
		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);
		if (rv < 0)
			return rv;

		printf("Size:     %d\n", resp->fifo_info.size);
		printf("Count:    %d\n", resp->fifo_info.count);
		printf("Timestamp:%" PRIx32 "\n", resp->fifo_info.timestamp);
		printf("Total lost: %d\n", resp->fifo_info.total_lost);
		for (i = 0; i < sensor_count; i++) {
			int lost = resp->fifo_info.lost[i];
			if (lost != 0)
				printf("Lost %d:     %d\n", i, lost);
		}
		return 0;
	}

	if (argc >= 2 && !strcasecmp(argv[1], "fifo_int_enable")) {
		param.cmd = MOTIONSENSE_CMD_FIFO_INT_ENABLE;
		if (argc == 3)
			param.fifo_int_enable.enable = strtol(argv[2], &e, 0);
		else
			param.fifo_int_enable.enable = EC_MOTION_SENSE_NO_VALUE;
		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);
		if (rv < 0)
			return rv;

		printf("%d\n", resp->fifo_int_enable.ret);
		return 0;
	}

	if (argc == 3 && !strcasecmp(argv[1], "fifo_read")) {
		/* large number to test fragmentation */
		struct {
			uint32_t number_data;
			struct ec_response_motion_sensor_data data[512];
		} fifo_read_buffer = {
			.number_data = UINT32_MAX,
		};
		int print_data = 0, max_data = strtol(argv[2], &e, 0);

		if (e && *e) {
			fprintf(stderr, "Bad %s arg.\n", argv[2]);
			return -1;
		}
		while (fifo_read_buffer.number_data != 0 &&
		       print_data < max_data) {
			struct ec_response_motion_sensor_data *vector;
			param.cmd = MOTIONSENSE_CMD_FIFO_READ;
			param.fifo_read.max_data_vector =
				MIN(ARRAY_SIZE(fifo_read_buffer.data),
				    max_data - print_data);

			rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2, &param,
					ms_command_sizes[param.cmd].outsize,
					&fifo_read_buffer, ec_max_insize);
			if (rv < 0)
				return rv;

			print_data += fifo_read_buffer.number_data;
			for (i = 0; i < fifo_read_buffer.number_data; i++) {
				vector = &fifo_read_buffer.data[i];
				if (vector->flags &
				    (MOTIONSENSE_SENSOR_FLAG_TIMESTAMP |
				     MOTIONSENSE_SENSOR_FLAG_FLUSH)) {
					printf("Timestamp:%" PRIx32 "%s\n",
					       vector->timestamp,
					       (vector->flags & MOTIONSENSE_SENSOR_FLAG_FLUSH ?
							" - Flush" :
							""));
				} else {
					printf("Sensor %d: %d\t%d\t%d "
					       "(as uint16: %u\t%u\t%u)\n",
					       vector->sensor_num,
					       vector->data[0], vector->data[1],
					       vector->data[2], vector->data[0],
					       vector->data[1],
					       vector->data[2]);
				}
			}
		}
		return 0;
	}
	if (argc == 3 && !strcasecmp(argv[1], "fifo_flush")) {
		param.cmd = MOTIONSENSE_CMD_FIFO_FLUSH;

		param.sensor_odr.sensor_num = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad %s arg.\n", argv[2]);
			return -1;
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);

		return rv < 0 ? rv : 0;
	}

	if (argc == 3 && !strcasecmp(argv[1], "calibrate")) {
		param.cmd = MOTIONSENSE_CMD_PERFORM_CALIB;
		param.perform_calib.enable = 1;
		param.perform_calib.sensor_num = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad %s arg.\n", argv[2]);
			return -1;
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);

		if (rv < 0)
			return rv;

		printf("--- Calibrated well ---\n");
		printf("New offset vector: X:%d, Y:%d, Z:%d\n",
		       resp->perform_calib.offset[0],
		       resp->perform_calib.offset[1],
		       resp->perform_calib.offset[2]);
		if (resp->perform_calib.temp ==
		    EC_MOTION_SENSE_INVALID_CALIB_TEMP)
			printf("Temperature at calibration unknown\n");
		else
			printf("Temperature at calibration: %d.%02d C\n",
			       resp->perform_calib.temp / 100,
			       resp->perform_calib.temp % 100);
		return 0;
	}

	if (argc >= 3 && !strcasecmp(argv[1], "offset")) {
		param.cmd = MOTIONSENSE_CMD_SENSOR_OFFSET;
		param.sensor_offset.flags = 0;
		param.sensor_offset.temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;

		param.sensor_offset.sensor_num = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad %s arg.\n", argv[2]);
			return -1;
		}

		if (argc >= 4) {
			/* Regarded as a command to set offset */
			if (argc >= 6 && argc < 8) {
				/* Set offset : X, Y, Z */
				param.sensor_offset.flags =
					MOTION_SENSE_SET_OFFSET;
				for (i = 0; i < 3; i++) {
					param.sensor_offset.offset[i] =
						strtol(argv[3 + i], &e, 0);
					if (e && *e) {
						fprintf(stderr, "Bad %s arg.\n",
							argv[3 + i]);
						return -1;
					}
				}
				if (argc == 7) {
					/* Set offset : Temperature */
					param.sensor_offset.temp =
						strtol(argv[6], &e, 0);
					if (e && *e) {
						fprintf(stderr, "Bad %s arg.\n",
							argv[6]);
						return -1;
					}
				}
			} else {
				return ms_help(argv[0]);
			}
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);

		if (rv < 0)
			return rv;

		printf("Offset vector: X:%d, Y:%d, Z:%d\n",
		       resp->sensor_offset.offset[0],
		       resp->sensor_offset.offset[1],
		       resp->sensor_offset.offset[2]);
		if (resp->sensor_offset.temp ==
		    EC_MOTION_SENSE_INVALID_CALIB_TEMP)
			printf("temperature at calibration unknown\n");
		else
			printf("temperature at calibration: %d.%02d C\n",
			       resp->sensor_offset.temp / 100,
			       resp->sensor_offset.temp % 100);
		return 0;
	}

	if (argc == 2 && !strcasecmp(argv[1], "list_activities")) {
		param.cmd = MOTIONSENSE_CMD_LIST_ACTIVITIES;
		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);
		if (rv < 0)
			return rv;

		printf("Enabled:\n");
		motionsense_display_activities(resp->list_activities.enabled);
		printf("Disabled:\n");
		motionsense_display_activities(resp->list_activities.disabled);
		return 0;
	}
	if (argc == 4 && !strcasecmp(argv[1], "set_activity")) {
		param.cmd = MOTIONSENSE_CMD_SET_ACTIVITY;
		param.set_activity.activity = strtol(argv[2], &e, 0);
		param.set_activity.enable = strtol(argv[3], &e, 0);

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);
		if (rv < 0)
			return rv;
		return 0;
	}
	if (argc == 3 && !strcasecmp(argv[1], "get_activity")) {
		param.cmd = MOTIONSENSE_CMD_GET_ACTIVITY;
		param.get_activity.activity = strtol(argv[2], &e, 0);

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);
		if (rv < 0)
			return rv;
		printf("State: %d\n", resp->get_activity.state);
		return 0;
	}
	if (argc == 2 && !strcasecmp(argv[1], "lid_angle")) {
		param.cmd = MOTIONSENSE_CMD_LID_ANGLE;
		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);
		if (rv < 0)
			return rv;

		printf("Lid angle: ");
		if (resp->lid_angle.value == LID_ANGLE_UNRELIABLE)
			printf("unreliable\n");
		else
			printf("%d\n", resp->lid_angle.value);

		return 0;
	}

	if (argc >= 3 && !strcasecmp(argv[1], "spoof")) {
		param.cmd = MOTIONSENSE_CMD_SPOOF;
		/* By default, just query the current spoof status. */
		param.spoof.spoof_enable = MOTIONSENSE_SPOOF_MODE_QUERY;
		param.spoof.sensor_id = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad %s arg.\n", argv[2]);
			return -1;
		}
		/* spoof activity state */
		if (argc >= 5 && !strcasecmp(argv[3], "activity")) {
			int enable = 0;

			param.spoof.activity_num = strtol(argv[4], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Base %s arg.\n", argv[4]);
				return -1;
			}
			if (argc >= 6) {
				enable = strtol(argv[5], &e, 0);
				if ((e && *e) || (enable != 0 && enable != 1)) {
					fprintf(stderr, "Bad %s arg.\n",
						argv[5]);
					return -1;
				}
			}
			if ((enable == 1) && (argc == 6)) {
				/* Enable spoofing, but lock to current state */
				param.spoof.spoof_enable =
					MOTIONSENSE_SPOOF_MODE_LOCK_CURRENT;
			} else if ((enable == 1) && (argc == 7)) {
				/* Enable spoofing, but use provided state */
				int state = strtol(argv[6], &e, 0);

				if ((e && *e) || (state != 0 && state != 1)) {
					fprintf(stderr, "Bad %s arg.\n",
						argv[6]);
					return -1;
				}
				param.spoof.activity_state = state;
				param.spoof.spoof_enable =
					MOTIONSENSE_SPOOF_MODE_CUSTOM;
			} else if ((enable == 0) && (argc == 6)) {
				param.spoof.spoof_enable =
					MOTIONSENSE_SPOOF_MODE_DISABLE;
			} else if (argc != 5) {
				return ms_help(argv[0]);
			}
			/* spoof accel data */
		} else if (argc >= 4) {
			int enable, i;
			int16_t val;

			enable = strtol(argv[3], &e, 0);
			if ((e && *e) || (enable != 0 && enable != 1)) {
				fprintf(stderr, "Bad %s arg.\n", argv[3]);
				return -1;
			}

			if ((enable == 1) && (argc == 4)) {
				/*
				 * Enable spoofing, but lock to current sensor
				 * values.
				 */
				param.spoof.spoof_enable =
					MOTIONSENSE_SPOOF_MODE_LOCK_CURRENT;
			} else if ((enable == 1) && (argc == 7)) {
				/*
				 * Enable spoofing, but use provided component
				 * values.
				 */
				param.spoof.spoof_enable =
					MOTIONSENSE_SPOOF_MODE_CUSTOM;
				for (i = 0; i < 3; i++) {
					val = strtol(argv[4 + i], &e, 0);
					if (e && *e) {
						fprintf(stderr, "Bad %s arg.\n",
							argv[4 + i]);
						return -1;
					}
					param.spoof.components[i] = val;
				}
			} else if (enable == 0) {
				param.spoof.spoof_enable =
					MOTIONSENSE_SPOOF_MODE_DISABLE;
			} else {
				return ms_help(argv[0]);
			}
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2, &param,
				ms_command_sizes[param.cmd].outsize, resp,
				ms_command_sizes[param.cmd].insize);
		if (rv < 0)
			return rv;

		if (param.spoof.spoof_enable == MOTIONSENSE_SPOOF_MODE_QUERY)
			/*
			 * Response is the current spoof status of the
			 * sensor.
			 */
			printf("Sensor %d spoof mode is %s.\n",
			       param.spoof.sensor_id,
			       resp->spoof.ret ? "enabled" : "disabled");

		return 0;
	}

	return ms_help(argv[0]);
}

int cmd_next_event(int argc, char *argv[])
{
	uint8_t *rdata = (uint8_t *)ec_inbuf;
	int rv;
	int i;

	rv = ec_command(EC_CMD_GET_NEXT_EVENT, 0, NULL, 0, rdata,
			ec_max_insize);
	if (rv < 0)
		return rv;

	printf("Next event is 0x%02x\n", rdata[0]);
	if (rv > 1) {
		printf("Event data:\n");
		for (i = 1; i < rv; ++i) {
			printf("%02x ", rdata[i]);
			if (!(i & 0xf))
				printf("\n");
		}
		printf("\n");
	}

	return 0;
}

static int find_led_color_by_name(const char *color)
{
	int i;

	for (i = 0; i < EC_LED_COLOR_COUNT; ++i)
		if (!strcasecmp(color, led_color_names[i]))
			return i;

	return -1;
}

static int find_led_id_by_name(const char *led)
{
	int i;

	for (i = 0; i < EC_LED_ID_COUNT; ++i)
		if (!strcasecmp(led, led_names[i]))
			return i;

	return -1;
}

int cmd_led(int argc, char *argv[])
{
	struct ec_params_led_control p;
	struct ec_response_led_control r;
	char *e, *ptr;
	int rv, i, j;

	memset(p.brightness, 0, sizeof(p.brightness));
	p.flags = 0;

	if (argc < 3) {
		fprintf(stderr,
			"Usage: %s <name> <query | auto | "
			"off | <color> | <color>=<value>...>\n",
			argv[0]);
		return -1;
	}

	p.led_id = find_led_id_by_name(argv[1]);
	if (p.led_id == (uint8_t)-1) {
		fprintf(stderr, "Bad LED name: %s\n", argv[1]);
		fprintf(stderr, "Valid LED names: ");
		for (i = 0; i < EC_LED_ID_COUNT; i++)
			fprintf(stderr, "%s ", led_names[i]);
		fprintf(stderr, "\n");
		return -1;
	}

	if (!strcasecmp(argv[2], "query")) {
		p.flags = EC_LED_FLAGS_QUERY;
		rv = ec_command(EC_CMD_LED_CONTROL, 1, &p, sizeof(p), &r,
				sizeof(r));
		printf("Brightness range for LED %d:\n", p.led_id);
		if (rv < 0) {
			fprintf(stderr, "Error: Unsupported LED.\n");
			return rv;
		}
		for (i = 0; i < EC_LED_COLOR_COUNT; ++i)
			printf("\t%s\t: 0x%x\n", led_color_names[i],
			       r.brightness_range[i]);
		return 0;
	}

	if (!strcasecmp(argv[2], "off")) {
		/* Brightness initialized to 0 for each color. */
	} else if (!strcasecmp(argv[2], "auto")) {
		p.flags = EC_LED_FLAGS_AUTO;
	} else if ((i = find_led_color_by_name(argv[2])) != -1) {
		p.brightness[i] = 0xff;
	} else {
		for (i = 2; i < argc; ++i) {
			ptr = strtok(argv[i], "=");
			j = find_led_color_by_name(ptr);
			if (j == -1) {
				fprintf(stderr, "Bad color name: %s\n", ptr);
				fprintf(stderr, "Valid colors: ");
				for (j = 0; j < EC_LED_COLOR_COUNT; j++)
					fprintf(stderr, "%s ",
						led_color_names[j]);
				fprintf(stderr, "\n");
				return -1;
			}
			ptr = strtok(NULL, "=");
			if (ptr == NULL) {
				fprintf(stderr, "Missing brightness value\n");
				return -1;
			}
			p.brightness[j] = strtol(ptr, &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad brightness: %s\n", ptr);
				return -1;
			}
		}
	}

	rv = ec_command(EC_CMD_LED_CONTROL, 1, &p, sizeof(p), &r, sizeof(r));
	return (rv < 0 ? rv : 0);
}

int cmd_usb_charge_set_mode(int argc, char *argv[])
{
	struct ec_params_usb_charge_set_mode p;
	char *e;
	int rv;

	if (argc != 3 && argc != 4) {
		fprintf(stderr,
			"Usage: %s <port_id> <mode_id> [<inhibit_charge>]\n",
			argv[0]);
		return -1;
	}
	p.usb_port_id = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port ID.\n");
		return -1;
	}
	p.mode = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad mode ID.\n");
		return -1;
	}
	p.inhibit_charge = 0;
	if (argc == 4) {
		p.inhibit_charge = strtol(argv[3], &e, 0);
		if ((e && *e) ||
		    (p.inhibit_charge != 0 && p.inhibit_charge != 1)) {
			fprintf(stderr, "Bad value\n");
			return -1;
		}
	}

	printf("Setting port %d to mode %d inhibit_charge %d...\n",
	       p.usb_port_id, p.mode, p.inhibit_charge);

	rv = ec_command(EC_CMD_USB_CHARGE_SET_MODE, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("USB charging mode set.\n");
	return 0;
}

int cmd_usb_mux(int argc, char *argv[])
{
	struct ec_params_usb_mux p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <mux>\n", argv[0]);
		return -1;
	}

	p.mux = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad mux value.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_USB_MUX, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("Set USB mux to 0x%x.\n", p.mux);

	return 0;
}

int cmd_usb_pd(int argc, char *argv[])
{
	const char *role_str[] = { "",	   "toggle", "toggle-off",
				   "sink", "source", "freeze" };
	const char *mux_str[] = { "", "none", "usb", "dp", "dock", "auto" };
	const char *swap_str[] = { "", "dr_swap", "pr_swap", "vconn_swap" };
	struct ec_params_usb_pd_control p;
	struct ec_response_usb_pd_control_v2 *r_v2 =
		(struct ec_response_usb_pd_control_v2 *)ec_inbuf;
	struct ec_response_usb_pd_control_v1 *r_v1 =
		(struct ec_response_usb_pd_control_v1 *)ec_inbuf;
	struct ec_response_usb_pd_control *r =
		(struct ec_response_usb_pd_control *)ec_inbuf;
	int rv, i, j;
	int option_ok;
	char *e;
	int cmdver;

	BUILD_ASSERT(ARRAY_SIZE(role_str) == USB_PD_CTRL_ROLE_COUNT);
	BUILD_ASSERT(ARRAY_SIZE(mux_str) == USB_PD_CTRL_MUX_COUNT);
	BUILD_ASSERT(ARRAY_SIZE(swap_str) == USB_PD_CTRL_SWAP_COUNT);
	p.role = USB_PD_CTRL_ROLE_NO_CHANGE;
	p.mux = USB_PD_CTRL_MUX_NO_CHANGE;
	p.swap = USB_PD_CTRL_SWAP_NONE;

	if (argc < 2) {
		fprintf(stderr, "No port specified.\n");
		return -1;
	}

	p.port = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Invalid param (port)\n");
		return -1;
	}

	for (i = 2; i < argc; ++i) {
		option_ok = 0;
		if (!strcmp(argv[i], "auto")) {
			if (argc != 3) {
				fprintf(stderr, "\"auto\" may not be used "
						"with other options.\n");
				return -1;
			}
			p.role = USB_PD_CTRL_ROLE_TOGGLE_ON;
			p.mux = USB_PD_CTRL_MUX_AUTO;
			continue;
		}

		for (j = 0; j < ARRAY_SIZE(role_str); ++j) {
			if (!strcmp(argv[i], role_str[j])) {
				if (p.role != USB_PD_CTRL_ROLE_NO_CHANGE) {
					fprintf(stderr,
						"Only one role allowed.\n");
					return -1;
				}
				p.role = j;
				option_ok = 1;
				break;
			}
		}
		if (option_ok)
			continue;

		for (j = 0; j < ARRAY_SIZE(mux_str); ++j) {
			if (!strcmp(argv[i], mux_str[j])) {
				if (p.mux != USB_PD_CTRL_MUX_NO_CHANGE) {
					fprintf(stderr,
						"Only one mux type allowed.\n");
					return -1;
				}
				p.mux = j;
				option_ok = 1;
				break;
			}
		}
		if (option_ok)
			continue;

		for (j = 0; j < ARRAY_SIZE(swap_str); ++j) {
			if (!strcmp(argv[i], swap_str[j])) {
				if (p.swap != USB_PD_CTRL_SWAP_NONE) {
					fprintf(stderr,
						"Only one swap type allowed.\n");
					return -1;
				}
				p.swap = j;
				option_ok = 1;
				break;
			}
		}

		if (!option_ok) {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			return -1;
		}
	}

	if (ec_cmd_version_supported(EC_CMD_USB_PD_CONTROL, 2))
		cmdver = 2;
	else if (ec_cmd_version_supported(EC_CMD_USB_PD_CONTROL, 1))
		cmdver = 1;
	else
		cmdver = 0;

	rv = ec_command(EC_CMD_USB_PD_CONTROL, cmdver, &p, sizeof(p), ec_inbuf,
			ec_max_insize);

	if (rv < 0 || argc != 2)
		return (rv < 0) ? rv : 0;

	if (cmdver == 0) {
		printf("Port C%d is %sabled, Role:%s Polarity:CC%d State:%d\n",
		       p.port, (r->enabled) ? "en" : "dis",
		       r->role == PD_ROLE_SOURCE ? "SRC" : "SNK",
		       r->polarity + 1, r->state);
	} else {
		printf("Port C%d: %s, %s  State:%s\n"
		       "Role:%s %s%s, Polarity:CC%d\n",
		       p.port,
		       (r_v1->enabled & PD_CTRL_RESP_ENABLED_COMMS) ?
			       "enabled" :
			       "disabled",
		       (r_v1->enabled & PD_CTRL_RESP_ENABLED_CONNECTED) ?
			       "connected" :
			       "disconnected",
		       r_v1->state,

		       (r_v1->role & PD_CTRL_RESP_ROLE_POWER) ? "SRC" : "SNK",
		       (r_v1->role & PD_CTRL_RESP_ROLE_DATA) ? "DFP" : "UFP",
		       (r_v1->role & PD_CTRL_RESP_ROLE_VCONN) ? " VCONN" : "",
		       r_v1->polarity + 1);

		if (cmdver == 2) {
			printf("CC State:");
			if (r_v2->cc_state == PD_CC_NONE)
				printf("None");
			else if (r_v2->cc_state == PD_CC_UFP_AUDIO_ACC)
				printf("UFP Audio accessory");
			else if (r_v2->cc_state == PD_CC_UFP_DEBUG_ACC)
				printf("UFP Debug accessory");
			else if (r_v2->cc_state == PD_CC_UFP_ATTACHED)
				printf("UFP attached");
			else if (r_v2->cc_state == PD_CC_DFP_DEBUG_ACC)
				printf("DFP Debug accessory");
			else if (r_v2->cc_state == PD_CC_DFP_ATTACHED)
				printf("DFP attached");
			else
				printf("UNKNOWN");
			printf("\n");

			if (r_v2->dp_mode) {
				printf("DP pin mode:");
				if (r_v2->dp_mode == MODE_DP_PIN_A)
					printf("A");
				else if (r_v2->dp_mode == MODE_DP_PIN_B)
					printf("B");
				else if (r_v2->dp_mode == MODE_DP_PIN_C)
					printf("C");
				else if (r_v2->dp_mode == MODE_DP_PIN_D)
					printf("D");
				else if (r_v2->dp_mode == MODE_DP_PIN_E)
					printf("E");
				else if (r_v2->dp_mode == MODE_DP_PIN_F)
					printf("F");
				else
					printf("UNKNOWN");
				printf("\n");
			}

			printf("Cable type:%s\n",
			       r_v2->control_flags & USB_PD_CTRL_ACTIVE_CABLE ?
				       "Active" :
				       "Passive");

			printf("TBT Adapter type:%s\n",
			       r_v2->control_flags &
					       USB_PD_CTRL_TBT_LEGACY_ADAPTER ?
				       "Legacy" :
				       "Gen3");

			printf("Optical Cable:%s\n",
			       r_v2->control_flags & USB_PD_CTRL_OPTICAL_CABLE ?
				       "True" :
				       "False");

			printf("Link LSRX Communication:%s-directional\n",
			       r_v2->control_flags &
					       USB_PD_CTRL_ACTIVE_LINK_UNIDIR ?
				       "Uni" :
				       "Bi");

			printf("TBT Cable Speed:");
			switch (r_v2->cable_speed) {
			case TBT_SS_U31_GEN1:
				printf("TBT Gen1");
				break;
			case TBT_SS_U32_GEN1_GEN2:
				printf("TBT Gen1 and TBT Gen2");
				break;
			case TBT_SS_TBT_GEN3:
				printf("TBT Gen3");
				break;
			default:
				printf("UNKNOWN");
			}
			printf("\n");

			printf("Rounded support: 3rd Gen %srounded support\n",
			       r_v2->cable_gen ? "and 4th Gen " : "");
		}
		/* If connected to a PD device, then print port partner info */
		if ((r_v1->enabled & PD_CTRL_RESP_ENABLED_CONNECTED) &&
		    (r_v1->enabled & PD_CTRL_RESP_ENABLED_PD_CAPABLE))
			printf("PD Partner Capabilities:\n%s%s%s%s",
			       (r_v1->role & PD_CTRL_RESP_ROLE_DR_POWER) ?
				       " DR power\n" :
				       "",
			       (r_v1->role & PD_CTRL_RESP_ROLE_DR_DATA) ?
				       " DR data\n" :
				       "",
			       (r_v1->role & PD_CTRL_RESP_ROLE_USB_COMM) ?
				       " USB capable\n" :
				       "",
			       (r_v1->role & PD_CTRL_RESP_ROLE_UNCONSTRAINED) ?
				       " Unconstrained power\n" :
				       "");
	}
	return 0;
}

int cmd_usb_pd_dps(int argc, char *argv[])
{
	struct ec_params_usb_pd_dps_control p;
	int rv;

	/*
	 * Set up requested flags.  If no flags were specified, p.mask will
	 * be 0 and nothing will change.
	 */
	if (argc < 1) {
		fprintf(stderr, "Usage: %s [enable|disable]\n", argv[0]);
		return -1;
	}

	if (!strcasecmp(argv[1], "enable")) {
		p.enable = 1;
	} else if (!strcasecmp(argv[1], "disable")) {
		p.enable = 0;
	} else {
		fprintf(stderr, "Usage: %s [enable|disable]\n", argv[0]);
		return -1;
	}

	rv = ec_command(EC_CMD_USB_PD_DPS_CONTROL, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	return 0;
}

static void print_pd_power_info(struct ec_response_usb_pd_power_info *r)
{
	switch (r->role) {
	case USB_PD_PORT_POWER_DISCONNECTED:
		printf("Disconnected");
		break;
	case USB_PD_PORT_POWER_SOURCE:
		printf("SRC");
		break;
	case USB_PD_PORT_POWER_SINK:
		printf("SNK");
		break;
	case USB_PD_PORT_POWER_SINK_NOT_CHARGING:
		printf("SNK (not charging)");
		break;
	default:
		printf("Unknown");
	}

	if ((r->role == USB_PD_PORT_POWER_SOURCE) && (r->meas.current_max))
		printf(" %dmA", r->meas.current_max);

	if ((r->role == USB_PD_PORT_POWER_DISCONNECTED) ||
	    (r->role == USB_PD_PORT_POWER_SOURCE)) {
		printf("\n");
		return;
	}

	printf(r->dualrole ? " DRP" : " Charger");
	switch (r->type) {
	case USB_CHG_TYPE_PD:
		printf(" PD");
		break;
	case USB_CHG_TYPE_C:
		printf(" Type-C");
		break;
	case USB_CHG_TYPE_PROPRIETARY:
		printf(" Proprietary");
		break;
	case USB_CHG_TYPE_BC12_DCP:
		printf(" DCP");
		break;
	case USB_CHG_TYPE_BC12_CDP:
		printf(" CDP");
		break;
	case USB_CHG_TYPE_BC12_SDP:
		printf(" SDP");
		break;
	case USB_CHG_TYPE_OTHER:
		printf(" Other");
		break;
	case USB_CHG_TYPE_VBUS:
		printf(" VBUS");
		break;
	case USB_CHG_TYPE_UNKNOWN:
		printf(" Unknown");
		break;
	}
	printf(" %dmV / %dmA, max %dmV / %dmA", r->meas.voltage_now,
	       r->meas.current_lim, r->meas.voltage_max, r->meas.current_max);
	if (r->max_power)
		printf(" / %dmW", r->max_power / 1000);
	printf("\n");
}

int cmd_usb_pd_mux_info(int argc, char *argv[])
{
	struct ec_params_usb_pd_mux_info p;
	struct ec_response_usb_pd_mux_info r;
	int num_ports, rv, i;
	bool tsv = false;

	if (argc == 2 && (strncmp(argv[1], "tsv", 4) == 0)) {
		tsv = true;
	} else if (argc >= 2) {
		fprintf(stderr, "Usage: %s [tsv]\n", argv[0]);
		return -1;
	}

	rv = ec_command(EC_CMD_USB_PD_PORTS, 0, NULL, 0, ec_inbuf,
			ec_max_insize);
	if (rv < 0)
		return rv;
	num_ports = ((struct ec_response_usb_pd_ports *)ec_inbuf)->num_ports;

	for (i = 0; i < num_ports; i++) {
		p.port = i;
		rv = ec_command(EC_CMD_USB_PD_MUX_INFO, 0, &p, sizeof(p), &r,
				sizeof(r));
		if (rv < 0)
			return rv;

		if (tsv) {
			/*
			 * Machine-readable tab-separated values. This set of
			 * values is append-only. Columns should not be removed
			 * or repurposed. Update the documentation above if new
			 * columns are added.
			 */
			printf("%d\t", i);
			printf("%d\t", !!(r.flags & USB_PD_MUX_USB_ENABLED));
			printf("%d\t", !!(r.flags & USB_PD_MUX_DP_ENABLED));
			printf("%s\t", r.flags & USB_PD_MUX_POLARITY_INVERTED ?
					       "INVERTED" :
					       "NORMAL");
			printf("%d\t", !!(r.flags & USB_PD_MUX_HPD_IRQ));
			printf("%d\n", !!(r.flags & USB_PD_MUX_HPD_LVL));
		} else {
			/* Human-readable mux info. */
			printf("Port %d: ", i);
			printf("USB=%d ", !!(r.flags & USB_PD_MUX_USB_ENABLED));
			printf("DP=%d ", !!(r.flags & USB_PD_MUX_DP_ENABLED));
			printf("POLARITY=%s ",
			       r.flags & USB_PD_MUX_POLARITY_INVERTED ?
				       "INVERTED" :
				       "NORMAL");
			printf("HPD_IRQ=%d ", !!(r.flags & USB_PD_MUX_HPD_IRQ));
			printf("HPD_LVL=%d ", !!(r.flags & USB_PD_MUX_HPD_LVL));
			printf("SAFE=%d ", !!(r.flags & USB_PD_MUX_SAFE_MODE));
			printf("TBT=%d ",
			       !!(r.flags & USB_PD_MUX_TBT_COMPAT_ENABLED));
			printf("USB4=%d ",
			       !!(r.flags & USB_PD_MUX_USB4_ENABLED));
			printf("\n");
		}
	}

	return 0;
}

int cmd_usb_pd_power(int argc, char *argv[])
{
	struct ec_params_usb_pd_power_info p;
	struct ec_response_usb_pd_power_info *r =
		(struct ec_response_usb_pd_power_info *)ec_inbuf;
	int num_ports, i, rv;
	char *e;

	rv = ec_command(EC_CMD_USB_PD_PORTS, 0, NULL, 0, ec_inbuf,
			ec_max_insize);
	if (rv < 0)
		return rv;
	num_ports = ((struct ec_response_usb_pd_ports *)r)->num_ports;

	if (argc < 2) {
		for (i = 0; i < num_ports; i++) {
			p.port = i;
			rv = ec_command(EC_CMD_USB_PD_POWER_INFO, 0, &p,
					sizeof(p), ec_inbuf, ec_max_insize);
			if (rv < 0)
				return rv;

			printf("Port %d: ", i);
			print_pd_power_info(r);
		}
	} else {
		p.port = strtol(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad port.\n");
			return -1;
		}
		rv = ec_command(EC_CMD_USB_PD_POWER_INFO, 0, &p, sizeof(p),
				ec_inbuf, ec_max_insize);
		if (rv < 0)
			return rv;

		printf("Port %d: ", p.port);
		print_pd_power_info(r);
	}

	return 0;
}

int cmd_kbpress(int argc, char *argv[])
{
	struct ec_params_mkbp_simulate_key p;
	char *e;
	int rv;

	if (argc != 4) {
		fprintf(stderr, "Usage: %s <row> <col> <0|1>\n", argv[0]);
		return -1;
	}
	p.row = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad row.\n");
		return -1;
	}
	p.col = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad column.\n");
		return -1;
	}
	p.pressed = strtol(argv[3], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad pressed flag.\n");
		return -1;
	}

	printf("%s row %d col %d.\n", p.pressed ? "Pressing" : "Releasing",
	       p.row, p.col);

	rv = ec_command(EC_CMD_MKBP_SIMULATE_KEY, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;
	printf("Done.\n");
	return 0;
}

int cmd_keyboard_factory_test(int argc, char *argv[])
{
	struct ec_response_keyboard_factory_test r;
	int rv;

	rv = ec_command(EC_CMD_KEYBOARD_FACTORY_TEST, 0, NULL, 0, &r,
			sizeof(r));
	if (rv < 0)
		return rv;

	if (r.shorted != 0)
		printf("Keyboard %d and %d pin are shorted.\n",
		       r.shorted & 0x00ff, r.shorted >> 8);
	else
		printf("Keyboard factory test passed.\n");

	return 0;
}

const char *action_key_names[] = {
	[TK_ABSENT] = "Absent",
	[TK_BACK] = "Back",
	[TK_FORWARD] = "Forward",
	[TK_REFRESH] = "Refresh",
	[TK_FULLSCREEN] = "Fullscreen",
	[TK_OVERVIEW] = "Overview",
	[TK_BRIGHTNESS_DOWN] = "Brightness Down",
	[TK_BRIGHTNESS_UP] = "Brightness Up",
	[TK_VOL_MUTE] = "Volume Mute",
	[TK_VOL_DOWN] = "Volume Down",
	[TK_VOL_UP] = "Volume Up",
	[TK_SNAPSHOT] = "Snapshot",
	[TK_PRIVACY_SCRN_TOGGLE] = "Privacy Screen Toggle",
	[TK_KBD_BKLIGHT_DOWN] = "Keyboard Backlight Down",
	[TK_KBD_BKLIGHT_UP] = "Keyboard Backlight Up",
	[TK_PLAY_PAUSE] = "Play/Pause",
	[TK_NEXT_TRACK] = "Next Track",
	[TK_PREV_TRACK] = "Previous Track",
	[TK_KBD_BKLIGHT_TOGGLE] = "Keyboard Backlight Toggle",
	[TK_MICMUTE] = "Microphone Mute",
	[TK_MENU] = "Menu",
};

BUILD_ASSERT(ARRAY_SIZE(action_key_names) == TK_COUNT);

int cmd_keyboard_get_config(int argc, char *argv[])
{
	struct ec_response_keybd_config r;
	int rv;

	rv = ec_command(EC_CMD_GET_KEYBD_CONFIG, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("Vivaldi key:\n");
	for (int i = 0; i < r.num_top_row_keys; ++i) {
		const char *name = NULL;
		if (r.action_keys[i] < TK_COUNT) {
			name = action_key_names[r.action_keys[i]];
		}
		if (name == NULL) {
			name = "Unknown Key";
		}
		printf("%2i: %s (%d)\n", i, name, r.action_keys[i]);
	}
	printf("Capabilities: %0#x", r.capabilities);
	if (r.capabilities & KEYBD_CAP_FUNCTION_KEYS) {
		printf(" FUNCTION_KEYS");
	}
	if (r.capabilities & KEYBD_CAP_NUMERIC_KEYPAD) {
		printf(" NUMERIC_KEYPAD");
	}
	if (r.capabilities & KEYBD_CAP_SCRNLOCK_KEY) {
		printf(" SCRNLOCK_KEY");
	}
	printf("\n");

	return 0;
}

int cmd_panic_info(int argc, char *argv[])
{
	int rv;
	struct ec_params_get_panic_info_v1 params = {
		.preserve_old_hostcmd_flag = 1,
	};

	/* By default, reading the panic info will set
	 * PANIC_DATA_FLAG_OLD_HOSTCMD. Prefer to leave this
	 * flag untouched when supported.
	 */
	if (ec_cmd_version_supported(EC_CMD_GET_PANIC_INFO, 1))
		rv = ec_command(EC_CMD_GET_PANIC_INFO, 1, &params,
				sizeof(params), ec_inbuf, ec_max_insize);
	else
		rv = ec_command(EC_CMD_GET_PANIC_INFO, 0, NULL, 0, ec_inbuf,
				ec_max_insize);

	if (rv < 0)
		return rv;

	if (rv == 0) {
		printf("No panic data.\n");
		return 0;
	}

	std::vector<uint8_t> data(static_cast<uint8_t *>(ec_inbuf),
				  static_cast<uint8_t *>(ec_inbuf) + rv);
	auto result = ec::ParsePanicInfo(data);

	if (!result.has_value()) {
		fprintf(stderr, "%s", result.error().c_str());
		return 1;
	}
	printf("%s", result.value().c_str());

	return 0;
}

int cmd_power_info(int argc, char *argv[])
{
	struct ec_response_power_info_v1 r;
	int rv;

	rv = ec_command(EC_CMD_POWER_INFO, 1, NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("Power source:\t");
	switch (r.system_power_source) {
	case POWER_SOURCE_UNKNOWN:
		printf("Unknown\n");
		break;
	case POWER_SOURCE_BATTERY:
		printf("Battery\n");
		break;
	case POWER_SOURCE_AC:
		printf("AC\n");
		break;
	case POWER_SOURCE_AC_BATTERY:
		printf("AC + battery\n");
		break;
	}

	printf("Battery state-of-charge: %d%%\n", r.battery_soc);
	printf("Max AC power: %d Watts\n", r.ac_adapter_100pct);
	printf("Battery 1Cd rate: %d\n", r.battery_1cd);
	printf("RoP Avg: %d Watts\n", r.rop_avg);
	printf("RoP Peak: %d Watts\n", r.rop_peak);
	printf("Battery DBPT support level: %d\n",
	       r.intel.batt_dbpt_support_level);
	printf("Battery DBPT Max Peak Power: %d Watts\n",
	       r.intel.batt_dbpt_max_peak_power);
	printf("Battery DBPT Sus Peak Power: %d Watts\n",
	       r.intel.batt_dbpt_sus_peak_power);
	return 0;
}

int cmd_pse(int argc, char *argv[])
{
	struct ec_params_pse p;
	struct ec_response_pse_status r;
	int rsize = 0;
	char *e;
	int rv;

	if (argc < 2 || argc > 3 || !strcmp(argv[1], "help")) {
		printf("Usage: %s <port> [<subcmd>]\n", argv[0]);
		printf("'pse <port> [status]' - Get port status\n");
		printf("'pse <port> disable' - Disable port\n");
		printf("'pse <port> enable' - Enable port\n");
		return -1;
	}

	p.port = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port.\n");
		return -1;
	}

	if (argc == 2 || !strcmp(argv[2], "status")) {
		p.cmd = EC_PSE_STATUS;
		rsize = sizeof(r);
	} else if (!strcmp(argv[2], "disable")) {
		p.cmd = EC_PSE_DISABLE;
	} else if (!strcmp(argv[2], "enable")) {
		p.cmd = EC_PSE_ENABLE;
	} else {
		fprintf(stderr, "Unknown command: %s\n", argv[2]);
		return -1;
	}

	rv = ec_command(EC_CMD_PSE, 0, &p, sizeof(p), &r, rsize);
	if (rv < 0)
		return rv;

	if (p.cmd == EC_PSE_STATUS) {
		const char *status;

		switch (r.status) {
		case EC_PSE_STATUS_DISABLED:
			status = "disabled";
			break;
		case EC_PSE_STATUS_ENABLED:
			status = "enabled";
			break;
		case EC_PSE_STATUS_POWERED:
			status = "powered";
			break;
		default:
			status = "unknown";
			break;
		}

		printf("Port %d: %s\n", p.port, status);
	}

	return 0;
}

int cmd_pstore_info(int argc, char *argv[])
{
	struct ec_response_pstore_info r;
	int rv;

	rv = ec_command(EC_CMD_PSTORE_INFO, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("PstoreSize %d\nAccessSize %d\n", r.pstore_size, r.access_size);
	return 0;
}

int cmd_pstore_read(int argc, char *argv[])
{
	struct ec_params_pstore_read p;
	uint8_t rdata[EC_PSTORE_SIZE_MAX];
	int offset, size;
	int rv;
	int i;
	char *e;
	char *buf;

	if (argc < 4) {
		fprintf(stderr, "Usage: %s <offset> <size> <filename>\n",
			argv[0]);
		return -1;
	}
	offset = strtol(argv[1], &e, 0);
	if ((e && *e) || offset < 0 || offset > 0x10000) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}
	size = strtol(argv[2], &e, 0);
	if ((e && *e) || size <= 0 || size > 0x10000) {
		fprintf(stderr, "Bad size.\n");
		return -1;
	}
	printf("Reading %d bytes at offset %d...\n", size, offset);

	buf = (char *)malloc(size);
	if (!buf) {
		fprintf(stderr, "Unable to allocate buffer.\n");
		return -1;
	}

	/* Read data in chunks */
	for (i = 0; i < size; i += EC_PSTORE_SIZE_MAX) {
		p.offset = offset + i;
		p.size = MIN(size - i, EC_PSTORE_SIZE_MAX);
		rv = ec_command(EC_CMD_PSTORE_READ, 0, &p, sizeof(p), rdata,
				sizeof(rdata));
		if (rv < 0) {
			fprintf(stderr, "Read error at offset %d\n", i);
			free(buf);
			return rv;
		}
		memcpy(buf + i, rdata, p.size);
	}

	rv = write_file(argv[3], buf, size);
	free(buf);
	if (rv)
		return rv;

	printf("done.\n");
	return 0;
}

int cmd_pstore_write(int argc, char *argv[])
{
	struct ec_params_pstore_write p;
	int offset, size;
	int rv;
	int i;
	char *e;
	char *buf;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <offset> <filename>\n", argv[0]);
		return -1;
	}
	offset = strtol(argv[1], &e, 0);
	if ((e && *e) || offset < 0 || offset > 0x10000) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}

	/* Read the input file */
	buf = read_file(argv[2], &size);
	if (!buf)
		return -1;

	printf("Writing to offset %d...\n", offset);

	/* Write data in chunks */
	for (i = 0; i < size; i += EC_PSTORE_SIZE_MAX) {
		p.offset = offset + i;
		p.size = MIN(size - i, EC_PSTORE_SIZE_MAX);
		memcpy(p.data, buf + i, p.size);
		rv = ec_command(EC_CMD_PSTORE_WRITE, 0, &p, sizeof(p), NULL, 0);
		if (rv < 0) {
			fprintf(stderr, "Write error at offset %d\n", i);
			free(buf);
			return rv;
		}
	}

	free(buf);
	printf("done.\n");
	return 0;
}

int cmd_host_event_get_raw(int argc, char *argv[])
{
	uint32_t events = read_mapped_mem32(EC_MEMMAP_HOST_EVENTS);

	if (events & EC_HOST_EVENT_MASK(EC_HOST_EVENT_INVALID)) {
		printf("Current host events: invalid\n");
		return -1;
	}

	printf("Current host events: 0x%08x\n", events);
	return 0;
}

int cmd_host_event_get_b(int argc, char *argv[])
{
	struct ec_response_host_event_mask r;
	int rv;

	rv = ec_command(EC_CMD_HOST_EVENT_GET_B, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;
	if (rv < sizeof(r)) {
		fprintf(stderr, "Insufficient data received.\n");
		return -1;
	}

	if (r.mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_INVALID)) {
		printf("Current host events-B: invalid\n");
		return -1;
	}

	printf("Current host events-B: 0x%08x\n", r.mask);
	return 0;
}

int cmd_host_event_get_smi_mask(int argc, char *argv[])
{
	struct ec_response_host_event_mask r;
	int rv;

	rv = ec_command(EC_CMD_HOST_EVENT_GET_SMI_MASK, 0, NULL, 0, &r,
			sizeof(r));
	if (rv < 0)
		return rv;

	printf("Current host event SMI mask: 0x%08x\n", r.mask);
	return 0;
}

int cmd_host_event_get_sci_mask(int argc, char *argv[])
{
	struct ec_response_host_event_mask r;
	int rv;

	rv = ec_command(EC_CMD_HOST_EVENT_GET_SCI_MASK, 0, NULL, 0, &r,
			sizeof(r));
	if (rv < 0)
		return rv;

	printf("Current host event SCI mask: 0x%08x\n", r.mask);
	return 0;
}

int cmd_host_event_get_wake_mask(int argc, char *argv[])
{
	struct ec_response_host_event_mask r;
	int rv;

	rv = ec_command(EC_CMD_HOST_EVENT_GET_WAKE_MASK, 0, NULL, 0, &r,
			sizeof(r));
	if (rv < 0)
		return rv;

	printf("Current host event wake mask: 0x%08x\n", r.mask);
	return 0;
}

int cmd_host_event_set_smi_mask(int argc, char *argv[])
{
	struct ec_params_host_event_mask p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <mask>\n", argv[0]);
		return -1;
	}
	p.mask = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad mask.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_HOST_EVENT_SET_SMI_MASK, 0, &p, sizeof(p), NULL,
			0);
	if (rv < 0)
		return rv;

	printf("Mask set.\n");
	return 0;
}

int cmd_host_event_set_sci_mask(int argc, char *argv[])
{
	struct ec_params_host_event_mask p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <mask>\n", argv[0]);
		return -1;
	}
	p.mask = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad mask.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_HOST_EVENT_SET_SCI_MASK, 0, &p, sizeof(p), NULL,
			0);
	if (rv < 0)
		return rv;

	printf("Mask set.\n");
	return 0;
}

int cmd_host_event_set_wake_mask(int argc, char *argv[])
{
	struct ec_params_host_event_mask p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <mask>\n", argv[0]);
		return -1;
	}
	p.mask = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad mask.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_HOST_EVENT_SET_WAKE_MASK, 0, &p, sizeof(p), NULL,
			0);
	if (rv < 0)
		return rv;

	printf("Mask set.\n");
	return 0;
}

int cmd_host_event_clear(int argc, char *argv[])
{
	struct ec_params_host_event_mask p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <mask>\n", argv[0]);
		return -1;
	}
	p.mask = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad mask.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_HOST_EVENT_CLEAR, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("Host events cleared.\n");
	return 0;
}

int cmd_host_event_clear_b(int argc, char *argv[])
{
	struct ec_params_host_event_mask p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <mask>\n", argv[0]);
		return -1;
	}
	p.mask = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad mask.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_HOST_EVENT_CLEAR_B, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("Host events-B cleared.\n");
	return 0;
}

int cmd_switches(int argc, char *argv[])
{
	uint8_t s = read_mapped_mem8(EC_MEMMAP_SWITCHES);
	printf("Current switches:   0x%02x\n", s);
	printf("Lid switch:         %s\n",
	       (s & EC_SWITCH_LID_OPEN ? "OPEN" : "CLOSED"));
	printf("Power button:       %s\n",
	       (s & EC_SWITCH_POWER_BUTTON_PRESSED ? "DOWN" : "UP"));
	printf("Write protect:      %sABLED\n",
	       (s & EC_SWITCH_WRITE_PROTECT_DISABLED ? "DIS" : "EN"));
	printf("Dedicated recovery: %sABLED\n",
	       (s & EC_SWITCH_DEDICATED_RECOVERY ? "EN" : "DIS"));

	return 0;
}

int cmd_tabletmode(int argc, char *argv[])
{
	struct ec_params_set_tablet_mode p;

	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;

	memset(&p, 0, sizeof(p));
	/* |+1| to also make sure the strings the same length. */
	if (strncmp(argv[1], "on", strlen("on") + 1) == 0) {
		p.tablet_mode = TABLET_MODE_FORCE_TABLET;
	} else if (strncmp(argv[1], "off", strlen("off") + 1) == 0) {
		p.tablet_mode = TABLET_MODE_FORCE_CLAMSHELL;
	} else if (strncmp(argv[1], "reset", strlen("reset") + 1) == 0) {
		// Match tablet mode to the current HW orientation.
		p.tablet_mode = TABLET_MODE_DEFAULT;
	} else {
		return EC_ERROR_PARAM1;
	}

	int rv = ec_command(EC_CMD_SET_TABLET_MODE, 0, &p, sizeof(p), NULL, 0);
	rv = (rv < 0 ? rv : 0);

	if (rv < 0) {
		fprintf(stderr, "Failed to set tablet mode, rv=%d\n", rv);
	} else {
		printf("\n");
		printf("SUCCESS. The tablet mode has been set.\n");
	}
	return rv;
}

int cmd_wireless(int argc, char *argv[])
{
	char *e;
	int rv;
	int now_flags;

	if (argc < 2) {
		fprintf(stderr,
			"Usage: %s <flags> [<mask> [<susflags> <susmask>]]\n",
			argv[0]);
		fprintf(stderr, "  0x1 = WLAN radio\n"
				"  0x2 = Bluetooth radio\n"
				"  0x4 = WWAN power\n"
				"  0x8 = WLAN power\n");
		return -1;
	}

	now_flags = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad flags.\n");
		return -1;
	}

	if (argc < 3) {
		/* Old-style - current flags only */
		struct ec_params_switch_enable_wireless_v0 p;

		p.enabled = now_flags;
		rv = ec_command(EC_CMD_SWITCH_ENABLE_WIRELESS, 0, &p, sizeof(p),
				NULL, 0);
		if (rv < 0)
			return rv;

		printf("Success.\n");
	} else {
		/* New-style - masks and suspend flags */
		struct ec_params_switch_enable_wireless_v1 p;
		struct ec_response_switch_enable_wireless_v1 r;

		memset(&p, 0, sizeof(p));

		p.now_flags = now_flags;

		p.now_mask = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad mask.\n");
			return -1;
		}

		if (argc > 4) {
			p.suspend_flags = strtol(argv[3], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad suspend flags.\n");
				return -1;
			}

			p.suspend_mask = strtol(argv[4], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad suspend mask.\n");
				return -1;
			}
		}

		rv = ec_command(EC_CMD_SWITCH_ENABLE_WIRELESS,
				EC_VER_SWITCH_ENABLE_WIRELESS, &p, sizeof(p),
				&r, sizeof(r));
		if (rv < 0)
			return rv;

		printf("Now=0x%x, suspend=0x%x\n", r.now_flags,
		       r.suspend_flags);
	}

	return 0;
}

static void cmd_locate_chip_help(const char *const cmd)
{
	fprintf(stderr,
		"Usage: %s <type> <index>\n"
		"  <type> is one of:\n"
		"    0: CBI_EEPROM\n"
		"    1: TCPCs\n"
		"  <index> instance # of <type>\n",
		cmd);
}

static const char *bus_type[] = { "I2C", "EMBEDDED" };

int cmd_locate_chip(int argc, char *argv[])
{
	struct ec_params_locate_chip p;
	struct ec_response_locate_chip r = { 0 };
	char *e;
	int rv;

	if (argc != 3) {
		cmd_locate_chip_help(argv[0]);
		return -1;
	}

	p.type = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad type.\n");
		cmd_locate_chip_help(argv[0]);
		return -1;
	}

	p.index = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad index.\n");
		cmd_locate_chip_help(argv[0]);
		return -1;
	}

	rv = ec_command(EC_CMD_LOCATE_CHIP, 0, &p, sizeof(p), &r, sizeof(r));

	if (rv == -EC_RES_INVALID_PARAM - EECRESULT) {
		fprintf(stderr, "Bus type %d not supported.\n", p.type);
		return rv;
	}

	if (rv == -EC_RES_UNAVAILABLE - EECRESULT) {
		fprintf(stderr, "Chip not found\n");
		return rv;
	}

	if (rv == -EC_RES_OVERFLOW - EECRESULT) {
		fprintf(stderr, "Index too large\n");
		return rv;
	}

	if (rv < 0)
		return rv;

	if (r.bus_type >= EC_BUS_TYPE_COUNT ||
	    r.bus_type >= ARRAY_SIZE(bus_type)) {
		fprintf(stderr, "Unknown bus type (%d)\n", r.bus_type);
		return -1;
	}

	/*
	 * When changing the format of this print, make sure FAFT
	 * (firmware_ECCbiEeprom) still passes. It may silently skip the test.
	 */
	printf("Bus: %s; Port: %d; Address: 0x%02x (7-bit format)\n",
	       bus_type[r.bus_type], r.i2c_info.port,
	       I2C_STRIP_FLAGS(r.i2c_info.addr_flags));

	return 0;
}

int cmd_lcd_backlight(int argc, char *argv[])
{
	struct ec_params_switch_enable_backlight p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <0|1>\n", argv[0]);
		return -1;
	}
	p.enabled = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad value.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_SWITCH_ENABLE_BKLIGHT, 0, &p, sizeof(p), NULL,
			0);
	if (rv < 0)
		return rv;

	printf("Success.\n");
	return 0;
}

static void cmd_basestate_help(void)
{
	fprintf(stderr, "Usage: ectool basestate [attach | detach | reset]\n");
}

int cmd_basestate(int argc, char *argv[])
{
	struct ec_params_set_base_state p;

	if (argc != 2) {
		cmd_basestate_help();
		return -1;
	}

	if (!strncmp(argv[1], "attach", 6)) {
		p.cmd = EC_SET_BASE_STATE_ATTACH;
	} else if (!strncmp(argv[1], "detach", 6)) {
		p.cmd = EC_SET_BASE_STATE_DETACH;
	} else if (!strncmp(argv[1], "reset", 5)) {
		p.cmd = EC_SET_BASE_STATE_RESET;
	} else {
		cmd_basestate_help();
		return -1;
	}

	return ec_command(EC_CMD_SET_BASE_STATE, 0, &p, sizeof(p), NULL, 0);
}

int cmd_ext_power_limit(int argc, char *argv[])
{
	/* Version 1 is used, no support for obsolete version 0 */
	struct ec_params_external_power_limit_v1 p;
	char *e;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <max_current_mA> <max_voltage_mV>\n",
			argv[0]);
		return -1;
	}

	p.current_lim = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad param1.\n");
		return -1;
	}

	p.voltage_lim = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad param2.\n");
		return -1;
	}

	/* Send version 1 of command */
	return ec_command(EC_CMD_EXTERNAL_POWER_LIMIT, 1, &p, sizeof(p), NULL,
			  0);
}

static void cmd_charge_current_limit_help(const char *cmd)
{
	fprintf(stderr,
		"\n"
		"  Usage: %s <max_current_mA>\n"
		"    Set the maximum battery charging current.\n"
		"  Usage: %s <max_current_mA> [battery_SoC]\n"
		"    Set the maximum battery charging current and the minimum battery\n"
		"    SoC at which it will apply. Setting [battery_SoC] is only \n"
		"    supported in v1.\n"
		"\n",
		cmd, cmd);
}

int cmd_charge_current_limit(int argc, char *argv[])
{
	struct ec_params_current_limit_v1 p1;
	uint32_t limit;
	uint8_t battery_soc;
	char *e;

	/*
	 * v0: max_current_ma (argc == 2)
	 * v1: max_current_ma [battery_soc] (argc == 2 or 3)
	 */
	if (!ec_cmd_version_supported(EC_CMD_CHARGE_CURRENT_LIMIT, 1)) {
		if (argc != 2) {
			cmd_charge_current_limit_help(argv[0]);
			return -1;
		}
	} else {
		if (argc < 2 || argc > 3) {
			cmd_charge_current_limit_help(argv[0]);
			return -1;
		}
	}

	/* max_current_ma */
	limit = strtoull(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "ERROR: Bad limit value: %s\n", argv[1]);
		return -1;
	}

	if (argc == 2) {
		struct ec_params_current_limit p0;

		p0.limit = limit;
		return ec_command(EC_CMD_CHARGE_CURRENT_LIMIT, 0, &p0,
				  sizeof(p0), NULL, 0);
	}

	/* argc==3 for battery_soc */
	battery_soc = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "ERROR: Bad battery SoC value: %s\n", argv[2]);
		return -1;
	}

	p1.limit = limit;
	p1.battery_soc = battery_soc;
	return ec_command(EC_CMD_CHARGE_CURRENT_LIMIT, 1, &p1, sizeof(p1), NULL,
			  0);
}

static void cmd_charge_control_help(const char *cmd, const char *msg)
{
	if (msg)
		fprintf(stderr, "ERROR: %s\n", msg);

	fprintf(stderr,
		"\n"
		"  Usage: %s\n"
		"    Get current settings.\n"
		"  Usage: %s normal|idle|discharge\n"
		"    Set charge mode (and disable battery sustainer).\n"
		"  Usage: %s normal <lower> <upper> [<flags>]\n"
		"    Enable battery sustainer. <lower> and <upper> are battery SoC\n"
		"    between which EC tries to keep the battery level.\n"
		"    <flags> are supported in v3+\n."
		"\n",
		cmd, cmd, cmd);
}

int cmd_charge_control(int argc, char *argv[])
{
	struct ec_params_charge_control p = {};
	struct ec_response_charge_control r;
	int version;
	const char *const charge_mode_text[] = EC_CHARGE_MODE_TEXT;
	char *e;
	int rv;

	if (ec_cmd_version_supported(EC_CMD_CHARGE_CONTROL, 3)) {
		version = 3;
	} else if (ec_cmd_version_supported(EC_CMD_CHARGE_CONTROL, 2)) {
		if (argc > 4) {
			cmd_charge_control_help(argv[0],
						"<flags> not supported by EC");
			return -1;
		}
		version = 2;
	} else {
		if (argc != 2) {
			cmd_charge_control_help(
				argv[0], "Bad arguments or EC is too old");
			return -1;
		}
		version = 1;
	}

	if (argc == 1) {
		p.cmd = EC_CHARGE_CONTROL_CMD_GET;
		rv = ec_command(EC_CMD_CHARGE_CONTROL, version, &p, sizeof(p),
				&r, sizeof(r));
		if (rv < 0) {
			fprintf(stderr, "Command failed.\n");
			return rv;
		}
		printf("Charge mode = %s (%d)\n",
		       r.mode < ARRAY_SIZE(charge_mode_text) ?
			       charge_mode_text[r.mode] :
			       "UNDEFINED",
		       r.mode);
		printf("Battery sustainer = %s (%d%% ~ %d%%)\n",
		       (r.sustain_soc.lower != -1 &&
			r.sustain_soc.upper != -1) ?
			       "on" :
			       "off",
		       r.sustain_soc.lower, r.sustain_soc.upper);
		return 0;
	}

	p.cmd = EC_CHARGE_CONTROL_CMD_SET;
	if (!strcasecmp(argv[1], "normal")) {
		p.mode = CHARGE_CONTROL_NORMAL;
		if (argc == 2) {
			p.sustain_soc.lower = -1;
			p.sustain_soc.upper = -1;
		} else if (argc > 3) {
			p.sustain_soc.lower = strtol(argv[2], &e, 0);
			if (e && *e) {
				cmd_charge_control_help(
					argv[0], "Bad character in <lower>");
				return -1;
			}
			p.sustain_soc.upper = strtol(argv[3], &e, 0);
			if (e && *e) {
				cmd_charge_control_help(
					argv[0], "Bad character in <upper>");
				return -1;
			}
			if (argc == 5) {
				p.flags = strtoul(argv[4], &e, 0);
				if (e && *e) {
					cmd_charge_control_help(
						argv[0],
						"Bad character in <flags>");
					return -1;
				}
			}
		} else {
			cmd_charge_control_help(argv[0], "Bad arguments");
			return -1;
		}
	} else if (!strcasecmp(argv[1], "idle")) {
		if (argc != 2) {
			cmd_charge_control_help(argv[0], "Bad arguments");
			return -1;
		}
		p.mode = CHARGE_CONTROL_IDLE;
	} else if (!strcasecmp(argv[1], "discharge")) {
		if (argc != 2) {
			cmd_charge_control_help(argv[0], "Bad arguments");
			return -1;
		}
		p.mode = CHARGE_CONTROL_DISCHARGE;
	} else {
		cmd_charge_control_help(argv[0], "Bad sub-command");
		return -1;
	}

	rv = ec_command(EC_CMD_CHARGE_CONTROL, version, &p, sizeof(p), NULL, 0);
	if (rv < 0) {
		fprintf(stderr, "Is AC connected?\n");
		return rv;
	}

	switch (p.mode) {
	case CHARGE_CONTROL_NORMAL:
		printf("Charge state machine is in normal mode%s.\n",
		       (p.sustain_soc.lower == -1 ||
			p.sustain_soc.upper == -1) ?
			       "" :
			       " with sustainer enabled");
		break;
	case CHARGE_CONTROL_IDLE:
		printf("Charge state machine force idle.\n");
		break;
	case CHARGE_CONTROL_DISCHARGE:
		printf("Charge state machine force discharge.\n");
		break;
	default:
		break;
	}
	return 0;
}

static void print_bool(const char *name, bool value)
{
	printf("%s = %s\n", name, value ? "true" : "false");
}

static int cmd_chargesplash(int argc, char **argv)
{
	static struct {
		const char *name;
		enum ec_chargesplash_cmd cmd;
	} actions[] = {
		{ "state", EC_CHARGESPLASH_GET_STATE },
		{ "request", EC_CHARGESPLASH_REQUEST },
		{ "lockout", EC_CHARGESPLASH_LOCKOUT },
		{ "reset", EC_CHARGESPLASH_RESET },
	};
	struct ec_params_chargesplash params;
	struct ec_response_chargesplash resp;

	if (argc != 2) {
		goto usage;
	}

	for (int i = 0; i < ARRAY_SIZE(actions); i++) {
		if (!strcasecmp(actions[i].name, argv[1])) {
			params.cmd = actions[i].cmd;
			if (ec_command(EC_CMD_CHARGESPLASH, 0, &params,
				       sizeof(params), &resp,
				       sizeof(resp)) < 0) {
				fprintf(stderr, "Host command failed\n");
				return -1;
			}

			print_bool("requested", resp.requested);
			print_bool("display_initialized",
				   resp.display_initialized);
			print_bool("locked_out", resp.locked_out);
			return 0;
		}
	}

usage:
	fprintf(stderr, "Usage: %s <state|request|lockout|reset>", argv[0]);
	return -1;
}

#define ST_CMD_SIZE ST_FLD_SIZE(ec_params_charge_state, cmd)
#define ST_PRM_SIZE(SUBCMD) \
	(ST_CMD_SIZE + ST_FLD_SIZE(ec_params_charge_state, SUBCMD))
#define ST_RSP_SIZE(SUBCMD) ST_FLD_SIZE(ec_response_charge_state, SUBCMD)

/* Table of subcommand sizes for EC_CMD_CHARGE_STATE */
static const struct {
	uint8_t to_ec_size;
	uint8_t from_ec_size;
} cs_paramcount[] = {
	/* Order must match enum charge_state_command */
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_state) },
	{ ST_PRM_SIZE(get_param), ST_RSP_SIZE(get_param) },
	{ ST_PRM_SIZE(set_param), 0 },
};
BUILD_ASSERT(ARRAY_SIZE(cs_paramcount) == CHARGE_STATE_NUM_CMDS);

#undef ST_CMD_SIZE
#undef ST_PRM_SIZE
#undef ST_RSP_SIZE

static int cs_do_cmd(struct ec_params_charge_state *to_ec,
		     struct ec_response_charge_state *from_ec)
{
	int rv;
	int cmd = to_ec->cmd;

	rv = ec_command(EC_CMD_CHARGE_STATE, 0, to_ec,
			cs_paramcount[cmd].to_ec_size, from_ec,
			cs_paramcount[cmd].from_ec_size);

	return (rv < 0 ? 1 : 0);
}

static const char *const base_params[] = {
	"chg_voltage", "chg_current", "chg_input_current",
	"chg_status",  "chg_option",  "limit_power",
};
BUILD_ASSERT(ARRAY_SIZE(base_params) == CS_NUM_BASE_PARAMS);

static int cmd_charge_state(int argc, char **argv)
{
	struct ec_params_charge_state param;
	struct ec_response_charge_state resp;
	uint32_t p, v;
	int i, r;
	char *e;

	if (argc > 1 && !strcasecmp(argv[1], "show")) {
		param.cmd = CHARGE_STATE_CMD_GET_STATE;
		r = cs_do_cmd(&param, &resp);
		if (r)
			return r;
		printf("ac = %d\n", resp.get_state.ac);
		printf("chg_voltage = %dmV\n", resp.get_state.chg_voltage);
		printf("chg_current = %dmA\n", resp.get_state.chg_current);
		printf("chg_input_current = %dmA\n",
		       resp.get_state.chg_input_current);
		printf("batt_state_of_charge = %d%%\n",
		       resp.get_state.batt_state_of_charge);
		return 0;
	}

	if (argc > 1 && !strcasecmp(argv[1], "param")) {
		switch (argc) {
		case 3:
			if (!strcasecmp(argv[2], "help"))
				break;
			param.cmd = CHARGE_STATE_CMD_GET_PARAM;
			p = strtoull(argv[2], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad param: %s\n", argv[2]);
				return -1;
			}
			param.get_param.param = p;
			r = cs_do_cmd(&param, &resp);
			if (r)
				return r;
			v = resp.get_param.value;
			if (p < CS_NUM_BASE_PARAMS)
				printf("%d (0x%x)       # %s\n", v, v,
				       base_params[p]);
			else
				printf("%d (0x%x)\n", v, v);
			return 0;
		case 4:
			param.cmd = CHARGE_STATE_CMD_SET_PARAM;
			p = strtoull(argv[2], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad param: %s\n", argv[2]);
				return -1;
			}
			v = strtoull(argv[3], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad value: %s\n", argv[3]);
				return -1;
			}
			param.set_param.param = p;
			param.set_param.value = v;
			return cs_do_cmd(&param, &resp);
		}

		printf("base params:\n");
		for (i = 0; i < CS_NUM_BASE_PARAMS; i++)
			printf("  %d   %s\n", i, base_params[i]);
		printf("custom profile params:\n");
		printf("  0x%x - 0x%x\n", CS_PARAM_CUSTOM_PROFILE_MIN,
		       CS_PARAM_CUSTOM_PROFILE_MAX);

		return 0;
	}

	printf("Usage:\n");
	printf("  %s show                  - show current state\n", argv[0]);
	printf("  %s param NUM [VALUE]     - get/set param NUM\n", argv[0]);
	printf("  %s param help            - show known param NUMs\n", argv[0]);
	return 0;
}

int cmd_gpio_get(int argc, char *argv[])
{
	struct ec_params_gpio_get_v1 p_v1;
	struct ec_response_gpio_get_v1 r_v1;
	int i, rv, subcmd, num_gpios;
	int cmdver = 1;

	if (!ec_cmd_version_supported(EC_CMD_GPIO_GET, cmdver)) {
		struct ec_params_gpio_get p;
		struct ec_response_gpio_get r;

		/* Fall back to version 0 command */
		cmdver = 0;
		if (argc != 2) {
			fprintf(stderr, "Usage: %s <GPIO name>\n", argv[0]);
			return -1;
		}

		if (strlen(argv[1]) + 1 > sizeof(p.name)) {
			fprintf(stderr, "GPIO name too long.\n");
			return -1;
		}
		strcpy(p.name, argv[1]);

		rv = ec_command(EC_CMD_GPIO_GET, cmdver, &p, sizeof(p), &r,
				sizeof(r));
		if (rv < 0)
			return rv;

		printf("GPIO %s = %d\n", p.name, r.val);
		return 0;
	}

	if (argc > 2 || (argc == 2 && !strcmp(argv[1], "help"))) {
		printf("Usage: %s [<subcmd> <GPIO name>]\n", argv[0]);
		printf("'gpioget <GPIO_NAME>' - Get value by name\n");
		printf("'gpioget count' - Get count of GPIOS\n");
		printf("'gpioget all' - Get info for all GPIOs\n");
		return -1;
	}

	/* Keeping it consistent with console command behavior */
	if (argc == 1)
		subcmd = EC_GPIO_GET_INFO;
	else if (!strcmp(argv[1], "count"))
		subcmd = EC_GPIO_GET_COUNT;
	else if (!strcmp(argv[1], "all"))
		subcmd = EC_GPIO_GET_INFO;
	else
		subcmd = EC_GPIO_GET_BY_NAME;

	if (subcmd == EC_GPIO_GET_BY_NAME) {
		p_v1.subcmd = EC_GPIO_GET_BY_NAME;
		if (strlen(argv[1]) + 1 > sizeof(p_v1.get_value_by_name.name)) {
			fprintf(stderr, "GPIO name too long.\n");
			return -1;
		}
		strcpy(p_v1.get_value_by_name.name, argv[1]);

		rv = ec_command(EC_CMD_GPIO_GET, cmdver, &p_v1, sizeof(p_v1),
				&r_v1, sizeof(r_v1));

		if (rv < 0)
			return rv;

		printf("GPIO %s = %d\n", p_v1.get_value_by_name.name,
		       r_v1.get_value_by_name.val);
		return 0;
	}

	/* Need GPIO count for EC_GPIO_GET_COUNT or EC_GPIO_GET_INFO */
	p_v1.subcmd = EC_GPIO_GET_COUNT;
	rv = ec_command(EC_CMD_GPIO_GET, cmdver, &p_v1, sizeof(p_v1), &r_v1,
			sizeof(r_v1));
	if (rv < 0)
		return rv;

	if (subcmd == EC_GPIO_GET_COUNT) {
		printf("GPIO COUNT = %d\n", r_v1.get_count.val);
		return 0;
	}

	/* subcmd EC_GPIO_GET_INFO */
	num_gpios = r_v1.get_count.val;
	p_v1.subcmd = EC_GPIO_GET_INFO;

	for (i = 0; i < num_gpios; i++) {
		p_v1.get_info.index = i;

		rv = ec_command(EC_CMD_GPIO_GET, cmdver, &p_v1, sizeof(p_v1),
				&r_v1, sizeof(r_v1));
		if (rv < 0)
			return rv;

		printf("%2d %-32s 0x%04X\n", r_v1.get_info.val,
		       r_v1.get_info.name, r_v1.get_info.flags);
	}

	return 0;
}

int cmd_gpio_set(int argc, char *argv[])
{
	struct ec_params_gpio_set p;
	char *e;
	int rv;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <GPIO name> <0 | 1>\n", argv[0]);
		return -1;
	}

	if (strlen(argv[1]) + 1 > sizeof(p.name)) {
		fprintf(stderr, "GPIO name too long.\n");
		return -1;
	}
	strcpy(p.name, argv[1]);

	p.val = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad value.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_GPIO_SET, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("GPIO %s set to %d\n", p.name, p.val);
	return 0;
}

void print_battery_flags(int flags)
{
	printf("  Flags                   0x%02x", flags);
	if (flags & EC_BATT_FLAG_AC_PRESENT)
		printf(" AC_PRESENT");
	if (flags & EC_BATT_FLAG_BATT_PRESENT)
		printf(" BATT_PRESENT");
	if (flags & EC_BATT_FLAG_DISCHARGING)
		printf(" DISCHARGING");
	if (flags & EC_BATT_FLAG_CHARGING)
		printf(" CHARGING");
	if (flags & EC_BATT_FLAG_LEVEL_CRITICAL)
		printf(" LEVEL_CRITICAL");
	if (flags & EC_BATT_FLAG_CUT_OFF)
		printf(" CUT_OFF");
	printf("\n");
}

static int get_battery_command_print_info(
	uint8_t index,
	const struct ec_response_battery_static_info_v2 *const static_r,
	const struct ec_response_battery_dynamic_info *dynamic_r)
{
	struct ec_params_battery_dynamic_info dynamic_p = {
		.index = index,
	};
	struct ec_response_battery_dynamic_info d;
	bool dynamic_from_cmd = false;
	int rv;

	if (!dynamic_r) {
		dynamic_from_cmd = true;
		rv = ec_command(EC_CMD_BATTERY_GET_DYNAMIC, 0, &dynamic_p,
				sizeof(dynamic_p), &d, sizeof(d));
		if (rv < 0)
			return -1;
		dynamic_r = &d;
	}

	printf("Battery %d info:\n", index);

	if (dynamic_r->flags & EC_BATT_FLAG_INVALID_DATA) {
		printf("  Invalid data (not present?)\n");
		return -1;
	}

	if (!is_string_printable(static_r->manufacturer))
		goto cmd_error;
	printf("  Manufacturer:           %s\n", static_r->manufacturer);

	if (!is_string_printable(static_r->device_name))
		goto cmd_error;
	printf("  Device name:            %s\n", static_r->device_name);

	if (!is_string_printable(static_r->chemistry))
		goto cmd_error;
	printf("  Chemistry   :           %s\n", static_r->chemistry);

	if (!is_string_printable(static_r->serial))
		goto cmd_error;
	printf("  Serial number:          %s\n", static_r->serial);

	if (!is_battery_range(static_r->design_capacity))
		goto cmd_error;
	printf("  Design capacity:        %u mAh\n", static_r->design_capacity);

	if (!is_battery_range(dynamic_r->full_capacity))
		goto cmd_error;
	printf("  Last full charge:       %u mAh\n", dynamic_r->full_capacity);

	if (!is_battery_range(static_r->design_voltage))
		goto cmd_error;
	printf("  Design output voltage   %u mV\n", static_r->design_voltage);

	if (!is_battery_range(static_r->cycle_count))
		goto cmd_error;
	printf("  Cycle count             %u\n", static_r->cycle_count);

	if (!is_battery_range(dynamic_r->actual_voltage))
		goto cmd_error;
	printf("  Present voltage         %u mV\n", dynamic_r->actual_voltage);

	/* current can be negative */
	printf("  Present current         %d mA\n", dynamic_r->actual_current);

	if (!is_battery_range(dynamic_r->remaining_capacity))
		goto cmd_error;
	printf("  Remaining capacity      %u mAh\n",
	       dynamic_r->remaining_capacity);

	if (dynamic_from_cmd) {
		if (!is_battery_range(dynamic_r->desired_voltage))
			goto cmd_error;
		printf("  Desired voltage         %u mV\n",
		       dynamic_r->desired_voltage);
	}

	if (dynamic_from_cmd) {
		if (!is_battery_range(dynamic_r->desired_current))
			goto cmd_error;
		printf("  Desired current         %u mA\n",
		       dynamic_r->desired_current);
	}

	print_battery_flags(dynamic_r->flags);
	return 0;

cmd_error:
	fprintf(stderr, "Bad battery info value.\n");
	return -1;
}

static int get_battery_command_v2(uint8_t index)
{
	struct ec_params_battery_static_info static_p = {
		.index = index,
	};
	struct ec_response_battery_static_info_v2 static_r;
	int rv;

	rv = ec_command(EC_CMD_BATTERY_GET_STATIC, 2, &static_p,
			sizeof(static_p), &static_r, sizeof(static_r));
	if (rv < 0) {
		fprintf(stderr, "CMD_BATTERY_GET_STATIC v2 failed: %d\n", rv);
		return -1;
	}

	return get_battery_command_print_info(index, &static_r, NULL);
}

static int get_battery_command_v1(uint8_t index)
{
	struct ec_params_battery_static_info static_p {
		.index = index,
	};
	struct ec_response_battery_static_info_v1 static_r;
	int rv;

	rv = ec_command(EC_CMD_BATTERY_GET_STATIC, 1, &static_p,
			sizeof(static_p), &static_r, sizeof(static_r));
	if (rv < 0) {
		fprintf(stderr, "CMD_BATTERY_GET_STATIC v1 failed: %d\n", rv);
		return -1;
	}

	/* Translate v1 response into v2 to display it */
	struct ec_response_battery_static_info_v2 static_v2 = {
		.design_capacity = static_r.design_capacity,
		.design_voltage = static_r.design_voltage,
		.cycle_count = static_r.cycle_count,
	};
	strncpy(static_v2.manufacturer, static_r.manufacturer_ext,
		sizeof(static_v2.manufacturer) - 1);
	strncpy(static_v2.device_name, static_r.model_ext,
		sizeof(static_v2.device_name) - 1);
	strncpy(static_v2.serial, static_r.serial_ext,
		sizeof(static_v2.serial) - 1);
	strncpy(static_v2.chemistry, static_r.type_ext,
		sizeof(static_v2.chemistry) - 1);

	return get_battery_command_print_info(index, &static_v2, NULL);
}
static int get_battery_command_v0(uint8_t index)
{
	struct ec_params_battery_static_info static_p = {
		.index = index,
	};
	struct ec_response_battery_static_info static_r;
	int rv;

	rv = ec_command(EC_CMD_BATTERY_GET_STATIC, 0, &static_p,
			sizeof(static_p), &static_r, sizeof(static_r));
	if (rv < 0) {
		fprintf(stderr, "CMD_BATTERY_GET_STATIC v0 failed: %d\n", rv);
		return -1;
	}

	/* Translate v0 response into v2 to display it */
	struct ec_response_battery_static_info_v2 static_v2 = {
		.design_capacity = static_r.design_capacity,
		.design_voltage = static_r.design_voltage,
		.cycle_count = static_r.cycle_count,
	};
	strncpy(static_v2.manufacturer, static_r.manufacturer,
		sizeof(static_v2.manufacturer) - 1);
	strncpy(static_v2.device_name, static_r.model,
		sizeof(static_v2.device_name) - 1);
	strncpy(static_v2.serial, static_r.serial,
		sizeof(static_v2.serial) - 1);
	strncpy(static_v2.chemistry, static_r.type,
		sizeof(static_v2.chemistry) - 1);

	return get_battery_command_print_info(index, &static_v2, NULL);
}

static int get_battery_info_from_memmap(void)
{
	struct ec_response_battery_static_info_v2 s2 = {};
	struct ec_response_battery_dynamic_info d = {};
	int val;

	val = read_mapped_mem8(EC_MEMMAP_BATTERY_VERSION);
	if (val < 1) {
		fprintf(stderr, "Battery version %d is not supported\n", val);
		return -1;
	}

	d.flags = read_mapped_mem8(EC_MEMMAP_BATT_FLAG);

	read_mapped_string(EC_MEMMAP_BATT_MFGR, s2.manufacturer,
			   sizeof(s2.manufacturer));

	read_mapped_string(EC_MEMMAP_BATT_MODEL, s2.device_name,
			   sizeof(s2.device_name));

	read_mapped_string(EC_MEMMAP_BATT_TYPE, s2.chemistry,
			   sizeof(s2.chemistry));

	read_mapped_string(EC_MEMMAP_BATT_SERIAL, s2.serial, sizeof(s2.serial));

	s2.design_capacity = read_mapped_mem32(EC_MEMMAP_BATT_DCAP);

	d.full_capacity = read_mapped_mem32(EC_MEMMAP_BATT_LFCC);

	s2.design_voltage = read_mapped_mem32(EC_MEMMAP_BATT_DVLT);

	s2.cycle_count = read_mapped_mem32(EC_MEMMAP_BATT_CCNT);

	d.actual_voltage = read_mapped_mem32(EC_MEMMAP_BATT_VOLT);

	d.actual_current = read_mapped_mem32(EC_MEMMAP_BATT_RATE);
	if (d.flags & EC_BATT_FLAG_DISCHARGING)
		d.actual_current *= -1;

	d.remaining_capacity = read_mapped_mem32(EC_MEMMAP_BATT_CAP);

	if (get_battery_command_print_info(0, &s2, &d)) {
		fprintf(stderr,
			"Bad battery info value. Check protocol version.\n");
		return -1;
	}

	return 0;
}

int cmd_battery(int argc, char *argv[])
{
	char *e;
	int index = 0;

	if (argc > 2) {
		fprintf(stderr, "Usage: %s [index]\n", argv[0]);
		return -1;
	} else if (argc == 2) {
		index = strtol(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad battery index.\n");
			return -1;
		}
	}

	/*
	 * Prefer to use newer hostcmd versions if supported because these allow
	 * us to read longer strings, and always use hostcmd for non-primary
	 * batteries because memmap doesn't export that data.
	 */
	uint32_t versions;
	ec_get_cmd_versions(EC_CMD_BATTERY_GET_STATIC, &versions);

	if (versions & EC_VER_MASK(2))
		return get_battery_command_v2(index);
	else if (versions & EC_VER_MASK(1))
		return get_battery_command_v1(index);
	else if (index > 0)
		return get_battery_command_v0(index);
	else
		return get_battery_info_from_memmap();
}

int cmd_battery_cut_off(int argc, char *argv[])
{
	struct ec_params_battery_cutoff p;
	int cmd_version;
	int rv;

	memset(&p, 0, sizeof(p));
	if (ec_cmd_version_supported(EC_CMD_BATTERY_CUT_OFF, 1)) {
		cmd_version = 1;
		if (argc > 1) {
			if (!strcasecmp(argv[1], "at-shutdown")) {
				p.flags = EC_BATTERY_CUTOFF_FLAG_AT_SHUTDOWN;
			} else {
				fprintf(stderr, "Bad parameter: %s\n", argv[1]);
				return -1;
			}
		}
	} else {
		/* Fall back to version 0 command */
		cmd_version = 0;
		if (argc > 1) {
			if (!strcasecmp(argv[1], "at-shutdown")) {
				fprintf(stderr, "Explicit 'at-shutdown' ");
				fprintf(stderr, "parameter not supported.\n");
			} else {
				fprintf(stderr, "Bad parameter: %s\n", argv[1]);
			}
			return -1;
		}
	}

	rv = ec_command(EC_CMD_BATTERY_CUT_OFF, cmd_version, &p, sizeof(p),
			NULL, 0);
	rv = (rv < 0 ? rv : 0);

	if (rv < 0) {
		fprintf(stderr, "Failed to cut off battery, rv=%d\n", rv);
		fprintf(stderr,
			"It is expected if the rv is -%d "
			"(EC_RES_INVALID_COMMAND) if the battery "
			"doesn't support cut-off function.\n",
			EC_RES_INVALID_COMMAND);
	} else {
		printf("\n");
		printf("SUCCESS. The battery has arranged a cut-off.\n");

		if (cmd_version == 1 &&
		    (p.flags & EC_BATTERY_CUTOFF_FLAG_AT_SHUTDOWN))
			printf("The battery will be cut off after shutdown.\n");
		else
			printf("The system should be shutdown immediately.\n");

		printf("\n");
	}
	return rv;
}

int cmd_battery_vendor_param(int argc, char *argv[])
{
	struct ec_params_battery_vendor_param p;
	struct ec_response_battery_vendor_param r;
	char *e;
	int rv;

	if (argc < 3)
		goto cmd_battery_vendor_param_usage;

	if (!strcasecmp(argv[1], "get"))
		p.mode = BATTERY_VENDOR_PARAM_MODE_GET;
	else if (!strcasecmp(argv[1], "set"))
		p.mode = BATTERY_VENDOR_PARAM_MODE_SET;
	else
		goto cmd_battery_vendor_param_usage;

	p.param = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Invalid param.\n");
		goto cmd_battery_vendor_param_usage;
	}

	if (p.mode == BATTERY_VENDOR_PARAM_MODE_SET) {
		if (argc != 4) {
			fprintf(stderr, "Missing value.\n");
			goto cmd_battery_vendor_param_usage;
		}

		p.value = strtol(argv[3], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Invalid value.\n");
			goto cmd_battery_vendor_param_usage;
		}
	}

	rv = ec_command(EC_CMD_BATTERY_VENDOR_PARAM, 0, &p, sizeof(p), &r,
			sizeof(r));

	if (rv < 0)
		return rv;

	printf("0x%08x\n", r.value);

	return 0;

cmd_battery_vendor_param_usage:
	fprintf(stderr,
		"Usage:\t %s get <param>\n"
		"\t %s set <param> <value>\n",
		argv[0], argv[0]);
	return -1;
}

static void batt_conf_dump(const struct board_batt_params *conf,
			   const char *manuf_name, const char *device_name,
			   uint8_t struct_version, bool is_human_readable)
{
	const struct fuel_gauge_info *fg = &conf->fuel_gauge;
	const struct ship_mode_info *ship = &conf->fuel_gauge.ship_mode;
	const struct sleep_mode_info *sleep = &conf->fuel_gauge.sleep_mode;
	const struct fet_info *fet = &conf->fuel_gauge.fet;
	const struct battery_info *info = &conf->batt_info;
	const char *comma = is_human_readable ? "," : "";

	printf("{\n"); /* Start of root */
	printf("\t\"%s,%s\": {\n", manuf_name, device_name);
	printf("\t\t\"struct_version\": \"0x%02x\",\n", struct_version);
	printf("\t\t\"fuel_gauge\": {\n");
	printf("\t\t\t\"flags\": \"0x%x\",\n", fg->flags);

	printf("\t\t\t\"ship_mode\": {\n");
	printf("\t\t\t\t\"reg_addr\": \"0x%02x\",\n", ship->reg_addr);
	printf("\t\t\t\t\"reg_data\": [ \"0x%04x\", \"0x%04x\" ]%s\n",
	       ship->reg_data[0], ship->reg_data[1], comma);
	printf("\t\t\t},\n");

	printf("\t\t\t\"sleep_mode\": {\n");
	printf("\t\t\t\t\"reg_addr\": \"0x%02x\",\n", sleep->reg_addr);
	printf("\t\t\t\t\"reg_data\": \"0x%04x\"%s\n", sleep->reg_data, comma);
	printf("\t\t\t},\n");

	printf("\t\t\t\"fet\": {\n");
	printf("\t\t\t\t\"reg_addr\": \"0x%02x\",\n", fet->reg_addr);
	printf("\t\t\t\t\"reg_mask\": \"0x%04x\",\n", fet->reg_mask);
	printf("\t\t\t\t\"disconnect_val\": \"0x%04x\",\n",
	       fet->disconnect_val);
	printf("\t\t\t\t\"cfet_mask\": \"0x%04x\",\n", fet->cfet_mask);
	printf("\t\t\t\t\"cfet_off_val\": \"0x%04x\"%s\n", fet->cfet_off_val,
	       comma);
	printf("\t\t\t}%s\n", comma);

	printf("\t\t},\n"); /* end of fuel_gauge */

	printf("\t\t\"batt_info\": {\n");
	printf("\t\t\t\"voltage_max\": %d,\n", info->voltage_max);
	printf("\t\t\t\"voltage_normal\": %d,\n", info->voltage_normal);
	printf("\t\t\t\"voltage_min\": %d,\n", info->voltage_min);
	printf("\t\t\t\"precharge_voltage\": %d,\n", info->precharge_voltage);
	printf("\t\t\t\"precharge_current\": %d,\n", info->precharge_current);
	printf("\t\t\t\"start_charging_min_c\": %d,\n",
	       info->start_charging_min_c);
	printf("\t\t\t\"start_charging_max_c\": %d,\n",
	       info->start_charging_max_c);
	printf("\t\t\t\"charging_min_c\": %d,\n", info->charging_min_c);
	printf("\t\t\t\"charging_max_c\": %d,\n", info->charging_max_c);
	printf("\t\t\t\"discharging_min_c\": %d,\n", info->discharging_min_c);
	printf("\t\t\t\"discharging_max_c\": %d%s\n", info->discharging_max_c,
	       comma);
	printf("\t\t}%s\n", comma); /* end of batt_info */

	printf("\t}%s\n", comma); /* end of board_batt_params */
	printf("}\n"); /* End of root */
}

static void batt_conf_dump_in_c(const struct board_batt_params *conf,
				const char *manuf_name, const char *device_name,
				uint8_t struct_version)
{
	const struct fuel_gauge_info *fg = &conf->fuel_gauge;
	const struct ship_mode_info *ship = &conf->fuel_gauge.ship_mode;
	const struct sleep_mode_info *sleep = &conf->fuel_gauge.sleep_mode;
	const struct fet_info *fet = &conf->fuel_gauge.fet;
	const struct battery_info *info = &conf->batt_info;

	printf("// struct_version = 0x%02x\n", struct_version);
	printf(".manuf_name = \"%s\",\n", manuf_name);
	printf(".device_name = \"%s\",\n", device_name);

	printf(".config = {\n");
	printf("\t.fuel_gauge = {\n");
	printf("\t\t.flags = 0x%x,\n", fg->flags);

	printf("\t\t.ship_mode = {\n");
	printf("\t\t\t.reg_addr = 0x%02x,\n", ship->reg_addr);
	printf("\t\t\t.reg_data = { 0x%04x, 0x%04x },\n", ship->reg_data[0],
	       ship->reg_data[1]);
	printf("\t\t},\n");

	printf("\t\t.sleep_mode = {\n");
	printf("\t\t\t.reg_addr = 0x%02x,\n", sleep->reg_addr);
	printf("\t\t\t.reg_data = 0x%04x,\n", sleep->reg_data);
	printf("\t\t},\n");

	printf("\t\t.fet = {\n");
	printf("\t\t\t.reg_addr = 0x%02x,\n", fet->reg_addr);
	printf("\t\t\t.reg_mask = 0x%04x,\n", fet->reg_mask);
	printf("\t\t\t.disconnect_val = 0x%04x,\n", fet->disconnect_val);
	printf("\t\t\t.cfet_mask = 0x%04x,\n", fet->cfet_mask);
	printf("\t\t\t.cfet_off_val = 0x%04x,\n", fet->cfet_off_val);
	printf("\t\t},\n");

	printf("\t},\n"); /* end of fuel_gauge */

	printf("\t.batt_info = {\n");
	printf("\t\t.voltage_max = %d,\n", info->voltage_max);
	printf("\t\t.voltage_normal = %d,\n", info->voltage_normal);
	printf("\t\t.voltage_min = %d,\n", info->voltage_min);
	printf("\t\t.precharge_voltage= %d,\n", info->precharge_voltage);
	printf("\t\t.precharge_current = %d,\n", info->precharge_current);
	printf("\t\t.start_charging_min_c = %d,\n", info->start_charging_min_c);
	printf("\t\t.start_charging_max_c = %d,\n", info->start_charging_max_c);
	printf("\t\t.charging_min_c = %d,\n", info->charging_min_c);
	printf("\t\t.charging_max_c = %d,\n", info->charging_max_c);
	printf("\t\t.discharging_min_c = %d,\n", info->discharging_min_c);
	printf("\t\t.discharging_max_c = %d,\n", info->discharging_max_c);
	printf("\t},\n"); /* end of batt_info */

	printf("},\n"); /* end of board_batt_params */
}

static int read_u32_from_json(base::Value::Dict *dict, const char *key,
			      uint32_t *value)
{
	std::string *str = dict->FindString(key);
	char *e;

	if (str == nullptr) {
		if (verbose)
			printf("Key '%s' not found. Ignored.\n", key);
		return 0;
	}

	*value = strtoul(str->c_str(), &e, 0);
	if (e && *e) {
		fprintf(stderr, "Failed to parse '%s: %s'\n", key,
			str->c_str());
		return -1;
	}

	return 0;
}

static int read_u16_from_json(base::Value::Dict *dict, const char *key,
			      uint16_t *value)
{
	std::string *str = dict->FindString(key);
	char *e;

	if (str == nullptr) {
		if (verbose)
			printf("Key '%s' not found. Ignored.\n", key);
		return 0;
	}

	*value = strtoul(str->c_str(), &e, 0);
	if (e && *e) {
		fprintf(stderr, "Failed to parse '%s: %s'\n", key,
			str->c_str());
		return -1;
	}

	return 0;
}

static int read_u8_from_json(base::Value::Dict *dict, const char *key,
			     uint8_t *value)
{
	const std::string *str = dict->FindString(key);
	char *e;

	if (str == nullptr) {
		if (verbose)
			printf("Key '%s' not found. Ignored.\n", key);
		return 0;
	}

	*value = strtoul(str->c_str(), &e, 0);
	if (e && *e) {
		fprintf(stderr, "Failed to parse '%s: %s'\n", key,
			str->c_str());
		return -1;
	}

	return 0;
}

static int read_battery_config_from_json(base::Value::Dict *root_dict,
					 struct board_batt_params *config)
{
	int i;
	char *e;

	base::Value::Dict *fuel_gauge = root_dict->FindDict("fuel_gauge");
	if (fuel_gauge == nullptr) {
		fprintf(stderr, "Error. fuel_gauge not found.\n");
		return -1;
	}
	if (read_u32_from_json(fuel_gauge, "flags", &config->fuel_gauge.flags))
		return -1;
	if (read_u32_from_json(fuel_gauge, "board_flags",
			       &config->fuel_gauge.board_flags))
		return -1;

	base::Value::Dict *ship_mode = fuel_gauge->FindDict("ship_mode");
	if (ship_mode != nullptr) {
		struct ship_mode_info *sm = &config->fuel_gauge.ship_mode;

		if (read_u8_from_json(ship_mode, "reg_addr", &sm->reg_addr))
			return -1;

		base::Value::List *reg_data = ship_mode->FindList("reg_data");
		for (i = 0; i < reg_data->size() && i < SHIP_MODE_WRITES; ++i) {
			const std::string *str = (*reg_data)[i].GetIfString();
			sm->reg_data[i] = strtoul(str->c_str(), &e, 0);
			if (e && *e) {
				fprintf(stderr,
					"Failed to parse reg_data: %s\n",
					str->c_str());
				return -1;
			}
		};
	}

	base::Value::Dict *sleep_mode = fuel_gauge->FindDict("sleep_mode");
	if (sleep_mode != nullptr) {
		struct sleep_mode_info *sm = &config->fuel_gauge.sleep_mode;

		if (read_u8_from_json(sleep_mode, "reg_addr", &sm->reg_addr))
			return -1;
		if (read_u16_from_json(sleep_mode, "reg_data", &sm->reg_data))
			return -1;
	}

	base::Value::Dict *fet = fuel_gauge->FindDict("fet");
	if (fet != nullptr) {
		struct fet_info *fi = &config->fuel_gauge.fet;

		if (read_u8_from_json(fet, "reg_addr", &fi->reg_addr))
			return -1;
		if (read_u16_from_json(fet, "reg_mask", &fi->reg_mask))
			return -1;
		if (read_u16_from_json(fet, "disconnect_val",
				       &fi->disconnect_val))
			return -1;
		if (read_u16_from_json(fet, "cfet_mask", &fi->cfet_mask))
			return -1;
		if (read_u16_from_json(fet, "cfet_off_val", &fi->cfet_off_val))
			return -1;
	}

	base::Value::Dict *batt_info = root_dict->FindDict("batt_info");
	if (batt_info == nullptr) {
		fprintf(stderr, "Error. batt_info not found.\n");
		return -1;
	}

	struct battery_info *bi = &config->batt_info;
	std::optional<int> voltage_max = batt_info->FindInt("voltage_max");
	std::optional<int> voltage_normal =
		batt_info->FindInt("voltage_normal");
	std::optional<int> voltage_min = batt_info->FindInt("voltage_min");
	std::optional<int> precharge_voltage =
		batt_info->FindInt("precharge_voltage");
	std::optional<int> precharge_current =
		batt_info->FindInt("precharge_current");
	std::optional<int> start_charging_min_c =
		batt_info->FindInt("start_charging_min_c");
	std::optional<int> start_charging_max_c =
		batt_info->FindInt("start_charging_max_c");
	std::optional<int> charging_min_c =
		batt_info->FindInt("charging_min_c");
	std::optional<int> charging_max_c =
		batt_info->FindInt("charging_max_c");
	std::optional<int> discharging_min_c =
		batt_info->FindInt("discharging_min_c");
	std::optional<int> discharging_max_c =
		batt_info->FindInt("discharging_max_c");
	std::optional<int> vendor_param_start =
		batt_info->FindInt("vendor_param_start");
	bi->voltage_max = voltage_max.value();
	bi->voltage_normal = voltage_normal.value();
	bi->voltage_min = voltage_min.value();
	bi->precharge_voltage = precharge_voltage.value();
	bi->precharge_current = precharge_current.value();
	bi->start_charging_min_c = start_charging_min_c.value();
	bi->start_charging_max_c = start_charging_max_c.value();
	bi->charging_min_c = charging_min_c.value();
	bi->charging_max_c = charging_max_c.value();
	bi->discharging_min_c = discharging_min_c.value();
	bi->discharging_max_c = discharging_max_c.value();
	if (vendor_param_start != absl::nullopt)
		bi->vendor_param_start = vendor_param_start.value();

	return 0;
}

static void cmd_battery_config_help(const char *cmd)
{
	fprintf(stderr,
		"\n"
		"Usage: %s get [-c/-j/-h] [<index>]\n"
		"    Print active battery config in one of following formats:\n"
		"    JSON5 (-h), JSON (-j), C-struct (-c). Default output format is\n"
		"    JSON5 (-h).\n"
		"    If <index> is specified, a config is read from CBI.\n"
		"\n"
		"Usage: %s set <json_file> <manuf_name> <device_name> [<index>]\n"
		"    Copy battery config from file to CBI.\n"
		"\n"
		"    json_file: Path to JSON file containing battery configs\n"
		"    manuf_name: Manufacturer's name. Up to 31 chars.\n"
		"    device_name: Battery's name. Up to 31 chars.\n"
		"    index: Index of config in CBI to be get or set.\n"
		"\n"
		"    Run `ectool battery` for <manuf_name> and <device_name>\n",
		cmd, cmd);
}

static int cmd_battery_config_get(int argc, char *argv[])
{
	struct batt_conf_header *head;
	struct board_batt_params conf;
	char manuf_name[SBS_MAX_STR_OBJ_SIZE];
	char device_name[SBS_MAX_STR_OBJ_SIZE];
	uint8_t *p;
	int expected;
	bool in_json = true;
	bool in_json_human = false;
	int rv;
	int c;
	int index = -1;

	while ((c = getopt(argc, argv, "chj")) != -1) {
		switch (c) {
		case 'c':
			in_json = false;
			break;
		case 'j':
			in_json_human = false;
			break;
		case 'h':
			in_json_human = true;
			break;
		case '?':
			/* getopt prints error message. */
			cmd_battery_config_help("bcfg");
			return -1;
		default:
			return -1;
		}
	}

	if (optind < argc) {
		char *e;
		index = strtol(argv[optind], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad index: '%s'\n", argv[optind]);
			return -1;
		}
		optind++;
	}

	if (optind < argc) {
		fprintf(stderr, "Unknown argument '%s'\n", argv[optind]);
		cmd_battery_config_help("bcfg");
		return -1;
	}

	if (index < 0) {
		rv = ec_command(EC_CMD_BATTERY_CONFIG, 0, NULL, 0, ec_inbuf,
				ec_max_insize);
	} else {
		struct ec_params_get_cbi pa = { 0 };

		pa.tag = index + CBI_TAG_BATTERY_CONFIG;
		rv = ec_command(EC_CMD_GET_CROS_BOARD_INFO, 0, &pa, sizeof(pa),
				ec_inbuf, ec_max_insize);
		if (rv == -EC_RES_INVALID_PARAM - EECRESULT)
			fprintf(stderr, "Config[%d] not found in CBI.\n",
				index);
	}
	if (rv < 0)
		return rv;

	head = (struct batt_conf_header *)ec_inbuf;
	if (head->struct_version > EC_BATTERY_CONFIG_STRUCT_VERSION) {
		fprintf(stderr,
			"Struct version mismatch. Supported: 0x00 ~ 0x%02x.\n",
			EC_BATTERY_CONFIG_STRUCT_VERSION);
		return -1;
	}

	/* Now we know it's ok to read the rest of the header. */

	expected = sizeof(*head) + head->manuf_name_size +
		   head->device_name_size + sizeof(struct board_batt_params);
	if (rv != expected) {
		fprintf(stderr, "Size mismatch: %d (expected=%d)\n", rv,
			expected);
		fprintf(stderr, ".manuf_name_size = %d\n",
			head->manuf_name_size);
		fprintf(stderr, ".device_name_size = %d\n",
			head->device_name_size);
		return -1;
	}

	/* Now we know it's ok to parse the payload. */
	p = (uint8_t *)head;
	p += sizeof(*head);
	memset(manuf_name, 0, sizeof(manuf_name));
	memcpy(manuf_name, p, head->manuf_name_size);
	p += head->manuf_name_size;
	memset(device_name, 0, sizeof(device_name));
	memcpy(device_name, p, head->device_name_size);
	p += head->device_name_size;
	memcpy(&conf, p, sizeof(conf));
	if (in_json) {
		batt_conf_dump(&conf, manuf_name, device_name,
			       head->struct_version, in_json_human);
	} else {
		batt_conf_dump_in_c(&conf, manuf_name, device_name,
				    head->struct_version);
	}

	return 0;
}

static int cmd_battery_config_set(int argc, char *argv[])
{
	FILE *fp = NULL;
	int size;
	char *json = NULL;
	const char *json_file;
	const char *manuf_name;
	const char *device_name;
	char identifier[SBS_MAX_STR_OBJ_SIZE * 2];
	struct board_batt_params config;
	struct ec_params_set_cbi *p = (struct ec_params_set_cbi *)ec_outbuf;
	struct batt_conf_header *header = (struct batt_conf_header *)p->data;
	uint8_t *d = (uint8_t *)header;
	uint8_t struct_version = EC_BATTERY_CONFIG_STRUCT_VERSION;
	int rv;
	int index = 0;

	if (argc < 4 || 5 < argc) {
		fprintf(stderr, "Invalid number of arguments.\n");
		cmd_battery_config_help("bcfg");
		return -1;
	} else if (argc == 5) {
		char *e;
		index = strtol(argv[4], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad index: '%s'\n", argv[4]);
			return -1;
		}
	}

	json_file = argv[1];
	manuf_name = argv[2];
	device_name = argv[3];

	if (strlen(manuf_name) > SBS_MAX_STR_SIZE) {
		fprintf(stderr, "manuf_name is too long.");
		return -1;
	}

	if (strlen(device_name) > SBS_MAX_STR_SIZE) {
		fprintf(stderr, "device_name is too long.");
		return -1;
	}

	fp = fopen(json_file, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open %s: %s\n", json_file,
			strerror(errno));
		return -1;
	}
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	rewind(fp);

	json = (char *)malloc(size);
	if (!json) {
		fprintf(stderr, "Failed to allocate memory.\n");
		fclose(fp);
		return -1;
	}

	if (fread(json, 1, size, fp) != size) {
		fprintf(stderr, "Failed to read %s\n", json_file);
		fclose(fp);
		free(json);
		return -1;
	}

	fclose(fp);

	std::optional<base::Value> root =
		base::JSONReader::Read(json, base::JSON_PARSE_RFC);
	if (root == std::nullopt) {
		fprintf(stderr, "File %s isn't properly formed JSON file.\n",
			json_file);
		free(json);
		return -1;
	}
	base::Value::Dict *dict = root->GetIfDict();
	if (dict == nullptr) {
		fprintf(stderr, "Failed to get dictionary from JSON file.\n");
		free(json);
		return -1;
	}
	if (read_u8_from_json(dict, "struct_version", &struct_version))
		return -1;

	/* Clear the dst to ensure it'll be null-terminated. */
	memset(identifier, 0, sizeof(identifier));
	sprintf(identifier, "%s,%s", manuf_name, device_name);
	base::Value::Dict *root_dict = dict->FindDict(identifier);
	if (root_dict == nullptr) {
		fprintf(stderr,
			"Config matching identifier=%s not found in %s.\n",
			identifier, json_file);
		free(json);
		return -1;
	}
	if (read_u8_from_json(root_dict, "struct_version", &struct_version))
		return -1;

	/* Clear config to ensure unspecified (optional) fields are 0. */
	memset(&config, 0, sizeof(config));
	if (read_battery_config_from_json(root_dict, &config))
		return -1;

	/* Data is ready. Create a packet for EC_CMD_SET_CROS_BOARD_INFO. */
	memset(p, 0, ec_max_outsize);
	header->struct_version = struct_version;
	header->manuf_name_size = strlen(manuf_name);
	header->device_name_size = strlen(device_name);
	d += sizeof(*header);
	memcpy(d, manuf_name, header->manuf_name_size);
	d += header->manuf_name_size;
	memcpy(d, device_name, header->device_name_size);
	d += header->device_name_size;
	memcpy(d, &config, sizeof(config));

	p->tag = index + CBI_TAG_BATTERY_CONFIG;
	p->size = sizeof(struct batt_conf_header) + header->manuf_name_size +
		  header->device_name_size + sizeof(config);
	size = sizeof(*p);
	size += p->size;

	rv = ec_command(EC_CMD_SET_CROS_BOARD_INFO, 0, p, size, NULL, 0);
	if (rv < 0) {
		if (rv == -EC_RES_ACCESS_DENIED - EECRESULT)
			fprintf(stderr, "Failed. CBI is write-protected.\n");
		else
			fprintf(stderr, "Error code: %d\n", rv);
	} else {
		printf("Successfully wrote battery config in CBI\n");
	}

	free(json);

	return rv;
}

static int cmd_battery_config(int argc, char *argv[])
{
	if (argc > 1 && !strcasecmp(argv[1], "get"))
		return cmd_battery_config_get(--argc, ++argv);
	else if (argc > 1 && !strcasecmp(argv[1], "set"))
		return cmd_battery_config_set(--argc, ++argv);

	fprintf(stderr, "Invalid sub-command '%s'\n",
		argv[1] ? argv[1] : "(null)");
	cmd_battery_config_help(argv[0]);
	return -1;
}

int cmd_board_version(int argc, char *argv[])
{
	struct ec_response_board_version response;
	int rv;

	rv = ec_command(EC_CMD_GET_BOARD_VERSION, 0, NULL, 0, &response,
			sizeof(response));
	if (rv < 0)
		return rv;

	printf("%d\n", response.board_version);
	return rv;
}

int cmd_boottime(int argc, char *argv[])
{
	struct ec_response_get_boot_time response;
	int rv;

	rv = ec_command(EC_CMD_GET_BOOT_TIME, 0, NULL, 0, &response,
			sizeof(response));
	if (rv < 0)
		return rv;

	printf("arail: %" PRIu64 "\n", response.timestamp[ARAIL]);
	printf("rsmrst: %" PRIu64 "\n", response.timestamp[RSMRST]);
	printf("espirst: %" PRIu64 "\n", response.timestamp[ESPIRST]);
	printf("pltrst_low: %" PRIu64 "\n", response.timestamp[PLTRST_LOW]);
	printf("pltrst_high: %" PRIu64 "\n", response.timestamp[PLTRST_HIGH]);
	printf("cnt: %" PRIu16 "\n", response.cnt);
	printf("ec_cur_time: %" PRIu64 "\n", response.timestamp[EC_CUR_TIME]);
	return rv;
}

static void cmd_cbi_help(char *cmd)
{
	fprintf(stderr,
		"  Usage: %s get <tag> [get_flag]\n"
		"  Usage: %s set <tag> <value> <size> [set_flag]\n"
		"  Usage: %s set <tag> <string/hex> <*> [set_flag]\n"
		"  Usage: %s remove <tag> [set_flag]\n"
		"    <tag> is one of:\n"
		"      0: BOARD_VERSION\n"
		"      1: OEM_ID\n"
		"      2: SKU_ID\n"
		"      3: DRAM_PART_NUM (string)\n"
		"      4: OEM_NAME (string)\n"
		"      5: MODEL_ID\n"
		"      6: FW_CONFIG\n"
		"      7: PCB_VENDOR\n"
		"      8: SSFC\n"
		"      9: REWORK_ID\n"
		"      10: FACTORY_CALIBRATION_DATA\n"
		"      11: COMMON_CONTROL\n"
		"      12: BATTERY_CONFIG (hex)\n"
		"    <size> is the size of the data in byte. It should be zero for\n"
		"      string types.\n"
		"    <value/string> is an integer or a string to be set\n"
		"    <*> is unused but must be present (e.g. '0')\n"
		"    <hex> is a hex string\n"
		"    [get_flag] is combination of:\n"
		"      01b: Invalidate cache and reload data from EEPROM\n"
		"    [set_flag] is combination of:\n"
		"      01b: Skip write to EEPROM. Use for back-to-back writes\n"
		"      10b: Set all fields to defaults first\n",
		cmd, cmd, cmd, cmd);
}

static int cmd_cbi_is_string_field(enum cbi_data_tag tag)
{
	return tag == CBI_TAG_DRAM_PART_NUM || tag == CBI_TAG_OEM_NAME;
}

static int cmd_cbi_is_binary_field(enum cbi_data_tag tag)
{
	return CBI_TAG_BATTERY_CONFIG <= tag &&
	       tag <= CBI_TAG_BATTERY_CONFIG_15;
}

/*
 * Write value to CBI
 *
 * TODO: Support asynchronous write
 */
static int cmd_cbi(int argc, char *argv[])
{
	enum cbi_data_tag tag;
	char *e;
	int rv;
	int i;

	if (argc < 3) {
		fprintf(stderr, "Invalid number of params\n");
		cmd_cbi_help(argv[0]);
		return -1;
	}

	/* Tag */
	tag = (enum cbi_data_tag)(strtol(argv[2], &e, 0));
	if (e && *e) {
		fprintf(stderr, "Bad tag\n");
		return -1;
	}

	if (!strcasecmp(argv[1], "get")) {
		struct ec_params_get_cbi p = { 0 };

		p.tag = tag;
		if (argc > 3) {
			p.flag = strtol(argv[3], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad flag\n");
				return -1;
			}
		}
		rv = ec_command(EC_CMD_GET_CROS_BOARD_INFO, 0, &p, sizeof(p),
				ec_inbuf, ec_max_insize);
		if (rv < 0) {
			fprintf(stderr, "Error code: %d\n", rv);
			return rv;
		}
		if (rv < sizeof(uint8_t)) {
			fprintf(stderr, "Invalid size: %d\n", rv);
			return -1;
		}
		if (cmd_cbi_is_string_field(tag)) {
			printf("%.*s", rv, (const char *)ec_inbuf);
		} else if (cmd_cbi_is_binary_field(tag)) {
			const uint8_t *const buf =
				(const uint8_t *const)(ec_inbuf);
			for (i = 0; i < rv; i++)
				printf("%02x", buf[i]);
		} else {
			const uint8_t *const buffer =
				(const uint8_t *const)(ec_inbuf);
			uint64_t int_value = 0;
			for (i = 0; i < rv; i++)
				int_value |= (uint64_t)buffer[i] << (i * 8);

			printf("As uint: %llu (0x%llx)\n",
			       (unsigned long long)int_value,
			       (unsigned long long)int_value);
			printf("As binary:");
			for (i = 0; i < rv; i++) {
				if (i % 32 == 31)
					printf("\n");
				printf(" %02x", buffer[i]);
			}
		}
		printf("\n");
		return 0;
	} else if (!strcasecmp(argv[1], "set")) {
		struct ec_params_set_cbi *p =
			(struct ec_params_set_cbi *)ec_outbuf;
		void *val_ptr;
		uint64_t val = 0;
		size_t size;
		uint8_t bad_size = 0;
		uint8_t *buf = NULL;

		if (argc < 5) {
			fprintf(stderr, "Invalid number of params\n");
			cmd_cbi_help(argv[0]);
			return -1;
		}
		memset(p, 0, ec_max_outsize);
		p->tag = tag;

		if (cmd_cbi_is_string_field(tag)) {
			val_ptr = argv[3];
			size = strlen((char *)(val_ptr)) + 1;
		} else if (cmd_cbi_is_binary_field(tag)) {
			const char *p = argv[3];

			size = strlen(p);
			if (size % 2) {
				fprintf(stderr,
					"\n<hex> length must be even.\n");
				return -1;
			}

			size /= 2;
			buf = (uint8_t *)malloc(size);
			if (!buf) {
				fprintf(stderr,
					"\nFailed to allocate buffer.\n");
				return -1;
			}
			for (i = 0; i < size; i++) {
				char t[3] = {};

				memcpy(t, p, 2);
				buf[i] = strtoul(t, &e, 16);
				if (e && *e) {
					fprintf(stderr, "\nBad value: '%s'\n",
						t);
					free(buf);
					return -1;
				}
				p += 2;
			}
			val_ptr = buf;
		} else {
			val = strtoul(argv[3], &e, 0);
			/* strtoul sets an errno for invalid input. If the value
			 * read is out of range of representable values by an
			 * unsigned long int, the function returns ULONG_MAX
			 * or ULONG_MIN and the errno is set to ERANGE.
			 */
			if ((e && *e) || errno == ERANGE) {
				fprintf(stderr, "Bad value\n");
				return -1;
			}
			size = strtol(argv[4], &e, 0);
			if (tag == CBI_TAG_REWORK_ID) {
				if ((e && *e) || size < 1 || size > 8 ||
				    (size < 8 && val >= (1ull << size * 8)))
					bad_size = 1;
			} else {
				if ((e && *e) || size < 1 || 4 < size ||
				    val >= (1ull << size * 8))
					bad_size = 1;
			}
			if (bad_size == 1) {
				fprintf(stderr, "Bad size: %zu\n", size);
				return -1;
			}

			val_ptr = &val;
		}

		if (size > ec_max_outsize - sizeof(*p)) {
			fprintf(stderr, "Size exceeds parameter buffer: %zu\n",
				size);
			return -1;
		}
		/* Little endian */
		memcpy(p->data, val_ptr, size);
		free(buf);
		val_ptr = NULL;
		p->size = size;
		if (argc > 5) {
			p->flag = strtol(argv[5], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad flag\n");
				return -1;
			}
		}
		rv = ec_command(EC_CMD_SET_CROS_BOARD_INFO, 0, p,
				sizeof(*p) + size, NULL, 0);
		if (rv < 0) {
			if (rv == -EC_RES_ACCESS_DENIED - EECRESULT)
				fprintf(stderr,
					"Write-protect is enabled or "
					"EC explicitly refused to change the "
					"requested field.\n");
			else
				fprintf(stderr, "Error code: %d\n", rv);
			return rv;
		}
		return 0;
	} else if (!strcasecmp(argv[1], "remove")) {
		struct ec_params_set_cbi p = { 0 };

		p.tag = tag;
		p.size = 0;
		if (argc > 3) {
			p.flag = strtol(argv[3], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad flag\n");
				return -1;
			}
		}
		rv = ec_command(EC_CMD_SET_CROS_BOARD_INFO, 0, &p, sizeof(p),
				NULL, 0);
		if (rv < 0) {
			if (rv == -EC_RES_ACCESS_DENIED - EECRESULT)
				fprintf(stderr,
					"Write-protect is enabled or "
					"EC explicitly refused to change the "
					"requested field.\n");
			else
				fprintf(stderr, "Error code: %d\n", rv);
			return rv;
		}
		return 0;
	}

	fprintf(stderr, "Invalid sub command: %s\n", argv[1]);
	cmd_cbi_help(argv[0]);

	return -1;
}

int cmd_chipinfo(int argc, char *argv[])
{
	struct ec_response_get_chip_info info;
	int rv;

	printf("Chip info:\n");

	rv = ec_command(EC_CMD_GET_CHIP_INFO, 0, NULL, 0, &info, sizeof(info));
	if (rv < 0)
		return rv;
	printf("  vendor:    %s\n", info.vendor);
	printf("  name:      %s\n", info.name);
	printf("  revision:  %s\n", info.revision);

	return 0;
}

int cmd_proto_info(int argc, char *argv[])
{
	struct ec_response_get_protocol_info info;
	int rv;
	int i;

	printf("Protocol info:\n");

	rv = ec_command(EC_CMD_GET_PROTOCOL_INFO, 0, NULL, 0, &info,
			sizeof(info));
	if (rv < 0) {
		fprintf(stderr, "Protocol info unavailable.  EC probably only "
				"supports protocol version 2.\n");
		return rv;
	}

	printf("  protocol versions:");
	for (i = 0; i < 32; i++) {
		if (info.protocol_versions & BIT(i))
			printf(" %d", i);
	}
	printf("\n");

	printf("  max request:  %4d bytes\n", info.max_request_packet_size);
	printf("  max response: %4d bytes\n", info.max_response_packet_size);
	printf("  flags: 0x%08x\n", info.flags);
	if (info.flags & EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED)
		printf("    EC_RES_IN_PROGRESS supported\n");
	return 0;
}

static int ec_hash_help(const char *cmd)
{
	printf("Usage:\n");
	printf("  %s                        - get last hash\n", cmd);
	printf("  %s abort                  - abort hashing\n", cmd);
	printf("  %s start [<offset> <size> [<nonce>]] - start hashing\n", cmd);
	printf("  %s recalc [<offset> <size> [<nonce>]] - sync rehash\n", cmd);
	printf("\n"
	       "If <offset> is RO or RW, offset and size are computed\n"
	       "automatically for the EC-RO or EC-RW firmware image.\n");

	return 0;
}

static int ec_hash_print(const struct ec_response_vboot_hash *r)
{
	int i;

	if (r->status == EC_VBOOT_HASH_STATUS_BUSY) {
		printf("status:  busy\n");
		return 0;
	} else if (r->status == EC_VBOOT_HASH_STATUS_NONE) {
		printf("status:  unavailable\n");
		return 0;
	} else if (r->status != EC_VBOOT_HASH_STATUS_DONE) {
		printf("status:  %d\n", r->status);
		return 0;
	}

	printf("status:  done\n");
	if (r->hash_type == EC_VBOOT_HASH_TYPE_SHA256)
		printf("type:    SHA-256\n");
	else
		printf("type:    %d\n", r->hash_type);

	printf("offset:  0x%08x\n", r->offset);
	printf("size:    0x%08x\n", r->size);

	printf("hash:    ");
	for (i = 0; i < r->digest_size; i++)
		printf("%02x", r->hash_digest[i]);
	printf("\n");
	return 0;
}

int cmd_ec_hash(int argc, char *argv[])
{
	struct ec_params_vboot_hash p;
	struct ec_response_vboot_hash r;
	char *e;
	int rv;

	memset(&p, 0, sizeof(p));
	if (argc < 2) {
		/* Get hash status */
		p.cmd = EC_VBOOT_HASH_GET;
		rv = ec_command(EC_CMD_VBOOT_HASH, 0, &p, sizeof(p), &r,
				sizeof(r));
		if (rv < 0)
			return rv;

		return ec_hash_print(&r);
	}

	if (argc == 2 && !strcasecmp(argv[1], "abort")) {
		/* Abort hash calculation */
		p.cmd = EC_VBOOT_HASH_ABORT;
		rv = ec_command(EC_CMD_VBOOT_HASH, 0, &p, sizeof(p), &r,
				sizeof(r));
		return (rv < 0 ? rv : 0);
	}

	/* The only other commands are start and recalc */
	if (!strcasecmp(argv[1], "start"))
		p.cmd = EC_VBOOT_HASH_START;
	else if (!strcasecmp(argv[1], "recalc"))
		p.cmd = EC_VBOOT_HASH_RECALC;
	else
		return ec_hash_help(argv[0]);

	p.hash_type = EC_VBOOT_HASH_TYPE_SHA256;

	if (argc < 3) {
		fprintf(stderr, "Must specify offset\n");
		return -1;
	}

	if (!strcasecmp(argv[2], "ro")) {
		p.offset = EC_VBOOT_HASH_OFFSET_RO;
		p.size = 0;
		printf("Hashing EC-RO...\n");
	} else if (!strcasecmp(argv[2], "rw")) {
		p.offset = EC_VBOOT_HASH_OFFSET_ACTIVE;
		p.size = 0;
		printf("Hashing EC-RW...\n");
	} else if (argc < 4) {
		fprintf(stderr, "Must specify size\n");
		return -1;
	} else {
		p.offset = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad offset.\n");
			return -1;
		}
		p.size = strtol(argv[3], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad size.\n");
			return -1;
		}
		printf("Hashing %d bytes at offset %d...\n", p.size, p.offset);
	}

	if (argc == 5) {
		/*
		 * Technically nonce can be any binary data up to 64 bytes,
		 * but this command only supports a 32-bit value.
		 */
		uint32_t nonce = strtol(argv[4], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad nonce integer.\n");
			return -1;
		}
		memcpy(p.nonce_data, &nonce, sizeof(nonce));
		p.nonce_size = sizeof(nonce);
	} else
		p.nonce_size = 0;

	rv = ec_command(EC_CMD_VBOOT_HASH, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;

	/* Start command doesn't wait for hashing to finish */
	if (p.cmd == EC_VBOOT_HASH_START)
		return 0;

	/* Recalc command does wait around, so a result is ready now */
	return ec_hash_print(&r);
}

int cmd_rtc_get(int argc, char *argv[])
{
	struct ec_response_rtc r;
	int rv;

	rv = ec_command(EC_CMD_RTC_GET_VALUE, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("Current time: 0x%08x (%d)\n", r.time, r.time);
	return 0;
}

int cmd_rtc_set(int argc, char *argv[])
{
	struct ec_params_rtc p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <time>\n", argv[0]);
		return -1;
	}
	p.time = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad time.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_RTC_SET_VALUE, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("Time set.\n");
	return 0;
}

int cmd_rtc_set_alarm(int argc, char *argv[])
{
	struct ec_params_rtc p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <sec>\n", argv[0]);
		return -1;
	}
	p.time = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad time.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_RTC_SET_ALARM, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	if (p.time == 0)
		printf("Disabling alarm.\n");
	else
		printf("Alarm set to go off in %d secs.\n", p.time);
	return 0;
}

int cmd_rtc_get_alarm(int argc, char *argv[])
{
	struct ec_response_rtc r;
	int rv;

	rv = ec_command(EC_CMD_RTC_GET_ALARM, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;

	if (r.time == 0)
		printf("Alarm not set\n");
	else
		printf("Alarm to go off in %d secs\n", r.time);
	return 0;
}

int cmd_console(int argc, char *argv[])
{
	char *out = (char *)ec_inbuf;
	int rv;

	/* Snapshot the EC console */
	rv = ec_command(EC_CMD_CONSOLE_SNAPSHOT, 0, NULL, 0, NULL, 0);
	if (rv < 0)
		return rv;

	/* Loop and read from the snapshot until it's done */
	while (1) {
		rv = ec_command(EC_CMD_CONSOLE_READ, 0, NULL, 0, ec_inbuf,
				ec_max_insize);
		if (rv < 0)
			return rv;

		/* Empty response means done */
		if (!rv || !*out)
			break;

		/* Make sure output is null-terminated, then dump it */
		out[ec_max_insize - 1] = '\0';
		fputs(out, stdout);
	}
	printf("\n");
	return 0;
}
struct param_info {
	const char *name; /* name of this parameter */
	const char *help; /* help message */
	int size; /* size in bytes */
	int offset; /* offset within structure */
};

#define FIELD(fname, field, help_str)                                       \
	{                                                                   \
		.name = fname, .help = help_str,                            \
		.size = sizeof(((struct ec_mkbp_config *)NULL)->field),     \
		.offset = __builtin_offsetof(struct ec_mkbp_config, field), \
	}

static const struct param_info keyconfig_params[] = {
	FIELD("scan_period", scan_period_us, "period between scans"),
	FIELD("poll_timeout", poll_timeout_us,
	      "revert to irq mode after no activity for this long"),
	FIELD("min_post_scan_delay", min_post_scan_delay_us,
	      "minimum post-scan delay before starting a new scan"),
	FIELD("output_settle", output_settle_us,
	      "delay to wait for output to settle"),
	FIELD("debounce_down", debounce_down_us,
	      "time for debounce on key down"),
	FIELD("debounce_up", debounce_up_us, "time for debounce on key up"),
	FIELD("fifo_max_depth", fifo_max_depth,
	      "maximum depth to allow for fifo (0 = disable)"),
	FIELD("flags", flags, "0 to disable scanning, 1 to enable"),
};

static const struct param_info *find_field(const struct param_info *params,
					   int count, const char *name,
					   unsigned int *nump)
{
	const struct param_info *param;
	int i;

	for (i = 0, param = params; i < count; i++, param++) {
		if (0 == strcmp(param->name, name)) {
			if (nump)
				*nump = i;
			return param;
		}
	}

	fprintf(stderr, "Unknown parameter '%s'\n", name);
	return NULL;
}

static int get_value(const struct param_info *param, const char *config)
{
	const char *field;

	field = config + param->offset;
	switch (param->size) {
	case 1:
		return *(uint8_t *)field;
	case 2:
		return *(uint16_t *)field;
	case 4:
		return *(uint32_t *)field;
	default:
		fprintf(stderr, "Internal error: unknown size %d\n",
			param->size);
	}

	return -1;
}

static int show_fields(struct ec_mkbp_config *config, int argc, char *argv[])
{
	const struct param_info *param;
	uint32_t mask;
	int i;

	if (!argc) {
		mask = -1U; /* show all fields */
	} else {
		mask = 0;
		while (argc > 0) {
			unsigned int num;

			param = find_field(keyconfig_params,
					   ARRAY_SIZE(keyconfig_params),
					   argv[0], &num);
			if (!param)
				return -1;
			mask |= 1 << num;
			argc--;
			argv++;
		}
	}

	param = keyconfig_params;
	for (i = 0; i < ARRAY_SIZE(keyconfig_params); i++, param++) {
		if (mask & BIT(i)) {
			fprintf(stderr, "%-12s   %u\n", param->name,
				get_value(param, (char *)config));
		}
	}

	return 0;
}

static int cmd_kbinfo(int argc, char *argv[])
{
	struct ec_params_mkbp_info info = {
		.info_type = EC_MKBP_INFO_KBD,
	};
	struct ec_response_mkbp_info resp;
	int rv;

	if (argc > 1) {
		fprintf(stderr, "Too many args\n");
		return -1;
	}
	rv = ec_command(EC_CMD_MKBP_INFO, 0, &info, sizeof(info), &resp,
			sizeof(resp));
	if (rv < 0)
		return rv;

	printf("Matrix rows: %d\n", resp.rows);
	printf("Matrix columns: %d\n", resp.cols);

	return 0;
}

static int cmd_keyconfig(int argc, char *argv[])
{
	struct ec_params_mkbp_set_config req;
	int cmd;
	int rv;

	if (argc < 2) {
		const struct param_info *param;
		int i;

		fprintf(stderr,
			"Usage: %s get [<param>] - print params\n"
			"\t%s set [<param>> <value>]\n"
			"   Available params are: (all time values are in us)",
			argv[0], argv[0]);

		param = keyconfig_params;
		for (i = 0; i < ARRAY_SIZE(keyconfig_params); i++, param++) {
			fprintf(stderr, "%-12s   %s\n", param->name,
				param->name);
		}
		return -1;
	}

	/* Get the command */
	if (0 == strcmp(argv[1], "get")) {
		cmd = EC_CMD_MKBP_GET_CONFIG;
	} else if (0 == strcmp(argv[1], "set")) {
		cmd = EC_CMD_MKBP_SET_CONFIG;
	} else {
		fprintf(stderr, "Invalid command '%s\n", argv[1]);
		return -1;
	}

	switch (cmd) {
	case EC_CMD_MKBP_GET_CONFIG:
		/* Read the existing config */
		rv = ec_command(cmd, 0, NULL, 0, &req, sizeof(req));
		if (rv < 0)
			return rv;
		show_fields(&req.config, argc - 2, argv + 2);
		break;
	}

	return 0;
}

static void cmd_memory_dump_usage(const char *command_name)
{
	fprintf(stderr,
		"Usage: %s [<address> [<size>]]\n"
		"  Prints the memory available for dumping in hexdump cononical format.\n"
		"  <address> is a 32-bit address offset. Defaults to 0x0.\n"
		"  <size> is the number of bytes to print after <address>."
		" Defaults to end of RAM.\n"
		"Usage: %s info\n"
		"  Prints metadata about the memory available for dumping\n",
		command_name, command_name);
}

static int cmd_memory_dump(int argc, char *argv[])
{
	int rv;
	char *e;
	bool just_info;
	int response_max;
	const char *command_name = argv[0];
	void *read_mem_response = NULL;
	uint32_t requested_address_start = 0;
	uint32_t requested_address_end = UINT32_MAX;
	/* Simple local structs for storing a memory dump */
	struct mem_segment {
		uint32_t addr_start;
		uint32_t addr_end;
		uint32_t size;
		uint8_t *mem;
		struct mem_segment *next;
	};
	uint16_t entry_count;
	struct mem_segment *segments = NULL;
	/* The real root is root.next, all other root fields are unused */
	struct mem_segment root;
	struct mem_segment *seg;
	struct ec_response_memory_dump_get_metadata metadata_response;
	struct ec_response_get_protocol_info protocol_info_response;

	if (argc > 3 || (argc == 2 && strcmp(argv[1], "help") == 0)) {
		cmd_memory_dump_usage(command_name);
		return -1;
	}
	if (argc == 2 && strcmp(argv[1], "info") == 0) {
		just_info = true;
	}
	if (argc >= 2 && !just_info) {
		/* Parse requested address argument */
		requested_address_start = strtoul(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad argument '%s'\n", argv[1]);
			cmd_memory_dump_usage(command_name);
			return -1;
		}
	}
	if (argc == 3 && !just_info) {
		/* Parse requested size argument */
		uint32_t requested_size = strtoul(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad argument '%s'\n", argv[2]);
			cmd_memory_dump_usage(command_name);
			return -1;
		}
		/* Cap max address at UINT32_MAX */
		requested_address_end =
			MIN((uint64_t)requested_address_start + requested_size,
			    (uint64_t)UINT32_MAX);
	}

	rv = ec_command(EC_CMD_GET_PROTOCOL_INFO, 0, NULL, 0,
			&protocol_info_response,
			sizeof(protocol_info_response));
	if (rv < 0) {
		fprintf(stderr, "Protocol info unavailable.\n");
		goto cmd_memory_dump_cleanup;
	}
	response_max = protocol_info_response.max_response_packet_size;
	read_mem_response = malloc(response_max);

	/* Fetch memory dump metadata */
	rv = ec_command(EC_CMD_MEMORY_DUMP_GET_METADATA, 0, NULL, 0,
			&metadata_response, sizeof(metadata_response));
	if (rv < 0) {
		fprintf(stderr, "Failed to get memory dump metadata.\n");
		goto cmd_memory_dump_cleanup;
	}
	entry_count = metadata_response.memory_dump_entry_count;
	if (entry_count == 0) {
		fprintf(stderr, "Memory dump is empty.\n");
		rv = -1;
		goto cmd_memory_dump_cleanup;
	}
	segments = (struct mem_segment *)malloc(sizeof(struct mem_segment) *
						entry_count);
	if (segments == NULL) {
		fprintf(stderr, "malloc failed\n");
		rv = -1;
		goto cmd_memory_dump_cleanup;
	}

	/* Fetch all memory segments */
	for (uint16_t entry_index = 0; entry_index < entry_count;
	     entry_index++) {
		seg = &segments[entry_index];
		struct ec_params_memory_dump_get_entry_info entry_info_params = {
			.memory_dump_entry_index = entry_index
		};
		struct ec_response_memory_dump_get_entry_info
			entry_info_response;

		rv = ec_command(EC_CMD_MEMORY_DUMP_GET_ENTRY_INFO, 0,
				&entry_info_params, sizeof(entry_info_params),
				&entry_info_response,
				sizeof(entry_info_response));
		if (rv < 0) {
			fprintf(stderr,
				"Failed to get memory dump info for entry %d.\n",
				entry_index);
			goto cmd_memory_dump_cleanup;
		}

		uint32_t entry_address_end =
			entry_info_response.address + entry_info_response.size;

		/* Check if entry is even in bounds of the requested range */
		if (entry_info_response.address >= requested_address_end ||
		    entry_address_end <= requested_address_start)
			continue;

		/* Clip memory segment boundaries based on requested range */
		seg->addr_start = MAX(entry_info_response.address,
				      requested_address_start);
		seg->addr_end = MIN(entry_address_end, requested_address_end);
		if (seg->addr_end - seg->addr_start <= 0)
			continue;
		seg->size = seg->addr_end - seg->addr_start;

		if (just_info) {
			printf("%-3d: %x-%x (%d bytes)\n", entry_index,
			       seg->addr_start, seg->addr_end, seg->size);
			continue;
		}

		seg->mem = (uint8_t *)malloc(seg->size);
		if (seg->mem == NULL) {
			fprintf(stderr, "malloc failed\n");
			rv = -1;
			goto cmd_memory_dump_cleanup;
		}

		/* Keep fetching until entire segment is copied */
		uint32_t offset = 0;
		while (offset < seg->size) {
			struct ec_params_memory_dump_read_memory
				read_mem_params = {
					.memory_dump_entry_index = entry_index,
					.address = seg->addr_start + offset,
					.size = seg->size - offset,
				};

			rv = ec_command(EC_CMD_MEMORY_DUMP_READ_MEMORY, 0,
					&read_mem_params,
					sizeof(read_mem_params),
					read_mem_response, response_max);
			if (rv <= 0) {
				fprintf(stderr,
					"Failed to read memory at %x.\n",
					read_mem_params.address);
				rv = -1;
				goto cmd_memory_dump_cleanup;
			}

			if (!memcpy(seg->mem + offset, read_mem_response, rv)) {
				fprintf(stderr, "memcpy failed\n");
				rv = -1;
				goto cmd_memory_dump_cleanup;
			}

			offset += rv;
		};

		/* Sort segments in ascending order of starting address */
		struct mem_segment *current = &root;
		for (int i = 0; current->next && i < entry_count; i++) {
			if (seg->addr_start < current->next->addr_start) {
				/* Insert segment before current->next */
				seg->next = current->next;
				current->next = seg;
				break;
			}
			current = current->next;
		}
		current->next = seg;
	}

	if (just_info) {
		rv = 0;
		goto cmd_memory_dump_cleanup;
	}

	/* Merge overlapping or touching segments */
	seg = root.next;
	for (int i = 0; seg && seg->next && i < entry_count; i++) {
		if (seg->addr_end < seg->next->addr_start) {
			/* No overlap */
			seg = seg->next;
			continue;
		}
		uint32_t overlap = seg->addr_end - seg->next->addr_start;
		uint32_t new_size = seg->size + seg->next->size - overlap;
		if (new_size != seg->next->addr_end - seg->addr_start) {
			fprintf(stderr, "Segment size is not aligned\n");
			rv = -1;
			goto cmd_memory_dump_cleanup;
		}
		seg->mem = (uint8_t *)realloc(seg->mem, new_size);
		if (seg->mem == NULL) {
			fprintf(stderr, "realloc failed\n");
			rv = -1;
			goto cmd_memory_dump_cleanup;
		}
		if (!memcpy(seg->mem + seg->size, seg->next->mem + overlap,
			    seg->next->size - overlap)) {
			fprintf(stderr, "Merging segments failed\n");
			rv = -1;
			goto cmd_memory_dump_cleanup;
		}
		seg->addr_end = seg->next->addr_end;
		seg->size = new_size;
		seg->next = seg->next->next;
	}

	/* Print dump in hexdump cononical format */
	seg = root.next;
	for (int i = 0; seg && i < entry_count; i++) {
		hexdump_canonical(seg->mem, seg->size, seg->addr_start);
		/* Extra newline to delinate segments */
		printf("\n");
		seg = seg->next;
	}
	rv = 0;
cmd_memory_dump_cleanup:
	free(read_mem_response);
	if (segments) {
		for (int i = 0; i < entry_count; i++)
			free(segments[i].mem);
		free(segments);
	}
	return rv;
}

static const char *const mkbp_button_strings[] = {
	[EC_MKBP_POWER_BUTTON] = "Power",
	[EC_MKBP_VOL_UP] = "Volume up",
	[EC_MKBP_VOL_DOWN] = "Volume down",
	[EC_MKBP_RECOVERY] = "Recovery",
};

static const char *const mkbp_switch_strings[] = {
	[EC_MKBP_LID_OPEN] = "Lid open",
	[EC_MKBP_TABLET_MODE] = "Tablet mode",
	[EC_MKBP_BASE_ATTACHED] = "Base attached",
};

static int cmd_mkbp_get(int argc, char *argv[])
{
	struct ec_params_mkbp_info p;
	union ec_response_get_next_data r;
	int rv;
	int i;
	uint32_t supported;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <buttons|switches>\n", argv[0]);
		return -1;
	}

	if (strncmp(argv[1], "button", 6) == 0) {
		p.event_type = EC_MKBP_EVENT_BUTTON;
	} else if (strncmp(argv[1], "switch", 6) == 0) {
		p.event_type = EC_MKBP_EVENT_SWITCH;
	} else {
		fprintf(stderr, "Invalid param: '%s'\n", argv[1]);
		return -1;
	}

	p.info_type = EC_MKBP_INFO_SUPPORTED;
	rv = ec_command(EC_CMD_MKBP_INFO, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;
	if (p.event_type == EC_MKBP_EVENT_BUTTON)
		supported = r.buttons;
	else if (p.event_type == EC_MKBP_EVENT_SWITCH)
		supported = r.switches;
	else
		return -1;

	p.info_type = EC_MKBP_INFO_CURRENT;
	rv = ec_command(EC_CMD_MKBP_INFO, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;

	if (p.event_type == EC_MKBP_EVENT_BUTTON) {
		printf("MKBP buttons state: 0x%04x (supported: 0x%04x)\n",
		       r.buttons, supported);
		for (i = 0; i < ARRAY_SIZE(mkbp_button_strings); i++) {
			if (supported & BIT(i) && mkbp_button_strings[i]) {
				printf("%s: %s\n", mkbp_button_strings[i],
				       r.buttons & BIT(i) ? "ON" : "OFF");
				supported &= ~BIT(i);
			}
		}
		if (supported)
			printf("Unknown buttons: 0x%04x\n", supported);
	} else if (p.event_type == EC_MKBP_EVENT_SWITCH) {
		printf("MKBP switches state: 0x%04x (supported: 0x%04x)\n",
		       r.switches, supported);
		for (i = 0; i < ARRAY_SIZE(mkbp_switch_strings); i++) {
			if (supported & BIT(i) && mkbp_switch_strings[i]) {
				printf("%s: %s\n", mkbp_switch_strings[i],
				       r.switches & BIT(i) ? "ON" : "OFF");
				supported &= ~BIT(i);
			}
		}
		if (supported)
			printf("Unknown switches: 0x%04x\n", supported);
	}

	return 0;
}

static int cmd_mkbp_wake_mask(int argc, char *argv[])
{
	struct ec_params_mkbp_event_wake_mask p;
	struct ec_response_mkbp_event_wake_mask r;
	int rv;

	if (argc < 3) {
		fprintf(stderr,
			"Usage: %s get <event|hostevent>\n"
			"\t%s set <event|hostevent> <mask>\n",
			argv[0], argv[0]);
		return -1;
	}

	/* Determine if the user want to get or set the wake mask. */
	if (strncmp(argv[1], "get", 3) == 0) {
		p.action = GET_WAKE_MASK;
	} else if (strncmp(argv[1], "set", 3) == 0) {
		p.action = SET_WAKE_MASK;
	} else {
		fprintf(stderr, "Invalid param: '%s'\n", argv[1]);
		return -1;
	}

	/* Determine which mask is of interest. */
	if (strncmp(argv[2], "event", 5) == 0) {
		p.mask_type = EC_MKBP_EVENT_WAKE_MASK;
	} else if (strncmp(argv[2], "hostevent", 9) == 0) {
		p.mask_type = EC_MKBP_HOST_EVENT_WAKE_MASK;
	} else {
		fprintf(stderr, "Invalid param: '%s'\n", argv[2]);
		return -1;
	}

	if (p.action == SET_WAKE_MASK) {
		char *e;

		if (argc < 4) {
			fprintf(stderr, "Missing mask value!");
			return -1;
		}

		p.new_wake_mask = strtol(argv[3], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad mask: '%s'", argv[1]);
			return -1;
		}
	}

	rv = ec_command(EC_CMD_MKBP_WAKE_MASK, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0) {
		if (rv == -EECRESULT - EC_RES_INVALID_PARAM) {
			fprintf(stderr,
				"Unknown mask, or mask is not in use.  "
				"You may need to enable the "
				"CONFIG_MKBP_%s_WAKEUP_MASK option in the EC.\n",
				p.mask_type == EC_MKBP_EVENT_WAKE_MASK ?
					"EVENT" :
					"HOSTEVENT");
		}
		return rv;
	}

	if (p.action == GET_WAKE_MASK)
		printf("MBKP %s wake mask: 0x%08x\n", argv[2], r.wake_mask);
	else if (p.action == SET_WAKE_MASK)
		printf("MKBP %s wake mask set.\n", argv[2]);

	return 0;
}

/* Index is already checked. argv[0] is first param value */
static int cmd_tmp006cal_v0(int idx, int argc, char *argv[])
{
	struct ec_params_tmp006_get_calibration pg;
	struct ec_response_tmp006_get_calibration_v0 rg;
	struct ec_params_tmp006_set_calibration_v0 ps;
	float val;
	char *e;
	int i, rv;

	/* Get current values */
	pg.index = idx;
	rv = ec_command(EC_CMD_TMP006_GET_CALIBRATION, 0, &pg, sizeof(pg), &rg,
			sizeof(rg));
	if (rv < 0)
		return rv;

	if (!argc) {
		/* If no new values are given, just print what we have */
		printf("S0: %e\n", rg.s0);
		printf("b0: %e\n", rg.b0);
		printf("b1: %e\n", rg.b1);
		printf("b2: %e\n", rg.b2);
		return EC_SUCCESS;
	}

	/* Prepare to reuse the current values */
	memset(&ps, 0, sizeof(ps));
	ps.index = idx;
	ps.s0 = rg.s0;
	ps.b0 = rg.b0;
	ps.b1 = rg.b1;
	ps.b2 = rg.b2;

	/* Parse up to four args, skipping any that are just "-" */
	for (i = 0; i < argc && i < 4; i++) {
		if (!strcmp(argv[i], "-"))
			continue;
		val = strtod(argv[i], &e);
		if (e && *e) {
			fprintf(stderr,
				"Bad arg \"%s\". Use \"-\" to skip a param.\n",
				argv[i]);
			return -1;
		}
		switch (i) {
		case 0:
			ps.s0 = val;
			break;
		case 1:
			ps.b0 = val;
			break;
		case 2:
			ps.b1 = val;
			break;
		case 3:
			ps.b2 = val;
			break;
		}
	}

	/* Set 'em */
	return ec_command(EC_CMD_TMP006_SET_CALIBRATION, 0, &ps, sizeof(ps),
			  NULL, 0);
}

/* Index is already checked. argv[0] is first param value */
static int cmd_tmp006cal_v1(int idx, int argc, char *argv[])
{
	struct ec_params_tmp006_get_calibration pg;
	struct ec_response_tmp006_get_calibration_v1 *rg =
		(struct ec_response_tmp006_get_calibration_v1 *)(ec_inbuf);
	struct ec_params_tmp006_set_calibration_v1 *ps =
		(struct ec_params_tmp006_set_calibration_v1 *)(ec_outbuf);
	float val;
	char *e;
	int i, rv, cmdsize;

	/* Algorithm 1 parameter names */
	static const char *const alg1_pname[] = {
		"s0", "a1", "a2", "b0", "b1", "b2",
		"c2", "d0", "d1", "ds", "e0", "e1",
	};

	/* Get current values */
	pg.index = idx;
	rv = ec_command(EC_CMD_TMP006_GET_CALIBRATION, 1, &pg, sizeof(pg), rg,
			ec_max_insize);
	if (rv < 0)
		return rv;

	if (!argc) {
		/* If no new values are given, just print what we have */
		printf("algorithm:  %d\n", rg->algorithm);
		printf("params:\n");
		/* We only know about alg 1 at the moment */
		if (rg->algorithm == 1)
			for (i = 0; i < rg->num_params; i++)
				printf("  %s  %e\n", alg1_pname[i], rg->val[i]);
		else
			for (i = 0; i < rg->num_params; i++)
				printf("  param%d  %e\n", i, rg->val[i]);
		return EC_SUCCESS;
	}

	/* Prepare to reuse the current values */
	memset(ps, 0, ec_max_outsize);
	ps->index = idx;
	ps->algorithm = rg->algorithm;
	ps->num_params = rg->num_params;
	for (i = 0; i < rg->num_params; i++)
		ps->val[i] = rg->val[i];

	/* Parse the args, skipping any that are just "-" */
	for (i = 0; i < argc && i < rg->num_params; i++) {
		if (!strcmp(argv[i], "-"))
			continue;
		val = strtod(argv[i], &e);
		if (e && *e) {
			fprintf(stderr,
				"Bad arg \"%s\". Use \"-\" to skip a param.\n",
				argv[i]);
			return -1;
		}
		ps->val[i] = val;
	}

	/* Set 'em */
	cmdsize = sizeof(*ps) + ps->num_params * sizeof(ps->val[0]);
	return ec_command(EC_CMD_TMP006_SET_CALIBRATION, 1, ps, cmdsize, NULL,
			  0);
}

int cmd_tmp006cal(int argc, char *argv[])
{
	char *e;
	int idx;

	if (argc < 2) {
		fprintf(stderr, "Must specify tmp006 index.\n");
		return -1;
	}

	idx = strtol(argv[1], &e, 0);
	if ((e && *e) || idx < 0 || idx > 255) {
		fprintf(stderr, "Bad index.\n");
		return -1;
	}

	/* Pass just the params (if any) to the helper function */
	argc -= 2;
	argv += 2;

	if (ec_cmd_version_supported(EC_CMD_TMP006_GET_CALIBRATION, 1))
		return cmd_tmp006cal_v1(idx, argc, argv);

	if (ec_cmd_version_supported(EC_CMD_TMP006_GET_CALIBRATION, 0))
		return cmd_tmp006cal_v0(idx, argc, argv);

	printf("The EC is being stupid\n");
	return -1;
}

int cmd_tmp006raw(int argc, char *argv[])
{
	struct ec_params_tmp006_get_raw p;
	struct ec_response_tmp006_get_raw r;
	char *e;
	int idx;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Must specify tmp006 index.\n");
		return -1;
	}

	idx = strtol(argv[1], &e, 0);
	if ((e && *e) || idx < 0 || idx > 255) {
		fprintf(stderr, "Bad index.\n");
		return -1;
	}

	p.index = idx;

	rv = ec_command(EC_CMD_TMP006_GET_RAW, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("T: %d.%02d K\n", r.t / 100, r.t % 100);
	printf("V: %d nV\n", r.v);
	return EC_SUCCESS;
}

static int cmd_hang_detect(int argc, char *argv[])
{
	struct ec_params_hang_detect req;
	struct ec_response_hang_detect resp;
	int rv;
	char *e;

	memset(&req, 0, sizeof(req));

	if (argc == 2 && !strcasecmp(argv[1], "reload")) {
		req.command = EC_HANG_DETECT_CMD_RELOAD;
		return ec_command(EC_CMD_HANG_DETECT, 0, &req, sizeof(req),
				  NULL, 0);
	}

	if (argc == 2 && !strcasecmp(argv[1], "cancel")) {
		req.command = EC_HANG_DETECT_CMD_CANCEL;
		return ec_command(EC_CMD_HANG_DETECT, 0, &req, sizeof(req),
				  NULL, 0);
	}

	if (argc == 3 && !strcasecmp(argv[1], "set_timeout")) {
		req.command = EC_HANG_DETECT_CMD_SET_TIMEOUT;

		req.reboot_timeout_sec = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad reboot timeout.\n");
			return -1;
		}

		rv = ec_command(EC_CMD_HANG_DETECT, 0, &req, sizeof(req), NULL,
				0);
		if (rv < 0)
			printf("Couldn't set reboot timeout (rv=%d)\n", rv);
		else
			printf("reboot_timeout=%d s\n", req.reboot_timeout_sec);
		return rv;
	}

	if (argc == 2 && !strcasecmp(argv[1], "get_status")) {
		req.command = EC_HANG_DETECT_CMD_GET_STATUS;
		rv = ec_command(EC_CMD_HANG_DETECT, 0, &req, sizeof(req), &resp,
				sizeof(resp));

		if (rv < 0)
			printf("Couldn't get boot status (rv=%d)\n", rv);
		else
			printf("boot status=%d\n", resp.status);
		return rv;
	}

	if (argc == 2 && !strcasecmp(argv[1], "clear_status")) {
		req.command = EC_HANG_DETECT_CMD_CLEAR_STATUS;
		return ec_command(EC_CMD_HANG_DETECT, 0, &req, sizeof(req),
				  NULL, 0);
	}

	fprintf(stderr,
		"args: reload|cancel|set_timeout <reboot_sec>|get_status|clear_status\n");
	return -1;
}

enum port_80_event {
	PORT_80_EVENT_RESUME = 0x1001, /* S3->S0 transition */
	PORT_80_EVENT_RESET = 0x1002, /* RESET transition */
};

int cmd_port80_read(int argc, char *argv[])
{
	struct ec_params_port80_read p;
	int cmdver = 1, rv;
	int i, head, tail;
	uint16_t *history;
	uint32_t writes, history_size;
	struct ec_response_port80_read rsp;
	int printed = 0;

	if (!ec_cmd_version_supported(EC_CMD_PORT80_READ, cmdver)) {
		/* fall back to last boot */
		struct ec_response_port80_last_boot r;
		rv = ec_command(EC_CMD_PORT80_LAST_BOOT, 0, NULL, 0, &r,
				sizeof(r));
		fprintf(stderr, "Last boot %2x\n", r.code);
		printf("done.\n");
		return 0;
	}

	/* read writes and history_size */
	p.subcmd = EC_PORT80_GET_INFO;
	rv = ec_command(EC_CMD_PORT80_READ, cmdver, &p, sizeof(p), &rsp,
			sizeof(rsp));
	if (rv < 0) {
		fprintf(stderr, "Read error at writes\n");
		return rv;
	}
	writes = rsp.get_info.writes;
	history_size = rsp.get_info.history_size;

	history = (uint16_t *)(malloc(history_size * sizeof(uint16_t)));
	if (!history) {
		fprintf(stderr, "Unable to allocate buffer.\n");
		return -1;
	}
	/* As the history buffer is quite large, we read data in chunks, with
	    size in bytes of EC_PORT80_SIZE_MAX in each chunk.
	    Incrementing offset until all history buffer has been read. To
	    simplify the design, chose HISTORY_LEN is always multiple of
	    EC_PORT80_SIZE_MAX.

	    offset: entry offset from the beginning of history buffer.
	    num_entries: number of entries requested.
	*/
	p.subcmd = EC_PORT80_READ_BUFFER;
	for (i = 0; i < history_size; i += EC_PORT80_SIZE_MAX) {
		p.read_buffer.offset = i;
		p.read_buffer.num_entries = EC_PORT80_SIZE_MAX;
		rv = ec_command(EC_CMD_PORT80_READ, cmdver, &p, sizeof(p), &rsp,
				sizeof(rsp));
		if (rv < 0) {
			fprintf(stderr, "Read error at offset %d\n", i);
			free(history);
			return rv;
		}
		memcpy((void *)(history + i), rsp.data.codes,
		       EC_PORT80_SIZE_MAX * sizeof(uint16_t));
	}

	head = writes;
	if (head > history_size)
		tail = head - history_size;
	else
		tail = 0;

	fprintf(stderr, "Port 80 writes");
	for (i = tail; i < head; i++) {
		int e = history[i % history_size];
		switch (e) {
		case PORT_80_EVENT_RESUME:
			fprintf(stderr, "\n(S3->S0)");
			printed = 0;
			break;
		case PORT_80_EVENT_RESET:
			fprintf(stderr, "\n(RESET)");
			printed = 0;
			break;
		default:
			if (!(printed++ % 20))
				fprintf(stderr, "\n ");
			fprintf(stderr, " %02x", e);
		}
	}
	fprintf(stderr, " <--new\n");

	free(history);
	printf("done.\n");
	return 0;
}

int cmd_force_lid_open(int argc, char *argv[])
{
	struct ec_params_force_lid_open p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <0|1>\n", argv[0]);
		return -1;
	}
	p.enabled = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad value.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_FORCE_LID_OPEN, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;
	printf("Success.\n");
	return 0;
}

int cmd_charge_port_override(int argc, char *argv[])
{
	struct ec_params_charge_port_override p;
	char *e;
	int rv;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <port# | dontcharge | off>\n",
			argv[0]);
		return -1;
	}

	if (!strcasecmp(argv[1], "dontcharge"))
		p.override_port = OVERRIDE_DONT_CHARGE;
	else if (!strcasecmp(argv[1], "off"))
		p.override_port = OVERRIDE_OFF;
	else {
		p.override_port = strtol(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad parameter.\n");
			return -1;
		}
	}

	rv = ec_command(EC_CMD_PD_CHARGE_PORT_OVERRIDE, 0, &p, sizeof(p), NULL,
			0);
	if (rv < 0)
		return rv;

	printf("Override port set to %d\n", p.override_port);
	return 0;
}

static void cmd_pchg_help(char *cmd)
{
	fprintf(stderr,
		"  Usage1: %s\n"
		"          Print the number of ports.\n"
		"\n"
		"  Usage2: %s <port>\n"
		"          Print the status of <port>.\n"
		"\n"
		"  Usage3: %s <port> reset [mode]\n"
		"          Reset <port> to [mode]. [mode]: 'normal'.\n"
		"\n"
		"  Usage4: %s <port> update <version> <addr1> <file1> <addr2> <file2> ...\n"
		"          Update firmware of <port>.\n"
		"\n"
		"  Usage5: %s <port> passthru <on/off> ...\n"
		"          Enable passthru mode for <port>.\n",
		cmd, cmd, cmd, cmd, cmd);
}

static int cmd_pchg_info(const struct ec_response_pchg *res)
{
	static const char *const pchg_state_text[] = EC_PCHG_STATE_TEXT;

	BUILD_ASSERT(ARRAY_SIZE(pchg_state_text) == PCHG_STATE_COUNT);

	printf("State: %s (%d)\n",
	       res->state < PCHG_STATE_COUNT ? pchg_state_text[res->state] :
					       "UNDEF",
	       res->state);
	printf("Battery: %u%%\n", res->battery_percentage);
	printf("Errors: 0x%x\n", res->error);
	printf("FW Version: 0x%x\n", res->fw_version);
	printf("Dropped events: %u\n", res->dropped_event_count);
	return 0;
}

static int cmd_pchg_wait_event(int port, uint32_t expected)
{
	struct ec_response_get_next_event_v1 event;
	const long timeout = 5000;
	uint32_t *e = &event.data.host_event;
	int rv;

	rv = wait_event(EC_MKBP_EVENT_PCHG, &event, sizeof(event), timeout);
	if (rv < 0)
		return rv;

	if (EC_MKBP_PCHG_EVENT_TO_PORT(*e) == port) {
		if (*e & EC_MKBP_PCHG_UPDATE_ERROR) {
			fprintf(stderr, "\nReceived update error\n");
			return -1;
		}
		if (*e & expected)
			return 0;
	}

	fprintf(stderr, "\nExpected event=0x%x but received 0x%x\n", expected,
		*e);
	return -1;
}

static int cmd_pchg_update_open(int port, uint32_t version,
				uint32_t *block_size, uint32_t *crc)
{
	struct ec_params_pchg_update *pu =
		(struct ec_params_pchg_update *)(ec_outbuf);
	struct ec_response_pchg_update *r =
		(struct ec_response_pchg_update *)(ec_inbuf);
	struct ec_params_pchg p;
	struct ec_response_pchg_v2 rv2;
	int rv;

	/* Open session. */
	pu->port = port;
	pu->cmd = EC_PCHG_UPDATE_CMD_OPEN;
	pu->version = version;
	rv = ec_command(EC_CMD_PCHG_UPDATE, 0, pu, sizeof(*pu), r, sizeof(*r));
	if (rv < 0) {
		fprintf(stderr, "\nFailed to open update session: %d\n", rv);
		return rv;
	}

	if (r->block_size + sizeof(*pu) > ec_max_outsize) {
		fprintf(stderr, "\nBlock size (%d) is too large.\n",
			r->block_size);
		return -1;
	}

	rv = cmd_pchg_wait_event(port, EC_MKBP_PCHG_DEVICE_EVENT);
	if (rv)
		return rv;

	p.port = port;
	rv = ec_command(EC_CMD_PCHG, 2, &p, sizeof(p), &rv2, sizeof(rv2));
	if (rv == -EC_RES_INVALID_VERSION - EECRESULT)
		/* We can use v2 because it's a superset of v1. */
		rv = ec_command(EC_CMD_PCHG, 1, &p, sizeof(p), &rv2,
				sizeof(struct ec_response_pchg));
	if (rv < 0) {
		fprintf(stderr, "EC_CMD_PCHG failed: %d\n", rv);
		return rv;
	}
	if (rv2.state != PCHG_STATE_DOWNLOAD) {
		fprintf(stderr, "Failed to reset to download mode: %d\n", rv);
		return -1;
	}

	rv = cmd_pchg_wait_event(port, EC_MKBP_PCHG_UPDATE_OPENED);
	if (rv)
		return rv;

	printf("Opened update session (port=%d ver=0x%x bsize=%d):\n", port,
	       version, r->block_size);

	*block_size = r->block_size;
	crc32_ctx_init(crc);

	return 0;
}

static int cmd_pchg_update_write(int port, uint32_t address,
				 const char *filename, uint32_t block_size,
				 uint32_t *crc)
{
	struct ec_params_pchg_update *p =
		(struct ec_params_pchg_update *)(ec_outbuf);
	FILE *fp;
	size_t len, total;
	int progress = 0;
	int rv;

	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "\nCan't open %s: %s\n", filename,
			strerror(errno));
		return -1;
	}

	fseek(fp, 0L, SEEK_END);
	total = ftell(fp);
	rewind(fp);
	printf("Writing %s (%zu bytes).\n", filename, total);

	p->cmd = EC_PCHG_UPDATE_CMD_WRITE;
	p->addr = address;

	/* Write firmware in blocks. */
	len = fread(p->data, 1, block_size, fp);
	while (len > 0) {
		int previous_progress = progress;
		int i;

		crc32_ctx_hash(crc, p->data, len);
		p->size = len;
		rv = ec_command(EC_CMD_PCHG_UPDATE, 0, p, sizeof(*p) + len,
				NULL, 0);
		if (rv < 0) {
			fprintf(stderr, "\nFailed to write FW: %d\n", rv);
			fclose(fp);
			return rv;
		}

		rv = cmd_pchg_wait_event(port, EC_MKBP_PCHG_WRITE_COMPLETE);
		if (rv)
			return rv;

		p->addr += len;
		progress = (p->addr - address) * 100 / total;
		for (i = 0; i < progress - previous_progress; i++) {
			printf("*");
			fflush(stdout);
		}

		len = fread(p->data, 1, block_size, fp);
	}

	printf("\n");
	fclose(fp);

	return 0;
}

static int cmd_pchg_update_close(int port, uint32_t *crc)
{
	struct ec_params_pchg_update *p =
		(struct ec_params_pchg_update *)(ec_outbuf);
	int rv;

	p->cmd = EC_PCHG_UPDATE_CMD_CLOSE;
	p->crc32 = crc32_ctx_result(crc);
	rv = ec_command(EC_CMD_PCHG_UPDATE, 0, p, sizeof(*p), NULL, 0);

	if (rv < 0) {
		fprintf(stderr, "\nFailed to close update session: %d\n", rv);
		return rv;
	}

	rv = cmd_pchg_wait_event(port, EC_MKBP_PCHG_UPDATE_CLOSED);
	if (rv)
		return rv;

	printf("Firmware was updated successfully (CRC32=0x%x).\n", p->crc32);

	return 0;
}

static int cmd_pchg(int argc, char *argv[])
{
	const size_t max_input_files = 8;
	int port, port_count;
	struct ec_response_pchg_count rcnt;
	struct ec_params_pchg p;
	struct ec_response_pchg r;
	char *e;
	int rv;

	rv = ec_command(EC_CMD_PCHG_COUNT, 0, NULL, 0, &rcnt, sizeof(rcnt));
	if (rv < 0) {
		fprintf(stderr, "\nFailed to get port count: %d\n", rv);
		return rv;
	}
	port_count = rcnt.port_count;

	if (argc == 1) {
		/* Usage.1 */
		printf("%d\n", port_count);
		return 0;
	}

	port = strtol(argv[1], &e, 0);
	if ((e && *e) || port >= port_count) {
		fprintf(stderr, "\nBad port index: %s\n", argv[1]);
		cmd_pchg_help(argv[0]);
		return -1;
	}

	p.port = port;
	rv = ec_command(EC_CMD_PCHG, 1, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0) {
		fprintf(stderr, "\nError code: %d\n", rv);
		return rv;
	}

	if (argc == 2) {
		/* Usage.2 */
		return cmd_pchg_info(&r);
	} else if (argc >= 3 && !strcmp(argv[2], "reset")) {
		/* Usage.3 */
		struct ec_params_pchg_update u;

		u.port = port;

		if (argc == 3) {
			u.cmd = EC_PCHG_UPDATE_CMD_RESET;
		} else if (argc == 4 && !strcmp(argv[3], "normal")) {
			u.cmd = EC_PCHG_UPDATE_CMD_RESET_TO_NORMAL;
		} else {
			fprintf(stderr, "\nInvalid mode: '%s'\n", argv[3]);
			return -1;
		}

		rv = ec_command(EC_CMD_PCHG_UPDATE, 0, &u, sizeof(u), NULL, 0);
		if (rv < 0) {
			fprintf(stderr, "\nFailed to reset port %d: %d\n", port,
				rv);
			cmd_pchg_help(argv[0]);
			return rv;
		}
		printf("Reset port %d complete.\n", port);
		return 0;
	} else if (argc >= 6 && !strcmp(argv[2], "update")) {
		/*
		 * Usage.4:
		 * argv[3]: <version>
		 * argv[4]: <addr1>
		 * argv[5]: <file1>
		 * argv[6]: <addr2>
		 * argv[7]: <file2>
		 * ...
		 */
		uint32_t address, version;
		uint32_t block_size = 0;
		uint32_t crc;
		int i;

		if (argc > 4 + max_input_files * 2) {
			fprintf(stderr, "\nToo many input files.\n");
			return -1;
		}

		version = strtol(argv[3], &e, 0);
		if (e && *e) {
			fprintf(stderr, "\nBad version: %s.\n", argv[3]);
			cmd_pchg_help(argv[0]);
			return -1;
		}

		rv = cmd_pchg_update_open(port, version, &block_size, &crc);
		if (rv < 0 || block_size == 0) {
			fprintf(stderr, "\nFailed to open update session: %d\n",
				rv);
			return -1;
		}

		/* Write files one by one. */
		for (i = 4; i + 1 < argc; i += 2) {
			address = strtol(argv[i], &e, 0);
			if (e && *e) {
				fprintf(stderr, "\nBad address: %s\n", argv[i]);
				cmd_pchg_help(argv[0]);
				return -1;
			}
			rv = cmd_pchg_update_write(port, address, argv[i + 1],
						   block_size, &crc);
			if (rv < 0) {
				fprintf(stderr,
					"\nFailed to write file '%s': %d",
					argv[i + i], rv);
				return -1;
			}
		}

		rv = cmd_pchg_update_close(port, &crc);
		if (rv < 0) {
			fprintf(stderr, "\nFailed to close update session: %d",
				rv);
			return -1;
		}

		return 0;
	} else if (argc >= 4 && !strcmp(argv[2], "passthru")) {
		/*
		 * Usage 5
		 */
		struct ec_params_pchg_update u;
		int onoff;

		if (!parse_bool(argv[3], &onoff)) {
			fprintf(stderr, "\nInvalid arg: '%s'\n", argv[3]);
			return -1;
		}

		u.port = port;
		u.cmd = EC_PCHG_UPDATE_CMD_ENABLE_PASSTHRU;

		rv = ec_command(EC_CMD_PCHG_UPDATE, 0, &u, sizeof(u), NULL, 0);
		if (rv < 0) {
			fprintf(stderr, "\nFailed to enable pass-through: %d\n",
				rv);
			return rv;
		}

		printf("Pass-through is %s for port %d\n",
		       onoff ? "enabled" : "disabled", port);
		return 0;
	}

	fprintf(stderr, "Invalid parameter\n\n");
	cmd_pchg_help(argv[0]);

	return -1;
}

int cmd_pd_log(int argc, char *argv[])
{
	union {
		struct ec_response_pd_log r;
		uint32_t words[8]; /* space for the payload */
	} u;
	struct mcdp_info minfo;
	struct ec_response_usb_pd_power_info pinfo;
	int rv;
	unsigned long long milliseconds;
	unsigned int seconds;
	time_t now;
	struct tm ltime;
	char time_str[64];

	while (1) {
		now = time(NULL);
		rv = ec_command(EC_CMD_PD_GET_LOG_ENTRY, 0, NULL, 0, &u,
				sizeof(u));
		if (rv < 0)
			return rv;

		if (u.r.type == PD_EVENT_NO_ENTRY) {
			printf("--- END OF LOG ---\n");
			break;
		}

		/* the timestamp is in 1024th of seconds */
		milliseconds =
			((uint64_t)u.r.timestamp << PD_LOG_TIMESTAMP_SHIFT) /
			1000;
		/* the timestamp is the number of milliseconds in the past */
		seconds = (milliseconds + 999) / 1000;
		milliseconds -= seconds * 1000;
		now -= seconds;
		localtime_r(&now, &ltime);
		strftime(time_str, sizeof(time_str), "%F %T", &ltime);
		printf("%s.%03lld P%d ", time_str, -milliseconds,
		       PD_LOG_PORT(u.r.size_port));
		if (u.r.type == PD_EVENT_MCU_CHARGE) {
			if (u.r.data & CHARGE_FLAGS_OVERRIDE)
				printf("override ");
			if (u.r.data & CHARGE_FLAGS_DELAYED_OVERRIDE)
				printf("pending_override ");
			memcpy(&pinfo.meas, u.r.payload,
			       sizeof(struct usb_chg_measures));
			pinfo.dualrole = !!(u.r.data & CHARGE_FLAGS_DUAL_ROLE);
			pinfo.role = u.r.data & CHARGE_FLAGS_ROLE_MASK;
			pinfo.type = (u.r.data & CHARGE_FLAGS_TYPE_MASK) >>
				     CHARGE_FLAGS_TYPE_SHIFT;
			pinfo.max_power = 0;
			print_pd_power_info(&pinfo);
		} else if (u.r.type == PD_EVENT_MCU_CONNECT) {
			printf("New connection\n");
		} else if (u.r.type == PD_EVENT_MCU_BOARD_CUSTOM) {
			printf("Board-custom event\n");
		} else if (u.r.type == PD_EVENT_ACC_RW_FAIL) {
			printf("RW signature check failed\n");
		} else if (u.r.type == PD_EVENT_PS_FAULT) {
			static const char *const fault_names[] = {
				"---", "OCP", "fast OCP", "OVP", "Discharge"
			};
			const char *fault = u.r.data < ARRAY_SIZE(fault_names) ?
						    fault_names[u.r.data] :
						    "???";
			printf("Power supply fault: %s\n", fault);
		} else if (u.r.type == PD_EVENT_VIDEO_DP_MODE) {
			printf("DP mode %sabled\n",
			       (u.r.data == 1) ? "en" : "dis");
		} else if (u.r.type == PD_EVENT_VIDEO_CODEC) {
			memcpy(&minfo, u.r.payload, sizeof(struct mcdp_info));
			printf("HDMI info: family:%04x chipid:%04x "
			       "irom:%d.%d.%d fw:%d.%d.%d\n",
			       MCDP_FAMILY(minfo.family),
			       MCDP_CHIPID(minfo.chipid), minfo.irom.major,
			       minfo.irom.minor, minfo.irom.build,
			       minfo.fw.major, minfo.fw.minor, minfo.fw.build);
		} else { /* Unknown type */
			int i;
			printf("Event %02x (%04x) [", u.r.type, u.r.data);
			for (i = 0; i < PD_LOG_SIZE(u.r.size_port); i++)
				printf("%02x ", u.r.payload[i]);
			printf("]\n");
		}
	}

	return 0;
}

int cmd_pd_control(int argc, char *argv[])
{
	struct ec_params_pd_control p;
	int rv;

	if (argc < 2) {
		fprintf(stderr, "Missing parameter\n");
		return -1;
	}

	/* Parse command */
	if (!strcmp(argv[1], "reset"))
		p.subcmd = PD_RESET;
	else if (!strcmp(argv[1], "suspend"))
		p.subcmd = PD_SUSPEND;
	else if (!strcmp(argv[1], "resume"))
		p.subcmd = PD_RESUME;
	else if (!strcmp(argv[1], "disable"))
		p.subcmd = PD_CONTROL_DISABLE;
	else if (!strcmp(argv[1], "on") || !strcmp(argv[1], "chip_on"))
		p.subcmd = PD_CHIP_ON;
	else {
		fprintf(stderr, "Unknown command: %s\n", argv[1]);
		return -1;
	}

	if (argc == 2) {
		p.chip = 0;
	} else {
		char *e;
		p.chip = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad port number '%s'.\n", argv[2]);
			return -1;
		}
	}

	rv = ec_command(EC_CMD_PD_CONTROL, 0, &p, sizeof(p), NULL, 0);
	return (rv < 0 ? rv : 0);
}

int cmd_pd_chip_info(int argc, char *argv[])
{
	struct ec_params_pd_chip_info p;
	struct ec_response_pd_chip_info_v1 r;
	char *e;
	int rv;
	int cmdver = 1;

	if (argc < 2 || 3 < argc) {
		fprintf(stderr,
			"Usage: %s <port> [<live>]\n"
			"live parameter can take values 0 or 1\n"
			"0 -> Return hard-coded value for VID/PID and\n"
			"     cached value for Firmware Version\n"
			"1 -> Return live chip value for VID/PID/FW Version\n",
			argv[0]);
		return -1;
	}

	p.port = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port number.\n");
		return -1;
	}

	p.live = 0;
	if (argc == 3) {
		p.live = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "invalid arg \"%s\"\n", argv[2]);
			return -1;
		}
	}

	if (!ec_cmd_version_supported(EC_CMD_PD_CHIP_INFO, cmdver))
		cmdver = 0;

	rv = ec_command(EC_CMD_PD_CHIP_INFO, cmdver, &p, sizeof(p), &r,
			sizeof(r));
	if (rv < 0)
		return rv;

	printf("vendor_id: 0x%x\n", r.vendor_id);
	printf("product_id: 0x%x\n", r.product_id);
	printf("device_id: 0x%x\n", r.device_id);

	if (r.fw_version_number != -1)
		printf("fw_version: 0x%" PRIx64 "\n", r.fw_version_number);
	else
		printf("fw_version: UNSUPPORTED\n");

	if (cmdver >= 1)
		printf("min_req_fw_version: 0x%" PRIx64 "\n",
		       r.min_req_fw_version_number);
	else
		printf("min_req_fw_version: UNSUPPORTED\n");

	return 0;
}

int cmd_pd_write_log(int argc, char *argv[])
{
	struct ec_params_pd_write_log_entry p;
	char *e;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <log_type> <port>\n", argv[0]);
		return -1;
	}

	if (!strcasecmp(argv[1], "charge"))
		p.type = PD_EVENT_MCU_CHARGE;
	else {
		p.type = strtol(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad log_type parameter.\n");
			return -1;
		}
	}

	p.port = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port parameter.\n");
		return -1;
	}

	return ec_command(EC_CMD_PD_WRITE_LOG_ENTRY, 0, &p, sizeof(p), NULL, 0);
}

int cmd_typec_control(int argc, char *argv[])
{
	struct ec_params_typec_control p;
	long conversion_result;
	char *endptr;
	int rv;

	if (argc < 3) {
		fprintf(stderr,
			"Usage: %s <port> <command> [args]\n"
			"  <port> is the type-c port to query\n"
			"  <command> is one of:\n"
			"    0: Exit modes\n"
			"    1: Clear events\n"
			"        args: <event mask>\n"
			"    2: Enter mode\n"
			"        args: <0: DP, 1:TBT, 2:USB4>\n"
			"    3: Set TBT UFP Reply\n"
			"        args: <0: NAK, 1: ACK>\n"
			"    4: Set USB mux mode\n"
			"        args: <mux_index> <mux_mode>\n"
			"        <mux_mode> is one of: dp, dock, usb, tbt,\n"
			"                              usb4, none, safe\n"
			"    5: Enable bist share mode\n"
			"        args: <0: DISABLE, 1: ENABLE>\n"
			"    6: Send VDM REQ\n"
			"        args: <tx_type vdm_hdr [vdo...]>\n"
			"        <tx_type> is 0 - SOP, 1 - SOP', 2 - SOP''\n",
			argv[0]);
		return -1;
	}

	p.port = strtol(argv[1], &endptr, 0);
	if (endptr && *endptr) {
		fprintf(stderr, "Bad port\n");
		return -1;
	}

	p.command = strtol(argv[2], &endptr, 0);
	if (endptr && *endptr) {
		fprintf(stderr, "Bad command\n");
		return -1;
	}

	switch (p.command) {
	case TYPEC_CONTROL_COMMAND_CLEAR_EVENTS:
		if (argc < 4) {
			fprintf(stderr, "Missing event mask\n");
			return -1;
		}

		p.clear_events_mask = strtol(argv[3], &endptr, 0);
		if (endptr && *endptr) {
			fprintf(stderr, "Bad event mask\n");
			return -1;
		}
		break;
	case TYPEC_CONTROL_COMMAND_ENTER_MODE:
		if (argc < 4) {
			fprintf(stderr, "Missing mode\n");
			return -1;
		}

		conversion_result = strtol(argv[3], &endptr, 0);
		if ((endptr && *endptr) || conversion_result > UINT8_MAX ||
		    conversion_result < 0) {
			fprintf(stderr, "Bad mode\n");
			return -1;
		}
		p.mode_to_enter = conversion_result;
		break;
	case TYPEC_CONTROL_COMMAND_TBT_UFP_REPLY:
		if (argc < 4) {
			fprintf(stderr, "Missing reply\n");
			return -1;
		}

		conversion_result = strtol(argv[3], &endptr, 0);
		if ((endptr && *endptr) || conversion_result > UINT8_MAX ||
		    conversion_result < 0) {
			fprintf(stderr, "Bad reply\n");
			return -1;
		}
		p.tbt_ufp_reply = conversion_result;
		break;
	case TYPEC_CONTROL_COMMAND_USB_MUX_SET:
		if (argc < 5) {
			fprintf(stderr, "Missing index or mode\n");
			return -1;
		}

		conversion_result = strtol(argv[3], &endptr, 0);
		if ((endptr && *endptr) || conversion_result > UINT8_MAX ||
		    conversion_result < 0) {
			fprintf(stderr, "Bad index\n");
			return -1;
		}
		p.mux_params.mux_index = conversion_result;
		if (!strcmp(argv[4], "dp")) {
			p.mux_params.mux_flags = USB_PD_MUX_DP_ENABLED;
		} else if (!strcmp(argv[4], "dock")) {
			p.mux_params.mux_flags = USB_PD_MUX_DOCK;
		} else if (!strcmp(argv[4], "usb")) {
			p.mux_params.mux_flags = USB_PD_MUX_USB_ENABLED;
		} else if (!strcmp(argv[4], "tbt")) {
			p.mux_params.mux_flags = USB_PD_MUX_TBT_COMPAT_ENABLED;
		} else if (!strcmp(argv[4], "usb4")) {
			p.mux_params.mux_flags = USB_PD_MUX_USB4_ENABLED;
		} else if (!strcmp(argv[4], "none")) {
			p.mux_params.mux_flags = USB_PD_MUX_NONE;
		} else if (!strcmp(argv[4], "safe")) {
			p.mux_params.mux_flags = USB_PD_MUX_SAFE_MODE;
		} else {
			fprintf(stderr, "Bad mux mode\n");
			return -1;
		}
		break;
	case TYPEC_CONTROL_COMMAND_BIST_SHARE_MODE:
		if (argc < 4) {
			fprintf(stderr, "Missing reply\n");
			return -1;
		}

		conversion_result = strtol(argv[3], &endptr, 0);
		if ((endptr && *endptr) || conversion_result > UINT8_MAX ||
		    conversion_result < 0) {
			fprintf(stderr, "Bad index\n");
			return -1;
		}
		p.bist_share_mode = conversion_result;
		break;
	case TYPEC_CONTROL_COMMAND_SEND_VDM_REQ:
		if (argc < 5) {
			fprintf(stderr, "Missing VDM header and type\n");
			return -1;
		}
		if (argc > 4 + VDO_MAX_SIZE) {
			fprintf(stderr, "Too many VDOs\n");
			return -1;
		}

		conversion_result = strtol(argv[3], &endptr, 0);
		if ((endptr && *endptr) || conversion_result > UINT8_MAX ||
		    conversion_result < 0) {
			fprintf(stderr, "Bad SOP* type\n");
			return -1;
		}
		p.vdm_req_params.partner_type = conversion_result;

		int vdm_index;
		for (vdm_index = 0; vdm_index < argc - 4; vdm_index++) {
			uint32_t vdm_entry =
				strtoul(argv[vdm_index + 4], &endptr, 0);
			if (endptr && *endptr) {
				fprintf(stderr, "Bad VDO\n");
				return -1;
			}
			p.vdm_req_params.vdm_data[vdm_index] = vdm_entry;
		}
		p.vdm_req_params.vdm_data_objects = vdm_index;
	}

	rv = ec_command(EC_CMD_TYPEC_CONTROL, 0, &p, sizeof(p), ec_inbuf,
			ec_max_insize);
	if (rv < 0)
		return -1;

	return 0;
}

int cmd_typec_discovery(int argc, char *argv[])
{
	struct ec_params_typec_discovery p;
	struct ec_response_typec_discovery *r =
		(struct ec_response_typec_discovery *)ec_inbuf;
	char *e;
	int rv, i, j;

	if (argc < 3) {
		fprintf(stderr,
			"Usage: %s <port> <type>\n"
			"  <port> is the type-c port to query\n"
			"  <type> is one of:\n"
			"    0: SOP\n"
			"    1: SOP prime\n",
			argv[0]);
		return -1;
	}

	p.port = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port\n");
		return -1;
	}

	p.partner_type = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad type\n");
		return -1;
	}

	rv = ec_command(EC_CMD_TYPEC_DISCOVERY, 0, &p, sizeof(p), ec_inbuf,
			ec_max_insize);
	if (rv < 0)
		return -1;

	if (r->identity_count == 0) {
		printf("No identity discovered\n");
		return 0;
	}

	printf("Identity VDOs:\n");
	for (i = 0; i < r->identity_count; i++)
		printf("0x%08x\n", r->discovery_vdo[i]);

	if (r->svid_count == 0) {
		printf("No SVIDs discovered\n");
		return 0;
	}

	for (i = 0; i < r->svid_count; i++) {
		printf("SVID 0x%04x Modes:\n", r->svids[i].svid);
		for (j = 0; j < r->svids[i].mode_count; j++)
			printf("0x%08x\n", r->svids[i].mode_vdo[j]);
	}

	return 0;
}

/* Print shared fields of sink and source cap PDOs */
static inline void print_pdo_fixed(uint32_t pdo)
{
	printf("    Fixed: %dmV %dmA %s%s%s%s", PDO_FIXED_VOLTAGE(pdo),
	       PDO_FIXED_CURRENT(pdo), pdo & PDO_FIXED_DUAL_ROLE ? "DRP " : "",
	       pdo & PDO_FIXED_UNCONSTRAINED ? "UP " : "",
	       pdo & PDO_FIXED_COMM_CAP ? "USB " : "",
	       pdo & PDO_FIXED_DATA_SWAP ? "DRD" : "");
}

static inline void print_pdo_battery(uint32_t pdo)
{
	printf("    Battery: max %dmV min %dmV max %dmW\n",
	       PDO_BATT_MAX_VOLTAGE(pdo), PDO_BATT_MIN_VOLTAGE(pdo),
	       PDO_BATT_MAX_POWER(pdo));
}

static inline void print_pdo_variable(uint32_t pdo)
{
	printf("    Variable: max %dmV min %dmV max %dmA\n",
	       PDO_VAR_MAX_VOLTAGE(pdo), PDO_VAR_MIN_VOLTAGE(pdo),
	       PDO_VAR_MAX_CURRENT(pdo));
}

static inline void print_pdo_augmented(uint32_t pdo)
{
	printf("    Augmented: max %dmV min %dmV max %dmA\n",
	       PDO_AUG_MAX_VOLTAGE(pdo), PDO_AUG_MIN_VOLTAGE(pdo),
	       PDO_AUG_MAX_CURRENT(pdo));
}

int cmd_typec_status(int argc, char *argv[])
{
	struct ec_params_typec_status p;
	struct ec_response_typec_status *r =
		(struct ec_response_typec_status *)ec_inbuf;
	char *endptr;
	int rv, i;
	const char *desc;

	if (argc != 2) {
		fprintf(stderr,
			"Usage: %s <port>\n"
			"  <port> is the type-c port to query\n",
			argv[0]);
		return -1;
	}

	p.port = strtol(argv[1], &endptr, 0);
	if (endptr && *endptr) {
		fprintf(stderr, "Bad port\n");
		return -1;
	}

	rv = ec_command(EC_CMD_TYPEC_STATUS, 0, &p, sizeof(p), ec_inbuf,
			ec_max_insize);
	if (rv == -EC_RES_INVALID_COMMAND - EECRESULT)
		/* Fall back to PD_CONTROL to support older ECs */
		return cmd_usb_pd(argc, argv);
	else if (rv < 0)
		return -1;

	printf("Port C%d: %s, %s  State:%s\n"
	       "Role:%s %s%s, Polarity:CC%d\n",
	       p.port, r->pd_enabled ? "enabled" : "disabled",
	       r->dev_connected ? "connected" : "disconnected", r->tc_state,
	       (r->power_role == PD_ROLE_SOURCE) ? "SRC" : "SNK",
	       (r->data_role == PD_ROLE_DFP) ? "DFP" :
	       (r->data_role == PD_ROLE_UFP) ? "UFP" :
					       "",
	       (r->vconn_role == PD_ROLE_VCONN_SRC) ? " VCONN" : "",
	       (r->polarity % 2 + 1));

	switch (r->cc_state) {
	case PD_CC_NONE:
		desc = "None";
		break;
	case PD_CC_UFP_AUDIO_ACC:
		desc = "UFP Audio accessory";
		break;
	case PD_CC_UFP_DEBUG_ACC:
		desc = "UFP Debug accessory";
		break;
	case PD_CC_UFP_ATTACHED:
		desc = "UFP attached";
		break;
	case PD_CC_DFP_DEBUG_ACC:
		desc = "DFP Debug accessory";
		break;
	case PD_CC_DFP_ATTACHED:
		desc = "DFP attached";
		break;
	default:
		desc = "UNKNOWN";
		break;
	}
	printf("CC State: %s\n", desc);

	if (r->dp_pin) {
		switch (r->dp_pin) {
		case MODE_DP_PIN_A:
			desc = "A";
			break;
		case MODE_DP_PIN_B:
			desc = "B";
			break;
		case MODE_DP_PIN_C:
			desc = "C";
			break;
		case MODE_DP_PIN_D:
			desc = "D";
			break;
		case MODE_DP_PIN_E:
			desc = "E";
			break;
		case MODE_DP_PIN_F:
			desc = "F";
			break;
		default:
			desc = "UNKNOWN";
			break;
		}
		printf("DP pin mode: %s\n", desc);
	}

	if (r->mux_state) {
		printf("MUX: USB=%d DP=%d POLARITY=%s HPD_IRQ=%d HPD_LVL=%d\n"
		       "     SAFE=%d TBT=%d USB4=%d\n",
		       !!(r->mux_state & USB_PD_MUX_USB_ENABLED),
		       !!(r->mux_state & USB_PD_MUX_DP_ENABLED),
		       (r->mux_state & USB_PD_MUX_POLARITY_INVERTED) ?
			       "INVERTED" :
			       "NORMAL",
		       !!(r->mux_state & USB_PD_MUX_HPD_IRQ),
		       !!(r->mux_state & USB_PD_MUX_HPD_LVL),
		       !!(r->mux_state & USB_PD_MUX_SAFE_MODE),
		       !!(r->mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED),
		       !!(r->mux_state & USB_PD_MUX_USB4_ENABLED));
	}

	printf("Port events: 0x%08x\n", r->events);

	if (r->sop_revision)
		printf("SOP  PD Rev: %d.%d\n",
		       PD_STATUS_REV_GET_MAJOR(r->sop_revision),
		       PD_STATUS_REV_GET_MINOR(r->sop_revision));

	if (r->sop_prime_revision)
		printf("SOP' PD Rev: %d.%d\n",
		       PD_STATUS_REV_GET_MAJOR(r->sop_prime_revision),
		       PD_STATUS_REV_GET_MINOR(r->sop_prime_revision));

	for (i = 0; i < r->source_cap_count; i++) {
		/*
		 * Bits 31:30 always indicate the type of PDO
		 *
		 * Table 6-7 PD Rev 3.0 Ver 2.0
		 */
		uint32_t pdo = r->source_cap_pdos[i];
		int pdo_type = pdo & PDO_TYPE_MASK;

		if (i == 0)
			printf("Source Capabilities:\n");

		if (pdo_type == PDO_TYPE_FIXED) {
			print_pdo_fixed(pdo);
			printf("\n");
		} else if (pdo_type == PDO_TYPE_BATTERY) {
			print_pdo_battery(pdo);
		} else if (pdo_type == PDO_TYPE_VARIABLE) {
			print_pdo_variable(pdo);
		} else {
			print_pdo_augmented(pdo);
		}
	}

	for (i = 0; i < r->sink_cap_count; i++) {
		/*
		 * Bits 31:30 always indicate the type of PDO
		 *
		 * Table 6-7 PD Rev 3.0 Ver 2.0
		 */
		uint32_t pdo = r->sink_cap_pdos[i];
		int pdo_type = pdo & PDO_TYPE_MASK;

		if (i == 0)
			printf("Sink Capabilities:\n");

		if (pdo_type == PDO_TYPE_FIXED) {
			print_pdo_fixed(pdo);
			/* Note: FRS bits are reserved in PD 2.0 spec */
			printf("%s\n",
			       pdo & PDO_FIXED_FRS_CURR_MASK ? "FRS" : "");
		} else if (pdo_type == PDO_TYPE_BATTERY) {
			print_pdo_battery(pdo);
		} else if (pdo_type == PDO_TYPE_VARIABLE) {
			print_pdo_variable(pdo);
		} else {
			print_pdo_augmented(pdo);
		}
	}

	return 0;
}

int cmd_typec_vdm_response(int argc, char *argv[])
{
	struct ec_params_typec_vdm_response p;
	struct ec_response_typec_vdm_response *r =
		(ec_response_typec_vdm_response *)ec_inbuf;
	char *endptr;
	int rv, i;

	if (argc != 2) {
		fprintf(stderr,
			"Usage: %s <port>\n"
			"  <port> is the type-c port to query\n",
			argv[0]);
		return -1;
	}

	p.port = strtol(argv[1], &endptr, 0);
	if (endptr && *endptr) {
		fprintf(stderr, "Bad port\n");
		return -1;
	}

	rv = ec_command(EC_CMD_TYPEC_VDM_RESPONSE, 0, &p, sizeof(p), ec_inbuf,
			ec_max_insize);
	if (rv < 0)
		return -1;

	if (r->vdm_data_objects > 0 && r->vdm_response_err == EC_RES_SUCCESS) {
		printf("VDM response from partner: %d", r->partner_type);
		for (i = 0; i < r->vdm_data_objects; i++)
			printf("\n  0x%08x", r->vdm_response[i]);
		printf("\n");
	} else {
		printf("No VDM response found (err: %d)\n",
		       r->vdm_response_err);
	}

	if (r->vdm_attention_objects > 0) {
		printf("VDM Attention:");
		for (i = 0; i < r->vdm_attention_objects; i++)
			printf("\n  0x%08x", r->vdm_attention[i]);
		printf("\n");
		printf("%d Attention messages remaining\n",
		       r->vdm_attention_left);
	} else {
		printf("No VDM Attention found");
	}

	return 0;
}

int cmd_tp_self_test(int argc, char *argv[])
{
	int rv;

	rv = ec_command(EC_CMD_TP_SELF_TEST, 0, NULL, 0, NULL, 0);
	if (rv < 0)
		return rv;

	printf("Touchpad self test: %s\n",
	       rv == EC_RES_SUCCESS ? "passed" : "failed");

	return rv;
}

int cmd_tp_frame_get(int argc, char *argv[])
{
	int i, j;
	uint32_t remaining = 0, offset = 0;
	int rv = EC_SUCCESS;
	uint8_t *data;
	struct ec_response_tp_frame_info *r;
	struct ec_params_tp_frame_get p;

	data = (uint8_t *)(malloc(ec_max_insize));
	r = (struct ec_response_tp_frame_info *)(malloc(ec_max_insize));

	if (data == NULL || r == NULL) {
		fprintf(stderr, "Couldn't allocate memory.\n");
		free(r);
		free(data);
		return EC_ERROR_UNKNOWN;
	}

	rv = ec_command(EC_CMD_TP_FRAME_INFO, 0, NULL, 0, r, ec_max_insize);
	if (rv < 0) {
		fprintf(stderr, "Failed to get touchpad frame info.\n");
		goto err;
	}

	rv = ec_command(EC_CMD_TP_FRAME_SNAPSHOT, 0, NULL, 0, NULL, 0);
	if (rv < 0) {
		fprintf(stderr, "Failed to snapshot frame.\n");
		goto err;
	}

	for (i = 0; i < r->n_frames; i++) {
		p.frame_index = i;
		offset = 0;
		remaining = r->frame_sizes[i];

		while (remaining > 0) {
			p.offset = offset;
			p.size = MIN(remaining, ec_max_insize);

			rv = ec_command(EC_CMD_TP_FRAME_GET, 0, &p, sizeof(p),
					data, p.size);
			if (rv < 0) {
				fprintf(stderr,
					"Failed to get frame data "
					"at offset 0x%x\n",
					offset);
				goto err;
			}

			for (j = 0; j < p.size; j++)
				printf("%02x ", data[j]);

			offset += p.size;
			remaining -= p.size;
		}
		printf("\n");
	}

err:
	free(data);
	free(r);

	return rv < 0;
}

int cmd_wait_event(int argc, char *argv[])
{
	static const char *const mkbp_event_text[] = EC_MKBP_EVENT_TEXT;
	static const char *const host_event_text[] = HOST_EVENT_TEXT;

	int rv, i;
	struct ec_response_get_next_event_v1 buffer;
	long timeout = 5000;
	long event_type;
	char *e;

	BUILD_ASSERT(ARRAY_SIZE(mkbp_event_text) == EC_MKBP_EVENT_COUNT);
	/*
	 * Only 64 host events are supported. The enum |host_event_code| uses
	 * 1-based counting so it can skip 0 (NONE). The last legal host event
	 * number is 64, so ARRAY_SIZE(host_event_text) <= 64+1.
	 */
	BUILD_ASSERT(ARRAY_SIZE(host_event_text) <= 65);

	if (!ec_pollevent) {
		fprintf(stderr, "Polling for MKBP event not supported\n");
		return -EINVAL;
	}

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <type> [<timeout>]\n", argv[0]);
		fprintf(stderr, "\n");
		fprintf(stderr, "type: MKBP event number or name.\n");
		for (int i = 0; i < ARRAY_SIZE(mkbp_event_text); i++) {
			const char *name = mkbp_event_text[i];

			if (name) {
				fprintf(stderr, "      %s or %d\n", name, i);
			}
		}

		return -1;
	}

	rv = find_enum_from_text(argv[1], mkbp_event_text,
				 ARRAY_SIZE(mkbp_event_text), &event_type);
	if (rv < 0 || event_type < 0 || event_type >= EC_MKBP_EVENT_COUNT) {
		fprintf(stderr, "Bad event type '%s'.\n", argv[1]);
		return -1;
	}
	if (argc >= 3) {
		timeout = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad timeout value '%s'.\n", argv[2]);
			return -1;
		}
	}

	rv = wait_event(event_type, &buffer, sizeof(buffer), timeout);
	if (rv < 0)
		return rv;

	printf("MKBP event %d data: ", buffer.event_type);
	for (i = 0; i < rv - 1; ++i)
		printf("%02x ", buffer.data.key_matrix[i]);
	printf("\n");

	switch (event_type) {
	case EC_MKBP_EVENT_HOST_EVENT:
		printf("Host events:");
		for (int evt = 1; evt < ARRAY_SIZE(host_event_text); evt++) {
			if (buffer.data.host_event & EC_HOST_EVENT_MASK(evt)) {
				const char *name = host_event_text[evt];

				printf(" %s", name ? name : "UNKNOWN");
			}
		}
		printf("\n");
		break;
	}

	return 0;
}

static void cmd_cec_help(void)
{
	fprintf(stderr, "  Usage: cec <port> write [write bytes...]\n"
			"    Write message on the CEC bus\n"
			"  Usage: cec <port> read [timeout]\n"
			"    [timeout] in seconds\n"
			"  Usage: cec <port> get <param>\n"
			"  Usage: cec <port> set <param> <val>\n"
			"    <param> is one of:\n"
			"      address: CEC receive address\n"
			"        <val> is the new CEC address\n"
			"      enable: Enable or disable CEC\n"
			"        <val> is 1 to enable, 0 to disable\n");
}

static long timespec_diff_ms(const struct timespec *t1,
			     const struct timespec *t2)
{
	return ((t1->tv_sec - t2->tv_sec) * 1000 +
		(t1->tv_nsec - t2->tv_nsec) / 1000000);
}

static int cmd_cec_write(int port, int argc, char *argv[])
{
	char *e;
	long val;
	int rv, i, msg_len;
	struct ec_params_cec_write p;
	struct ec_params_cec_write_v1 p_v1;
	struct ec_response_get_next_event_v1 buffer;
	int version;
	uint8_t *msg_param;
	struct timespec start, now;
	const long timeout_ms = 1000; /* How long to wait for the send result */
	long elapsed_ms;
	uint32_t event_port, events;

	if (argc < 3 || argc > 18) {
		fprintf(stderr, "Invalid number of params\n");
		cmd_cec_help();
		return -1;
	}

	msg_len = argc - 2;

	rv = get_latest_cmd_version(EC_CMD_CEC_WRITE_MSG, &version);
	if (rv < 0)
		return rv;

	if (version == 0) {
		msg_param = p.msg;
	} else {
		p_v1.port = port;
		p_v1.msg_len = msg_len;
		msg_param = p_v1.msg;
	}

	for (i = 0; i < msg_len; i++) {
		val = strtol(argv[i + 2], &e, 16);
		if (e && *e)
			return -1;
		if (val < 0 || val > 0xff)
			return -1;
		msg_param[i] = (uint8_t)val;
	}

	printf("Write to CEC: ");
	for (i = 0; i < msg_len; i++)
		printf("0x%02x ", msg_param[i]);
	printf("\n");

	if (version == 0)
		rv = ec_command(EC_CMD_CEC_WRITE_MSG, 0, &p, msg_len, NULL, 0);
	else
		rv = ec_command(EC_CMD_CEC_WRITE_MSG, version, &p_v1,
				sizeof(p_v1), NULL, 0);
	if (rv < 0)
		return rv;

	/*
	 * Wait for a send OK or send failed event. Retry multiple times since
	 * we might receive other events or events for other ports.
	 */
	clock_gettime(CLOCK_REALTIME, &start);
	while (true) {
		clock_gettime(CLOCK_REALTIME, &now);
		elapsed_ms = timespec_diff_ms(&now, &start);
		if (elapsed_ms >= timeout_ms)
			break;

		rv = wait_event(EC_MKBP_EVENT_CEC_EVENT, &buffer,
				sizeof(buffer), timeout_ms - elapsed_ms);
		if (rv < 0)
			return rv;

		event_port = EC_MKBP_EVENT_CEC_GET_PORT(buffer.data.cec_events);
		events = EC_MKBP_EVENT_CEC_GET_EVENTS(buffer.data.cec_events);

		if (event_port != port)
			continue;

		if (events & EC_MKBP_CEC_SEND_OK)
			return 0;

		if (events & EC_MKBP_CEC_SEND_FAILED) {
			fprintf(stderr, "Send failed\n");
			return -1;
		}
	}

	fprintf(stderr, "No send result received\n");

	return -1;
}

static int cec_read_handle_cec_event(int port, uint32_t cec_events,
				     uint8_t *msg, uint8_t *msg_len)
{
	int rv;
	uint32_t event_port, events;
	struct ec_params_cec_read p;
	struct ec_response_cec_read r;

	/* Extract the port and events */
	event_port = EC_MKBP_EVENT_CEC_GET_PORT(cec_events);
	events = EC_MKBP_EVENT_CEC_GET_EVENTS(cec_events);

	/* Check if it's the HAVE_DATA event we're waiting for */
	if (event_port != port || !(events & EC_MKBP_CEC_HAVE_DATA)) {
		*msg_len = 0;
		return 0;
	}

	/* Data is ready, so send the read command */
	p.port = port;
	rv = ec_command(EC_CMD_CEC_READ_MSG, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0) {
		/*
		 * When the kernel driver is enabled it may read the data out
		 * first, so the queue will be empty and the command will
		 * returns EC_RES_UNAVAILABLE. The ectool read command is still
		 * useful for testing if the kernel driver is not enabled.
		 */
		printf("Note: `cec read` doesn't work if the cros_ec_cec "
		       "kernel driver is running\n");
		return rv;
	}

	/* Message received successfully */
	memcpy(msg, r.msg, r.msg_len);
	*msg_len = r.msg_len;
	return 0;
}

static int cmd_cec_read(int port, int argc, char *argv[])
{
	int i, rv;
	char *e;
	struct ec_response_get_next_event_v1 buffer;
	long timeout_ms = 5000;
	unsigned long event_mask;
	struct timespec start, now;
	long elapsed_ms;
	int event_size;
	bool received = false;
	uint8_t msg[MAX_CEC_MSG_LEN];
	uint8_t msg_len;

	if (!ec_pollevent) {
		fprintf(stderr, "Polling for MKBP event not supported\n");
		return -EINVAL;
	}

	if (argc >= 3) {
		timeout_ms = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad timeout value '%s'.\n", argv[2]);
			return -1;
		}
	}

	/*
	 * There are two ways of receiving CEC messages:
	 * 1. Old EC firmware which only supports one port sends the data in a
	 *    cec_message MKBP event.
	 * 2. New EC firmware which supports multiple ports sends
	 *    EC_MKBP_CEC_HAVE_DATA to notify that data is ready, then
	 *    EC_CMD_CEC_READ_MSG is used to read it.
	 * To make ectool compatible with both, we wait for either
	 * EC_MKBP_EVENT_CEC_MESSAGE or EC_MKBP_CEC_HAVE_DATA.
	 */
	event_mask = BIT(EC_MKBP_EVENT_CEC_EVENT) |
		     BIT(EC_MKBP_EVENT_CEC_MESSAGE);
	clock_gettime(CLOCK_REALTIME, &start);
	while (true) {
		clock_gettime(CLOCK_REALTIME, &now);
		elapsed_ms = timespec_diff_ms(&now, &start);
		if (elapsed_ms >= timeout_ms)
			break;

		rv = wait_event_mask(event_mask, &buffer, sizeof(buffer),
				     timeout_ms - elapsed_ms);
		if (rv < 0)
			return rv;
		event_size = rv;

		if (buffer.event_type == EC_MKBP_EVENT_CEC_EVENT) {
			rv = cec_read_handle_cec_event(
				port, buffer.data.cec_events, msg, &msg_len);
			if (rv < 0)
				return rv;

			/* Message received successfully */
			if (msg_len) {
				received = true;
				break;
			}
			/* No message received, continue waiting */

		} else if (buffer.event_type == EC_MKBP_EVENT_CEC_MESSAGE) {
			/* Message received successfully */
			received = true;
			msg_len = event_size - 1;
			memcpy(msg, buffer.data.cec_message, msg_len);
			break;
		}
	}

	if (!received) {
		fprintf(stderr, "Timed out waiting for message\n");
		return -1;
	}

	printf("CEC data: ");
	for (i = 0; i < msg_len; i++)
		printf("0x%02x ", msg[i]);
	printf("\n");

	return 0;
}

static int cec_cmd_from_str(const char *str)
{
	if (!strcmp("address", str))
		return CEC_CMD_LOGICAL_ADDRESS;
	if (!strcmp("enable", str))
		return CEC_CMD_ENABLE;
	return -1;
}

static int cmd_cec_set(int port, int argc, char *argv[])
{
	char *e;
	struct ec_params_cec_set p;
	uint8_t val;
	int cmd;

	if (argc != 4) {
		fprintf(stderr, "Invalid number of params\n");
		cmd_cec_help();
		return -1;
	}

	val = (uint8_t)strtol(argv[3], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad parameter '%s'.\n", argv[3]);
		return -1;
	}

	cmd = cec_cmd_from_str(argv[2]);
	if (cmd < 0) {
		fprintf(stderr, "Invalid command '%s'.\n", argv[2]);
		return -1;
	}
	p.cmd = cmd;
	p.port = port;
	p.val = val;

	return ec_command(EC_CMD_CEC_SET, 0, &p, sizeof(p), NULL, 0);
}

static int cmd_cec_get(int port, int argc, char *argv[])
{
	int rv, cmd;
	struct ec_params_cec_get p;
	struct ec_response_cec_get r;

	if (argc != 3) {
		fprintf(stderr, "Invalid number of params\n");
		cmd_cec_help();
		return -1;
	}

	cmd = cec_cmd_from_str(argv[2]);
	if (cmd < 0) {
		fprintf(stderr, "Invalid command '%s'.\n", argv[2]);
		return -1;
	}
	p.cmd = cmd;
	p.port = port;

	rv = ec_command(EC_CMD_CEC_GET, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("%d\n", r.val);

	return 0;
}

int cmd_cec(int argc, char *argv[])
{
	int port;
	char *e;

	if (argc < 3) {
		fprintf(stderr, "Invalid number of params\n");
		cmd_cec_help();
		return -1;
	}

	port = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Invalid port: %s\n", argv[1]);
		cmd_cec_help();
		return -1;
	}
	argc--;
	argv++;

	if (!strcmp(argv[1], "write"))
		return cmd_cec_write(port, argc, argv);
	if (!strcmp(argv[1], "read"))
		return cmd_cec_read(port, argc, argv);
	if (!strcmp(argv[1], "get"))
		return cmd_cec_get(port, argc, argv);
	if (!strcmp(argv[1], "set"))
		return cmd_cec_set(port, argc, argv);

	fprintf(stderr, "Invalid sub command: %s\n", argv[1]);
	cmd_cec_help();

	return -1;
}

static void cmd_s0ix_counter_help(char *cmd)
{
	fprintf(stderr,
		"  Usage: %s get - to get the value of s0ix counter\n"
		"         %s reset - to reset s0ix counter \n",
		cmd, cmd);
}

static int cmd_s0ix_counter(int argc, char *argv[])
{
	struct ec_params_s0ix_cnt p;
	struct ec_response_s0ix_cnt r;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Invalid number of params\n");
		cmd_s0ix_counter_help(argv[0]);
		return -1;
	}

	if (!strcasecmp(argv[1], "get")) {
		p.flags = 0;
	} else if (!strcasecmp(argv[1], "reset")) {
		p.flags = EC_S0IX_COUNTER_RESET;
	} else {
		fprintf(stderr, "Bad subcommand: %s\n", argv[1]);
		return -1;
	}

	rv = ec_command(EC_CMD_GET_S0IX_COUNTER, 0, &p, sizeof(p), &r,
			sizeof(r));
	if (rv < 0) {
		return rv;
	}

	printf("s0ix_counter: %u\n", r.s0ix_counter);

	return 0;
}

/* NULL-terminated list of commands. Please keep sorted. */
const struct command commands[] = {
	{ "adcread", cmd_adc_read, "<channel>\n\tRead an ADC channel." },
	{ "addentropy", cmd_add_entropy,
	  "[reset]\n\tAdd entropy to device secret." },
	{ "apreset", cmd_apreset, "\n\tIssue AP reset." },
	{ "autofanctrl", cmd_thermal_auto_fan_ctrl,
	  "<on>\n\tTurn on automatic fan speed control." },
	{ "backlight", cmd_lcd_backlight,
	  "<enabled>\n\tEnable/disable LCD backlight." },
	{ "basestate", cmd_basestate,
	  "[attach | detach | reset]\n"
	  "\tManually force base state to attached, detached or reset." },
	{ "battery", cmd_battery, "\n\tPrints battery info." },
	{ "batterycutoff", cmd_battery_cut_off,
	  "[at-shutdown]\n\tCut off battery output power." },
	{ "batteryparam", cmd_battery_vendor_param,
	  "\n\tRead or write board-specific battery parameter." },
	{ "bcfg", cmd_battery_config, "\n\tPrint an active battery config." },
	{ "boardversion", cmd_board_version, "\n\tPrints the board version." },
	{ "boottime", cmd_boottime, "\n\tGet boot time." },
	{ "button", cmd_button,
	  "[vup|vdown|rec] <Delay-ms>\n\tSimulates button press." },
	{ "cbi", cmd_cbi, "\n\tGet/Set/Remove Cros Board Info." },
	{ "cec", cmd_cec, "\n\tRead or write CEC messages and settings." },
	{ "chargecontrol", cmd_charge_control,
	  "\n\tForce the battery to stop charging or discharge." },
	{ "chargecurrentlimit", cmd_charge_current_limit,
	  "\n\tSet the maximum battery charging current and the minimum battery\n"
	  "\tSoC at which it will apply." },
	{ "chargeoverride", cmd_charge_port_override,
	  "\n\tOverrides charge port selection logic." },
	{ "chargesplash", cmd_chargesplash,
	  "\n\tShow and manipulate chargesplash variables." },
	{ "chargestate", cmd_charge_state,
	  "\n\tHandle commands related to charge state v2 (and later)." },
	{ "chipinfo", cmd_chipinfo, "\n\tPrints chip info." },
	{ "cmdversions", cmd_cmdversions,
	  "<cmd>\n\tPrints supported version mask for a command number." },
	{ "console", cmd_console,
	  "\n\tPrints the last output to the EC debug console." },
	{ "echash", cmd_ec_hash, "[CMDS]\n\tVarious EC hash commands." },
	{ "eventclear", cmd_host_event_clear,
	  "<mask>\n"
	  "\tClears EC host events flags where mask has bits set." },
	{ "eventclearb", cmd_host_event_clear_b,
	  "<mask>\n"
	  "\tClears EC host events flags copy B where mask has bits set." },
	{ "eventget", cmd_host_event_get_raw,
	  "\n\tPrints raw EC host event flags." },
	{ "eventgetb", cmd_host_event_get_b,
	  "\n\tPrints raw EC host event flags copy B." },
	{ "eventgetscimask", cmd_host_event_get_sci_mask,
	  "\n\tPrints SCI mask for EC host events." },
	{ "eventgetsmimask", cmd_host_event_get_smi_mask,
	  "\n\tPrints SMI mask for EC host events." },
	{ "eventgetwakemask", cmd_host_event_get_wake_mask,
	  "\n\tPrints wake mask for EC host events." },
	{ "eventsetscimask", cmd_host_event_set_sci_mask,
	  "<mask>\n\tSets the SCI mask for EC host events." },
	{ "eventsetsmimask", cmd_host_event_set_smi_mask,
	  "<mask>\n\tSets the SMI mask for EC host events." },
	{ "eventsetwakemask", cmd_host_event_set_wake_mask,
	  "<mask>\n\tSets the wake mask for EC host events" },
	{ "extpwrlimit", cmd_ext_power_limit,
	  "\n\tSet the maximum external power limit." },
	{ "fanduty", cmd_fanduty,
	  "<percent>\n\tForces the fan PWM to a constant duty cycle." },
	{ "flasherase", cmd_flash_erase,
	  "<offset> <size>\n\tErases EC flash." },
	{ "flasheraseasync", cmd_flash_erase,
	  "<offset> <size>\n"
	  "\tErases EC flash asynchronously." },
	{ "flashinfo", cmd_flash_info,
	  "\n\tPrints information on the EC flash." },
	{ "flashpd", cmd_flash_pd,
	  "<dev_id> <port> <filename>\n"
	  "\tFlash commands over PD." },
	{ "flashprotect", cmd_flash_protect,
	  "[now] [enable | disable]\n"
	  "\tPrints or sets EC flash protection state." },
	{ "flashread", cmd_flash_read,
	  "<offset> <size> <outfile>\n"
	  "\tReads from EC flash to a file." },
	{ "flashspiinfo", cmd_flash_spi_info,
	  "\n\tPrints information on EC SPI flash, if present." },
	{ "flashwrite", cmd_flash_write,
	  "<offset> <infile>\n"
	  "\tWrites to EC flash from a file." },
	{ "forcelidopen", cmd_force_lid_open,
	  "<enable>\n"
	  "\tForces the lid switch to open position." },
	{ "fpcontext", cmd_fp_context,
	  "\n\tSets the fingerprint sensor context." },
	{ "fpencstatus", cmd_fp_enc_status,
	  "\n\tPrints status of Fingerprint sensor encryption engine." },
	{ "fpframe", cmd_fp_frame,
	  "\n\tRetrieve the finger image as a PGM image." },
	{ "fpinfo", cmd_fp_info,
	  "\n\tPrints information about the Fingerprint sensor." },
	{ "fpmode", cmd_fp_mode,
	  "[mode... [capture_type]]\n"
	  "\tConfigure/Read the fingerprint sensor current mode.\n"
	  "\tmode: capture|deepsleep|fingerdown|fingerup|enroll|match|\n"
	  "\t\treset|reset_sensor|maintenance\n"
	  "\tcapture_type: vendor|pattern0|pattern1|qual|test_reset" },
	{ "fpseed", cmd_fp_seed, "\n\tSets the value of the TPM seed." },
	{ "fpstats", cmd_fp_stats,
	  "\n\tPrints timing statisitcs relating to capture and matching." },
	{ "fptemplate", cmd_fp_template,
	  "[<infile>|<index 0..2>]\n"
	  "\tAdd a template if <infile> is provided, else dump it." },
	{ "gpioget", cmd_gpio_get,
	  "<GPIO name>\n"
	  "\tGet the value of GPIO signal." },
	{ "gpioset", cmd_gpio_set,
	  "<GPIO name>\n"
	  "\tSet the value of GPIO signal." },
	{ "hangdetect", cmd_hang_detect,
	  "reload|cancel|set_timeout <reboot_sec>|get_status|clear_status\n"
	  "\tConfigure the ap hang detect mechanism." },
	{ "hello", cmd_hello, "\n\tChecks for basic communication with EC." },
	{ "hibdelay", cmd_hibdelay,
	  "[sec]\n"
	  "\tSet the delay before going into hibernation." },
	{ "hostevent", cmd_hostevent, "\n\tGet & set host event masks." },
	{ "hostsleepstate", cmd_hostsleepstate,
	  "\n\tReport host sleep state to the EC." },
	{ "i2cprotect", cmd_i2c_protect,
	  "<port> [status]\n"
	  "\tProtect EC's I2C bus." },
	{ "i2cread", cmd_i2c_read, "\n\tRead I2C bus." },
	{ "i2cspeed", cmd_i2c_speed,
	  "<port> [speed]\n"
	  "\tGet or set EC's I2C bus speed." },
	{ "i2cwrite", cmd_i2c_write, "\n\tWrite I2C bus." },
	{ "i2cxfer", cmd_i2c_xfer,
	  "<port> <peripheral_addr> <read_count> [write bytes...]\n"
	  "\tPerform I2C transfer on EC's I2C bus." },
	{ "infopddev", cmd_pd_device_info,
	  "<port>\n"
	  "\tGet info about USB type-C accessory attached to port." },
	{ "inventory", cmd_inventory,
	  "\n\tReturn the list of supported features." },
	{ "kbfactorytest", cmd_keyboard_factory_test,
	  "\n\tScan out keyboard if any pins are shorted." },
	{ "kbgetconfig", cmd_keyboard_get_config,
	  "\n\tGet keyboard Vivaldi configuration." },
	{ "kbinfo", cmd_kbinfo, "\n\tDump keyboard matrix dimensions." },
	{ "kbpress", cmd_kbpress, "\n\tSimulate key press." },
	{ "keyconfig", cmd_keyconfig,
	  "get [<param>] | set [<param>> <value>]\n"
	  "\tConfigure keyboard scanning." },
	{ "keyscan", cmd_keyscan,
	  "<beat_us> <filename>\n"
	  "\tTest low-level key scanning." },
	{ "led", cmd_led,
	  "<name> <query | auto | off | <color> | <color>=<value>...>\n"
	  "\tSet the color of an LED or query brightness range." },
	{ "lightbar", cmd_lightbar,
	  "[CMDS]\n"
	  "\tVarious lightbar control commands." },
	{ "locatechip", cmd_locate_chip,
	  "<type> <index>\n"
	  "\tGet the addresses and ports of i2c connected and embedded chips." },
	{ "memory_dump", cmd_memory_dump,
	  "[<address> [<size>]]\n"
	  "\tOutputs the memory dump in hexdump canonical format." },
	{ "mkbpget", cmd_mkbp_get,
	  "<buttons|switches>\n"
	  "\tGet MKBP buttons/switches supported mask and current state." },
	{ "mkbpwakemask", cmd_mkbp_wake_mask,
	  "<get|set> <event|hostevent> [mask]\n"
	  "\tGet or Set the MKBP event wake mask, or host event wake mask." },
	{ "motionsense", cmd_motionsense,
	  "[CMDS]\n"
	  "\tVarious motion sense control commands." },
	{ "nextevent", cmd_next_event, "\n\tGet the next pending MKBP event." },
	{ "panicinfo", cmd_panic_info, "\n\tPrints saved panic info." },
	{ "pause_in_s5", cmd_s5,
	  "[on|off]\n"
	  "\tWhether or not the AP should pause in S5 on shutdown." },
	{ "pchg", cmd_pchg,
	  "[<port>]\n"
	  "\tGet peripheral charge port count and status." },
	{ "pdchipinfo", cmd_pd_chip_info,
	  "<port>\n"
	  "\tGet PD chip information." },
	{ "pdcontrol", cmd_pd_control,
	  "[suspend|resume|reset|disable|on]\n"
	  "\tControls the PD chip." },
	{ "pdgetmode", cmd_pd_get_amode,
	  "<port>\n"
	  "\tGet All USB-PD alternate SVIDs and modes on <port>." },
	{ "pdlog", cmd_pd_log, "\n\tPrints the PD event log entries." },
	{ "pdsetmode", cmd_pd_set_amode,
	  "<port> <svid> <opos>\n"
	  "\tSet USB-PD alternate SVID and mode on <port>." },
	{ "pdwritelog", cmd_pd_write_log,
	  "<type> <port>\n"
	  "\tWrites a PD event log of the given <type>." },
	{ "port80flood", cmd_port_80_flood,
	  "\n\tRapidly write bytes to port 80." },
	{ "port80read", cmd_port80_read,
	  "\n\tPrint history of port 80 write." },
	{ "powerinfo", cmd_power_info,
	  "\n\tPrints power-related information." },
	{ "protoinfo", cmd_proto_info,
	  "\n\tPrints EC host protocol information." },
	{ "pse", cmd_pse, "\n\tGet and set PoE PSE port power status." },
	{ "pstoreinfo", cmd_pstore_info,
	  "\n\tPrints information on the EC host persistent storage." },
	{ "pstoreread", cmd_pstore_read,
	  "<offset> <size> <outfile>\n"
	  "\tReads from EC host persistent storage to a file." },
	{ "pstorewrite", cmd_pstore_write,
	  "<offset> <infile>\n"
	  "\tWrites to EC host persistent storage from a file." },
	{ "pwmgetduty", cmd_pwm_get_duty,
	  "\n\tPrints the current 16 bit duty cycle for given PWM." },
	{ "pwmgetfanrpm", cmd_pwm_get_fan_rpm,
	  "[<index> | all]\n"
	  "\tPrints current fan RPM." },
	{ "pwmgetkblight", cmd_pwm_get_keyboard_backlight,
	  "\n\tPrints current keyboard backlight percent." },
	{ "pwmgetnumfans", cmd_pwm_get_num_fans,
	  "\n\tPrints the number of fans present." },
	{ "pwmsetduty", cmd_pwm_set_duty,
	  "\n\tSet 16 bit duty cycle of given PWM." },
	{ "pwmsetfanrpm", cmd_pwm_set_fan_rpm,
	  "<targetrpm>\n"
	  "\tSet target fan RPM." },
	{ "pwmsetkblight", cmd_pwm_set_keyboard_backlight,
	  "<percent>\n"
	  "\tSet keyboard backlight in percent." },
	{ "rand", cmd_rand,
	  "<num_bytes>\n"
	  "\tgenerate <num_bytes> of random numbers." },
	{ "reboot_ap_on_g3", cmd_reboot_ap_on_g3,
	  " [<delay>]\n"
	  "\tRequests that the EC will automatically reboot the AP after a\n"
	  "\tconfigurable number of seconds the next time we enter the G3\n"
	  "\tpower state." },
	{ "reboot_ec", cmd_reboot_ec,
	  "<RO|RW|cold|hibernate|hibernate-clear-ap-off|disable-jump|cold-ap-off>\n"
	  "\t[at-shutdown|switch-slot|clear-ap-idle]\n"
	  "\tReboot EC to RO or RW" },
	{ "rgbkbd", cmd_rgbkbd,
	  "...\n"
	  "\tSet/get RGB keyboard status, config, etc.." },
	{ "rollbackinfo", cmd_rollback_info,
	  "\n\tPrint rollback block information." },
	{ "rtcget", cmd_rtc_get, "\n\tPrint real-time clock." },
	{ "rtcgetalarm", cmd_rtc_get_alarm,
	  "\n\tPrint # of seconds before real-time clock alarm goes off." },
	{ "rtcset", cmd_rtc_set,
	  "<time>\n"
	  "\tSet real-time clock." },
	{ "rtcsetalarm", cmd_rtc_set_alarm,
	  "<sec>\n"
	  "\tSet real-time clock alarm to go off in <sec> seconds." },
	{ "rwhashpd", cmd_rw_hash_pd,
	  "<dev_id> <HASH[0] ... <HASH[4]>\n"
	  "\tSet entry in PD MCU's device rw_hash table." },
	{ "rwsig", cmd_rwsig,
	  "<info|dump|action|status> ...\n"
	  "\tinfo: Get all info about rwsig.\n"
	  "\tdump: Show individual rwsig field.\n"
	  "\taction: Control the behavior of RWSIG task.\n"
	  "\tstatus: Run RW signature verification and get status." },
	{ "rwsigaction", cmd_rwsig_action_legacy,
	  "(DEPRECATED; use \"rwsig action\")\n"
	  "\tControl the behavior of RWSIG task." },
	{ "rwsigstatus", cmd_rwsig_status,
	  "(DEPRECATED; use \"rwsig status\"\n"
	  "\tRun RW signature verification and get status." },
	{ "s0ix_counter", cmd_s0ix_counter,
	  "get|set\n"
	  "\tGet or reset s0ix counter." },
	{ "sertest", cmd_serial_test, "\n\tSerial output test for COM2." },
	{ "smartdischarge", cmd_smart_discharge,
	  "\n\tSet/Get smart discharge parameters." },
	{ "stress", cmd_stress_test,
	  "[reboot] [help]\n"
	  "\tStress test the ec host command interface." },
	{ "switches", cmd_switches, "\n\tPrints current EC switch positions" },
	{ "sysinfo", cmd_sysinfo,
	  "[flags|reset_flags|firmware_copy]\n"
	  "\tDisplay system info." },
	{ "tabletmode", cmd_tabletmode,
	  "[on | off | reset]\n"
	  "\tManually force tablet mode to on, off or reset." },
	{ "temps", cmd_temperature,
	  "<sensorid>\n"
	  "\tPrint temperature and temperature ratio between fan_off and\n"
	  "\tfan_max values, which could be a fan speed if it's controlled\n"
	  "\tlinearly." },
	{ "tempsinfo", cmd_temp_sensor_info,
	  "<sensorid>\n"
	  "\tPrint temperature sensor info." },
	{ "test", cmd_test,
	  "result length [version]\n"
	  "\tFake a variety of responses, purely for testing purposes." },
	{ "thermalget", cmd_thermal_get_threshold,
	  "<platform-specific args>\n"
	  "\tGet the threshold temperature values from the thermal engine." },
	{ "thermalset", cmd_thermal_set_threshold,
	  "<platform-specific args>\n"
	  "\tSet the threshold temperature values for the thermal engine." },
	{ "tmp006cal", cmd_tmp006cal,
	  "<tmp006_index> [params...]\n"
	  "\tGet/set TMP006 calibration." },
	{ "tmp006raw", cmd_tmp006raw,
	  "<tmp006_index>\n"
	  "\tGet raw TMP006 data." },
	{ "tpframeget", cmd_tp_frame_get, "\n\tGet touchpad frame data." },
	{ "tpselftest", cmd_tp_self_test, "\n\tRun touchpad self test." },
	{ "typeccontrol", cmd_typec_control,
	  "<port> <command>\n"
	  "\tControl USB PD policy." },
	{ "typecdiscovery", cmd_typec_discovery,
	  "<port> <type>\n"
	  "\tGet discovery information for port and type." },
	{ "typecstatus", cmd_typec_status,
	  "<port>\n"
	  "\tGet status information for port." },
	{ "typecvdmresponse", cmd_typec_vdm_response,
	  "<port>\n"
	  "\tGet last VDM response for AP-requested VDM." },
	{ "uptimeinfo", cmd_uptimeinfo,
	  "\n\tGet info about how long the EC has been running and the most\n"
	  "\trecent AP resets." },
	{ "usbchargemode", cmd_usb_charge_set_mode,
	  "<port> <mode> [<inhibit_charge>]\n"
	  "\tSet USB charging mode." },
	{ "usbmux", cmd_usb_mux,
	  "<mux>\n"
	  "\tSet USB mux switch state." },
	{ "usbpd", cmd_usb_pd,
	  "<port> <auto | "
	  "[toggle|toggle-off|sink|source] [none|usb|dp|dock]\n"
	  "\t[dr_swap|pr_swap|vconn_swap]>\n"
	  "\tControl USB PD/type-C [deprecated]." },
	{ "usbpddps", cmd_usb_pd_dps,
	  "[enable | disable]\n"
	  "\tEnable or disable dynamic pdo selection." },
	{ "usbpdmuxinfo", cmd_usb_pd_mux_info,
	  "[tsv]\n"
	  "\tGet USB-C SS mux info.\n"
	  "\t    tsv: Output as tab separated values. Columns are defined "
	  "as:\n"
	  "\t\t   Port, USB enabled, DP enabled, Polarity, HPD IRQ, "
	  "HPD LVL." },
	{ "usbpdpower", cmd_usb_pd_power,
	  "[port]\n"
	  "\tGet USB PD power information." },
	{ "version", cmd_version, "\n\tPrints EC version." },
	{ "waitevent", cmd_wait_event,
	  "<type> [<timeout>]\n"
	  "\tWait for the MKBP event of type and display it." },
	{ "wireless", cmd_wireless,
	  "<flags> [<mask> [<suspend_flags> <suspend_mask>]]\n"
	  "\tEnable/disable WLAN/Bluetooth radio." },
	{ NULL, NULL }
};

void print_help(const char *prog, int print_cmds)
{
	printf("Usage: %s [--dev=n]"
	       " [--interface=dev|i2c|lpc] [--i2c_bus=n] [--device=vid:pid]"
	       " --verbose",
	       prog);
	printf("[--name=cros_ec|cros_fp|cros_pd|cros_scp|cros_ish] [--ascii] ");
	printf("<command> [params]\n\n");
	printf("  --i2c_bus=n  Specifies the number of an I2C bus to use. For\n"
	       "               example, to use /dev/i2c-7, pass --i2c_bus=7.\n"
	       "               Implies --interface=i2c.\n\n");
	printf("  --interface Specifies the interface.\n\n");
	printf("  --device    Specifies USB endpoint by vendor ID and product\n"
	       "              ID (e.g. 18d1:5022).\n\n");
	printf("  --verbose   Print more messages.\n\n");
	if (print_cmds) {
		const struct command *cmd;
		puts(help_str);
		for (cmd = commands; cmd->name != NULL; cmd++) {
			printf("  %s ", cmd->name);
			if (cmd->help != NULL)
				puts(cmd->help);
			else
				puts("");
		}

	} else
		printf("Use '%s help' to print a list of commands.\n", prog);
}

int main(int argc, char *argv[])
{
	const struct command *cmd;
	int dev = 0;
	int interfaces = COMM_ALL;
	int i2c_bus = -1;
	char device_name[41] = CROS_EC_DEV_NAME;
	uint16_t vid = USB_VID_GOOGLE, pid = USB_PID_HAMMER;
	int rv = 1;
	int parse_error = 0;
	char *e;
	int i;

	BUILD_ASSERT(ARRAY_SIZE(lb_command_paramcount) == LIGHTBAR_NUM_CMDS);

	while ((i = getopt_long(argc, argv, "+v?", long_opts, NULL)) != -1) {
		switch (i) {
		case '?':
			/* Unhandled option */
			parse_error = 1;
			break;

		case OPT_DEV:
			dev = strtoull(optarg, &e, 0);
			if (!*optarg || (e && *e)) {
				fprintf(stderr, "Invalid --dev\n");
				parse_error = 1;
			}
			break;

		case OPT_INTERFACE:
			if (!strcasecmp(optarg, "dev")) {
				interfaces = COMM_DEV;
			} else if (!strcasecmp(optarg, "lpc")) {
				interfaces = COMM_LPC;
			} else if (!strcasecmp(optarg, "i2c")) {
				interfaces = COMM_I2C;
			} else if (!strcasecmp(optarg, "servo")) {
				interfaces = COMM_SERVO;
			} else {
				fprintf(stderr, "Invalid --interface\n");
				parse_error = 1;
			}
			break;
		case OPT_DEVICE:
			if (parse_vidpid(optarg, &vid, &pid)) {
				interfaces = COMM_USB;
			} else {
				fprintf(stderr, "Invalid --device\n");
				parse_error = 1;
			}
			break;
		case OPT_NAME:
			strncpy(device_name, optarg, 40);
			device_name[40] = '\0';
			break;
		case OPT_I2C_BUS:
			i2c_bus = strtoull(optarg, &e, 0);
			if (*optarg == '\0' || (e && *e != '\0') ||
			    i2c_bus < 0) {
				fprintf(stderr, "Invalid --i2c_bus\n");
				parse_error = 1;
			}
			break;
		case OPT_ASCII:
			ascii_mode = 1;
			break;
		case OPT_VERBOSE:
		case 'v':
			verbose = 1;
			break;
		}
	}

	if (i2c_bus != -1) {
		if (!(interfaces & COMM_I2C)) {
			fprintf(stderr,
				"--i2c_bus is specified, but --interface is set to something other than I2C\n");
			parse_error = 1;
		} else {
			interfaces = COMM_I2C;
		}
	}

	/* Must specify a command */
	if (!parse_error && optind == argc)
		parse_error = 1;

	/* 'ectool help' prints help with commands */
	if (!parse_error && !strcasecmp(argv[optind], "help")) {
		print_help(argv[0], 1);
		exit(1);
	}

	/* Handle sub-devices command offset */
	if (dev > 0 && dev < 4) {
		set_command_offset(EC_CMD_PASSTHRU_OFFSET(dev));
	} else if (dev == 8) {
		/* Special offset for Fingerprint MCU */
		strcpy(device_name, "cros_fp");
	} else if (dev != 0) {
		fprintf(stderr, "Bad device number %d\n", dev);
		parse_error = 1;
	}

	if (parse_error) {
		print_help(argv[0], 0);
		exit(1);
	}

	/* Prefer /dev method, which supports built-in mutex */
	if (!(interfaces & COMM_DEV) || comm_init_dev(device_name)) {
		/* If dev is excluded or isn't supported, find alternative */

		/* Lock is not needed for COMM_USB */
		if (!(interfaces & COMM_USB) &&
		    acquire_gec_lock(GEC_LOCK_TIMEOUT_SECS) < 0) {
			fprintf(stderr, "Could not acquire GEC lock.\n");
			exit(1);
		}
		if (interfaces == COMM_USB) {
			if (comm_init_usb(vid, pid)) {
				fprintf(stderr, "Couldn't find EC on USB.\n");
				goto out;
			}
		} else if (comm_init_alt(interfaces, device_name, i2c_bus)) {
			fprintf(stderr, "Couldn't find EC\n");
			goto out;
		}
	}

	if (comm_init_buffer()) {
		fprintf(stderr, "Couldn't initialize buffers\n");
		goto out;
	}

	/* Handle commands */
	for (cmd = commands; cmd->name; cmd++) {
		if (!strcasecmp(argv[optind], cmd->name)) {
			rv = cmd->handler(argc - optind, argv + optind);
			goto out;
		}
	}

	/* If we're still here, command was unknown */
	fprintf(stderr, "Unknown command '%s'\n\n", argv[optind]);
	print_help(argv[0], 0);

out:
	release_gec_lock();

	if (interfaces == COMM_USB)
		comm_usb_exit();

	return !!rv;
}
