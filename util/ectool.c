/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "battery.h"
#include "comm-host.h"
#include "compile_time_macros.h"
#include "ec_flash.h"
#include "ectool.h"
#include "lightbar.h"
#include "lock/gec_lock.h"
#include "misc_util.h"
#include "panic.h"
#include "usb_pd.h"

/* Command line options */
enum {
	OPT_DEV = 1000,
	OPT_INTERFACE,
	OPT_NAME,
};

static struct option long_opts[] = {
	{"dev", 1, 0, OPT_DEV},
	{"interface", 1, 0, OPT_INTERFACE},
	{"name", 1, 0, OPT_NAME},
	{NULL, 0, 0, 0}
};

#define GEC_LOCK_TIMEOUT_SECS	30  /* 30 secs */

const char help_str[] =
	"Commands:\n"
	"  extpwrcurrentlimit\n"
	"      Set the maximum external power current\n"
	"  autofanctrl <on>\n"
	"      Turn on automatic fan speed control.\n"
	"  backlight <enabled>\n"
	"      Enable/disable LCD backlight\n"
	"  battery\n"
	"      Prints battery info\n"
	"  batterycutoff [at-shutdown]\n"
	"      Cut off battery output power\n"
	"  batteryparam\n"
	"      Read or write board-specific battery parameter\n"
	"  boardversion\n"
	"      Prints the board version\n"
	"  chargecurrentlimit\n"
	"      Set the maximum battery charging current\n"
	"  chargecontrol\n"
	"      Force the battery to stop charging or discharge\n"
	"  chargeoverride\n"
	"      Overrides charge port selection logic\n"
	"  chargestate\n"
	"      Handle commands related to charge state v2 (and later)\n"
	"  chipinfo\n"
	"      Prints chip info\n"
	"  cmdversions <cmd>\n"
	"      Prints supported version mask for a command number\n"
	"  console\n"
	"      Prints the last output to the EC debug console\n"
	"  echash [CMDS]\n"
	"      Various EC hash commands\n"
	"  eventclear <mask>\n"
	"      Clears EC host events flags where mask has bits set\n"
	"  eventclearb <mask>\n"
	"      Clears EC host events flags copy B where mask has bits set\n"
	"  eventget\n"
	"      Prints raw EC host event flags\n"
	"  eventgetb\n"
	"      Prints raw EC host event flags copy B\n"
	"  eventgetscimask\n"
	"      Prints SCI mask for EC host events\n"
	"  eventgetsmimask\n"
	"      Prints SMI mask for EC host events\n"
	"  eventgetwakemask\n"
	"      Prints wake mask for EC host events\n"
	"  eventsetscimask <mask>\n"
	"      Sets the SCI mask for EC host events\n"
	"  eventsetsmimask <mask>\n"
	"      Sets the SMI mask for EC host events\n"
	"  eventsetwakemask <mask>\n"
	"      Sets the wake mask for EC host events\n"
	"  fanduty <percent>\n"
	"      Forces the fan PWM to a constant duty cycle\n"
	"  flasherase <offset> <size>\n"
	"      Erases EC flash\n"
	"  flashinfo\n"
	"      Prints information on the EC flash\n"
	"  flashpd <dev_id> <port> <filename>\n"
	"      Flash commands over PD\n"
	"  flashprotect [now] [enable | disable]\n"
	"      Prints or sets EC flash protection state\n"
	"  flashread <offset> <size> <outfile>\n"
	"      Reads from EC flash to a file\n"
	"  flashwrite <offset> <infile>\n"
	"      Writes to EC flash from a file\n"
	"  forcelidopen <enable>\n"
	"      Forces the lid switch to open position\n"
	"  gpioget <GPIO name>\n"
	"      Get the value of GPIO signal\n"
	"  gpioset <GPIO name>\n"
	"      Set the value of GPIO signal\n"
	"  hangdetect <flags> <event_msec> <reboot_msec> | stop | start\n"
	"      Configure or start/stop the hang detect timer\n"
	"  hello\n"
	"      Checks for basic communication with EC\n"
	"  kbpress\n"
	"      Simulate key press\n"
	"  i2cread\n"
	"      Read I2C bus\n"
	"  i2cwrite\n"
	"      Write I2C bus\n"
	"  i2cxfer <port> <slave_addr> <read_count> [write bytes...]\n"
	"      Perform I2C transfer on EC's I2C bus\n"
	"  infopddev <port>\n"
	"      Get info about USB type-C accessory attached to port\n"
	"  inventory\n"
	"      Return the list of supported features\n"
	"  keyscan <beat_us> <filename>\n"
	"      Test low-level key scanning\n"
	"  led <name> <query | auto | off | <color> | <color>=<value>...>\n"
	"      Set the color of an LED or query brightness range\n"
	"  lightbar [CMDS]\n"
	"      Various lightbar control commands\n"
	"  motionsense [CMDS]\n"
	"      Various motion sense control commands\n"
	"  panicinfo\n"
	"      Prints saved panic info\n"
	"  pause_in_s5 [on|off]\n"
	"      Whether or not the AP should pause in S5 on shutdown\n"
	"  pdlog\n"
	"      Prints the PD event log entries\n"
	"  pdwritelog <type> <port>\n"
	"      Writes a PD event log of the given <type>\n"
	"  pdgetmode <port>\n"
	"      Get All USB-PD alternate SVIDs and modes on <port>\n"
	"  pdsetmode <port> <svid> <opos>\n"
	"      Set USB-PD alternate SVID and mode on <port>\n"
	"  port80flood\n"
	"      Rapidly write bytes to port 80\n"
	"  port80read\n"
	"      Print history of port 80 write\n"
	"  powerinfo\n"
	"	Prints power-related information\n"
	"  protoinfo\n"
	"       Prints EC host protocol information\n"
	"  pstoreinfo\n"
	"      Prints information on the EC host persistent storage\n"
	"  pstoreread <offset> <size> <outfile>\n"
	"      Reads from EC host persistent storage to a file\n"
	"  pstorewrite <offset> <infile>\n"
	"      Writes to EC host persistent storage from a file\n"
	"  pwmgetfanrpm [<index> | all]\n"
	"      Prints current fan RPM\n"
	"  pwmgetkblight\n"
	"      Prints current keyboard backlight percent\n"
	"  pwmgetnumfans\n"
	"      Prints the number of fans present\n"
	"  pwmsetfanrpm <targetrpm>\n"
	"      Set target fan RPM\n"
	"  pwmsetkblight <percent>\n"
	"      Set keyboard backlight in percent\n"
	"  readtest <patternoffset> <size>\n"
	"      Reads a pattern from the EC via LPC\n"
	"  reboot_ec <RO|RW|cold|hibernate|disable-jump> [at-shutdown]\n"
	"      Reboot EC to RO or RW\n"
	"  rtcget\n"
	"      Print real-time clock\n"
	"  rtcset <time>\n"
	"      Set real-time clock\n"
	"  rwhashpd <dev_id> <HASH[0] ... <HASH[4]>\n"
	"      Set entry in PD MCU's device rw_hash table.\n"
	"  sertest\n"
	"      Serial output test for COM2\n"
	"  switches\n"
	"      Prints current EC switch positions\n"
	"  temps <sensorid>\n"
	"      Print temperature.\n"
	"  tempsinfo <sensorid>\n"
	"      Print temperature sensor info.\n"
	"  thermalget <platform-specific args>\n"
	"      Get the threshold temperature values from the thermal engine.\n"
	"  thermalset <platform-specific args>\n"
	"      Set the threshold temperature values for the thermal engine.\n"
	"  tmp006cal <tmp006_index> [params...]\n"
	"      Get/set TMP006 calibration\n"
	"  tmp006raw <tmp006_index>\n"
	"      Get raw TMP006 data\n"
	"  usbchargemode <port> <mode>\n"
	"      Set USB charging mode\n"
	"  usbmux <mux>\n"
	"      Set USB mux switch state\n"
	"  usbpd <port> <auto | "
			"[toggle|toggle-off|sink|source] [none|usb|dp|dock]>\n"
	"      Control USB PD/type-C\n"
	"  usbpdpower\n"
	"      Get USB PD power information\n"
	"  version\n"
	"      Prints EC version\n"
	"  wireless <flags> [<mask> [<suspend_flags> <suspend_mask>]]\n"
	"      Enable/disable WLAN/Bluetooth radio\n"
	"";

/* Note: depends on enum system_image_copy_t */
static const char * const image_names[] = {"unknown", "RO", "RW"};

/* Note: depends on enum ec_led_colors */
static const char * const led_color_names[EC_LED_COLOR_COUNT] = {
	"red", "green", "blue", "yellow", "white"};

/* Note: depends on enum ec_led_id */
static const char * const led_names[EC_LED_ID_COUNT] = {
	"battery", "power", "adapter"};

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

void print_help(const char *prog, int print_cmds)
{
	printf("Usage: %s [--dev=n] [--interface=dev|lpc|i2c] ", prog);
	printf("[--name=cros_ec|cros_sh|cros_pd] <command> [params]\n\n");
	if (print_cmds)
		puts(help_str);
	else
		printf("Use '%s help' to print a list of commands.\n", prog);
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

int cmd_test(int argc, char *argv[])
{
	struct ec_params_test_protocol p = {
		.buf = "0123456789abcdef0123456789ABCDEF"
	};
	struct ec_response_test_protocol r;
	int rv, version = 0;
	char *e;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s result length [version]\n",
			argv[0]);
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

	rv = ec_command(EC_CMD_TEST_PROTOCOL, version,
			&p, sizeof(p), &r, sizeof(r));
	printf("rv = %d\n", rv);

	return rv;
}

int cmd_s5(int argc, char *argv[])
{
	struct ec_params_get_set_value p;
	struct ec_params_get_set_value r;
	int rv;

	p.flags = 0;

	if (argc > 1) {
		p.flags |= EC_GSV_SET;
		if (!parse_bool(argv[1], &p.value)) {
			fprintf(stderr, "invalid arg \"%s\"\n", argv[1]);
			return -1;
		}
	}

	rv = ec_command(EC_CMD_GSV_PAUSE_IN_S5, 0,
			&p, sizeof(p), &r, sizeof(r));
	if (rv > 0)
		printf("%s\n", r.value ? "on" : "off");

	return rv < 0;
}

static const char * const ec_feature_names[] = {
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
	[EC_FEATURE_I2C] = "I2C master",
	[EC_FEATURE_CHARGER] = "Charger",
	[EC_FEATURE_BATTERY] = "Simple Battery",
	[EC_FEATURE_SMART_BATTERY] = "Smart Battery",
	[EC_FEATURE_HANG_DETECT] = "Host hang detection",
	[EC_FEATURE_PMU] = "Power Management",
	[EC_FEATURE_SUB_MCU] = "Control downstream MCU",
	[EC_FEATURE_USB_PD] = "USB Cros Power Delievery",
	[EC_FEATURE_USB_MUX] = "USB Multiplexer",
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
			if (r.flags[i] & (1 << j)) {
				if (idx >= ARRAY_SIZE(ec_feature_names) ||
				    strlen(ec_feature_names[idx]) == 0)
					printf("%-4d: Unknown feature\n", idx);
				else
					printf("%-4d: %s support\n",
					       idx, ec_feature_names[idx]);
			}
		}
	}
	return 0;
}


int cmd_cmdversions(int argc, char *argv[])
{
	struct ec_params_get_cmd_versions p;
	struct ec_response_get_cmd_versions r;
	char *e;
	int cmd;
	int rv;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <cmd>\n", argv[0]);
		return -1;
	}
	cmd = strtol(argv[1], &e, 0);
	if ((e && *e) || cmd < 0 || cmd > 0xff) {
		fprintf(stderr, "Bad command number.\n");
		return -1;
	}

	p.cmd = cmd;
	rv = ec_command(EC_CMD_GET_CMD_VERSIONS, 0, &p, sizeof(p),
			&r, sizeof(r));
	if (rv < 0) {
		if (rv == -EC_RES_INVALID_PARAM)
			printf("Command 0x%02x not supported by EC.\n", cmd);

		return rv;
	}

	printf("Command 0x%02x supports version mask 0x%08x\n",
	       cmd, r.version_mask);
	return 0;
}

int cmd_version(int argc, char *argv[])
{
	struct ec_response_get_version r;
	char *build_string = (char *)ec_inbuf;
	int rv;

	rv = ec_command(EC_CMD_GET_VERSION, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0) {
		fprintf(stderr, "ERROR: EC_CMD_GET_VERSION failed: %d\n", rv);
		return rv;
	}
	rv = ec_command(EC_CMD_GET_BUILD_INFO, 0,
			NULL, 0, ec_inbuf, ec_max_insize);
	if (rv < 0) {
		fprintf(stderr, "ERROR: EC_CMD_GET_BUILD_INFO failed: %d\n",
				rv);
		return rv;
	}

	/* Ensure versions are null-terminated before we print them */
	r.version_string_ro[sizeof(r.version_string_ro) - 1] = '\0';
	r.version_string_rw[sizeof(r.version_string_rw) - 1] = '\0';
	build_string[ec_max_insize - 1] = '\0';

	/* Print versions */
	printf("RO version:    %s\n", r.version_string_ro);
	printf("RW version:    %s\n", r.version_string_rw);
	printf("Firmware copy: %s\n",
	       (r.current_image < ARRAY_SIZE(image_names) ?
		image_names[r.current_image] : "?"));
	printf("Build info:    %s\n", build_string);

	return 0;
}


int cmd_read_test(int argc, char *argv[])
{
	struct ec_params_read_test p;
	struct ec_response_read_test r;
	int offset, size;
	int errors = 0;
	int rv;
	int i;
	char *e;
	char *buf;
	uint32_t *b;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <pattern_offset> <size>\n", argv[0]);
		return -1;
	}
	offset = strtol(argv[1], &e, 0);
	size = strtol(argv[2], &e, 0);
	if ((e && *e) || size <= 0 || size > 0x100000) {
		fprintf(stderr, "Bad size.\n");
		return -1;
	}
	printf("Reading %d bytes with pattern offset 0x%x...\n", size, offset);

	buf = (char *)malloc(size);
	if (!buf) {
		fprintf(stderr, "Unable to allocate buffer.\n");
		return -1;
	}

	/* Read data in chunks */
	for (i = 0; i < size; i += sizeof(r.data)) {
		p.offset = offset + i / sizeof(uint32_t);
		p.size = MIN(size - i, sizeof(r.data));
		rv = ec_command(EC_CMD_READ_TEST, 0, &p, sizeof(p),
				&r, sizeof(r));
		if (rv < 0) {
			fprintf(stderr, "Read error at offset %d\n", i);
			free(buf);
			return rv;
		}
		memcpy(buf + i, r.data, p.size);
	}

	/* Check data */
	for (i = 0, b = (uint32_t *)buf; i < size / 4; i++, b++) {
		if (*b != i + offset) {
			printf("Mismatch at byte offset 0x%x: "
			       "expected 0x%08x, got 0x%08x\n",
			       (int)(i * sizeof(uint32_t)), i + offset, *b);
			errors++;
		}
	}

	free(buf);
	if (errors) {
		printf("Found %d errors\n", errors);
		return -1;
	}

	printf("done.\n");
	return 0;
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
	else if (!strcmp(argv[1], "RW") || !strcmp(argv[1], "A")) {
		/*
		 * TODO(crosbug.com/p/11149): remove "A" once all scripts are
		 * updated to use "RW".
		 */
		p.cmd = EC_REBOOT_JUMP_RW;
	} else if (!strcmp(argv[1], "cold"))
		p.cmd = EC_REBOOT_COLD;
	else if (!strcmp(argv[1], "disable-jump"))
		p.cmd = EC_REBOOT_DISABLE_JUMP;
	else if (!strcmp(argv[1], "hibernate"))
		p.cmd = EC_REBOOT_HIBERNATE;
	else {
		fprintf(stderr, "Unknown command: %s\n", argv[1]);
		return -1;
	}

	/* Parse flags, if any */
	p.flags = 0;
	for (i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "at-shutdown"))
			p.flags |= EC_REBOOT_FLAG_ON_AP_SHUTDOWN;
		else {
			fprintf(stderr, "Unknown flag: %s\n", argv[i]);
			return -1;
		}
	}

	rv = ec_command(EC_CMD_REBOOT_EC, 0, &p, sizeof(p), NULL, 0);
	return (rv < 0 ? rv : 0);
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
		printf("WriteIdealSize %d\nFlags 0x%x\n",
		       r.write_ideal_size, r.flags);
	}

	return 0;
}

int cmd_flash_read(int argc, char *argv[])
{
	int offset, size;
	int rv;
	char *e;
	char *buf;

	if (argc < 4) {
		fprintf(stderr,
			"Usage: %s <offset> <size> <filename>\n", argv[0]);
		return -1;
	}
	offset = strtol(argv[1], &e, 0);
	if ((e && *e) || offset < 0 || offset > 0x100000) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}
	size = strtol(argv[2], &e, 0);
	if ((e && *e) || size <= 0 || size > 0x100000) {
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
	rv = ec_flash_read(buf, offset, size);
	if (rv < 0) {
		free(buf);
		return rv;
	}

	rv = write_file(argv[3], buf, size);
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
	if ((e && *e) || offset < 0 || offset > 0x100000) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}

	/* Read the input file */
	buf = read_file(argv[2], &size);
	if (!buf)
		return -1;

	printf("Writing to offset %d...\n", offset);

	/* Write data in chunks */
	rv = ec_flash_write(buf, offset, size);

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

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <offset> <size>\n", argv[0]);
		return -1;
	}

	offset = strtol(argv[1], &e, 0);
	if ((e && *e) || offset < 0 || offset > 0x100000) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}

	size = strtol(argv[2], &e, 0);
	if ((e && *e) || size <= 0 || size > 0x100000) {
		fprintf(stderr, "Bad size.\n");
		return -1;
	}

	printf("Erasing %d bytes at offset %d...\n", size, offset);
	rv = ec_flash_erase(offset, size);
	if (rv < 0)
		return rv;

	printf("done.\n");
	return 0;
}


static void print_flash_protect_flags(const char *desc, uint32_t flags)
{
	printf("%s 0x%08x", desc, flags);
	if (flags & EC_FLASH_PROTECT_GPIO_ASSERTED)
		printf(" wp_gpio_asserted");
	if (flags & EC_FLASH_PROTECT_RO_AT_BOOT)
		printf(" ro_at_boot");
	if (flags & EC_FLASH_PROTECT_ALL_AT_BOOT)
		printf(" all_at_boot");
	if (flags & EC_FLASH_PROTECT_RO_NOW)
		printf(" ro_now");
	if (flags & EC_FLASH_PROTECT_ALL_NOW)
		printf(" all_now");
	if (flags & EC_FLASH_PROTECT_ERROR_STUCK)
		printf(" STUCK");
	if (flags & EC_FLASH_PROTECT_ERROR_INCONSISTENT)
		printf(" INCONSISTENT");
	printf("\n");
}


int cmd_flash_protect(int argc, char *argv[])
{
	struct ec_params_flash_protect p;
	struct ec_response_flash_protect r;
	int rv, i;

	/*
	 * Set up requested flags.  If no flags were specified, p.mask will
	 * be 0 and nothing will change.
	 */
	p.mask = p.flags = 0;
	for (i = 1; i < argc; i++) {
		if (!strcasecmp(argv[i], "now")) {
			p.mask |= EC_FLASH_PROTECT_ALL_NOW;
			p.flags |= EC_FLASH_PROTECT_ALL_NOW;
		} else if (!strcasecmp(argv[i], "enable")) {
			p.mask |= EC_FLASH_PROTECT_RO_AT_BOOT;
			p.flags |= EC_FLASH_PROTECT_RO_AT_BOOT;
		} else if (!strcasecmp(argv[i], "disable"))
			p.mask |= EC_FLASH_PROTECT_RO_AT_BOOT;
	}

	rv = ec_command(EC_CMD_FLASH_PROTECT, EC_VER_FLASH_PROTECT,
			&p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;
	if (rv < sizeof(r)) {
		fprintf(stderr, "Too little data returned.\n");
		return -1;
	}

	/* Print returned flags */
	print_flash_protect_flags("Flash protect flags:", r.flags);
	print_flash_protect_flags("Valid flags:        ", r.valid_flags);
	print_flash_protect_flags("Writable flags:     ", r.writable_flags);

	/* Check if we got all the flags we asked for */
	if ((r.flags & p.mask) != (p.flags & p.mask)) {
		fprintf(stderr, "Unable to set requested flags "
			"(wanted mask 0x%08x flags 0x%08x)\n",
			p.mask, p.flags);
		if (p.mask & ~r.writable_flags)
			fprintf(stderr, "Which is expected, because writable "
				"mask is 0x%08x.\n", r.writable_flags);

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
		rwp[0] = (uint8_t)  (val >> 0) & 0xff;
		rwp[1] = (uint8_t)  (val >> 8) & 0xff;
		rwp[2] = (uint8_t) (val >> 16) & 0xff;
		rwp[3] = (uint8_t) (val >> 24) & 0xff;
		rwp += 4;
	}
	rv = ec_command(EC_CMD_USB_PD_RW_HASH_ENTRY, 0, p, sizeof(*p), NULL, 0);

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
		ec_command(EC_CMD_USB_PD_GET_AMODE, 0, p, sizeof(*p),
			   ec_inbuf, ec_max_insize);
		if (!r->svid || (r->svid == USB_VID_GOOGLE))
			break;
		p->svid_idx++;
	} while (p->svid_idx < SVID_DISCOVERY_MAX);

	if (r->svid != USB_VID_GOOGLE) {
		fprintf(stderr, "Google VID not returned\n");
		return -1;
	}

	*opos = 0; /* invalid ... must be 1 thru 6 */
	for (i = 0; i < PDO_MODES; i++) {
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

		ec_command(EC_CMD_USB_PD_SET_AMODE, 0, p, sizeof(*p),
			   NULL, 0);
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
	rv = ec_command(EC_CMD_USB_PD_DISCOVERY, 0, p, sizeof(*p),
			ec_inbuf, ec_max_insize);
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
	rv = ec_command(EC_CMD_USB_PD_DEV_INFO, 0, p, sizeof(*p),
			ec_inbuf, ec_max_insize);
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
	uint32_t *data = &(p->size) + 1;

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
	rv = ec_command(EC_CMD_USB_PD_FW_UPDATE, 0,
			p, p->size + sizeof(*p), NULL, 0);

	if (rv < 0)
		goto pd_flash_error;

	/* Reboot */
	fprintf(stderr, "Rebooting\n");
	p->dev_id = dev_id;
	p->port = port;
	p->cmd = USB_PD_FW_REBOOT;
	p->size = 0;
	rv = ec_command(EC_CMD_USB_PD_FW_UPDATE, 0,
			p, p->size + sizeof(*p), NULL, 0);

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
	rv = ec_command(EC_CMD_USB_PD_FW_UPDATE, 0,
			p, p->size + sizeof(*p), NULL, 0);

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
		rv = ec_command(EC_CMD_USB_PD_FW_UPDATE, 0,
				p, p->size + sizeof(*p), NULL, 0);
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
	rv = ec_command(EC_CMD_USB_PD_FW_UPDATE, 0,
			p, p->size + sizeof(*p), NULL, 0);

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
		ec_command(EC_CMD_USB_PD_GET_AMODE, 0, p, sizeof(*p),
			   ec_inbuf, ec_max_insize);
		if (!r->svid)
			break;
		printf("%cSVID:0x%04x ", (r->opos) ? '*' : ' ',
		       r->svid);
		for (i = 0; i < PDO_MODES; i++) {
			printf("%c0x%08x ", (r->opos && (r->opos == i + 1)) ?
			       '*' : ' ', r->vdo[i]);
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
		rv = read_mapped_mem8(EC_MEMMAP_TEMP_SENSOR_B +
				      id - EC_TEMP_SENSOR_ENTRIES);
	else {
		/* Sensor in second bank, but second bank isn't supported */
		rv = EC_TEMP_SENSOR_NOT_PRESENT;
	}
	return rv;
}


int cmd_temperature(int argc, char *argv[])
{
	int rv;
	int id;
	char *e;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <sensorid> | all\n", argv[0]);
		return -1;
	}

	if (strcmp(argv[1], "all") == 0) {
		for (id = 0;
		     id < EC_TEMP_SENSOR_ENTRIES + EC_TEMP_SENSOR_B_ENTRIES;
		     id++) {
			rv = read_mapped_temperature(id);
			switch (rv) {
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
				printf("%d: %d\n", id,
				       rv + EC_TEMP_SENSOR_OFFSET);
			}
		}
		return 0;
	}

	id = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad sensor ID.\n");
		return -1;
	}

	if (id < 0 ||
	    id >= EC_TEMP_SENSOR_ENTRIES + EC_TEMP_SENSOR_B_ENTRIES) {
		printf("Sensor ID invalid.\n");
		return -1;
	}

	printf("Reading temperature...");
	rv = read_mapped_temperature(id);

	switch (rv) {
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
		printf("%d\n", rv + EC_TEMP_SENSOR_OFFSET);
		return 0;
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
		for (p.id = 0;
		     p.id < EC_TEMP_SENSOR_ENTRIES + EC_TEMP_SENSOR_B_ENTRIES;
		     p.id++) {
			rv = ec_command(EC_CMD_TEMP_SENSOR_GET_INFO, 0,
					&p, sizeof(p), &r, sizeof(r));
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

	rv = ec_command(EC_CMD_TEMP_SENSOR_GET_INFO, 0,
			&p, sizeof(p), &r, sizeof(r));
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
		fprintf(stderr,
			"Usage: %s <sensortypeid> <thresholdid>\n", argv[0]);
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

	rv = ec_command(EC_CMD_THERMAL_GET_THRESHOLD, 0,
			&p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;

	if (r.value < 0)
		return -1;

	printf("Threshold %d for sensor type %d is %d K.\n",
			p.threshold_id, p.sensor_type, r.value);

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

	rv = ec_command(EC_CMD_THERMAL_SET_THRESHOLD, 0,
			&p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("Threshold %d for sensor type %d set to %d.\n",
			p.threshold_id, p.sensor_type, p.value);

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
	for (i = 0; i < 99; i++) {	/* number of sensors is unknown */

		/* ask for one */
		p.sensor_num = i;
		rv = ec_command(EC_CMD_THERMAL_GET_THRESHOLD, 1,
				&p, sizeof(p), &r, sizeof(r));
		if (rv <= 0)		/* stop on first failure */
			break;

		/* ask for its name, too */
		pi.id = i;
		rv = ec_command(EC_CMD_TEMP_SENSOR_GET_INFO, 0,
				&pi, sizeof(pi), &ri, sizeof(ri));

		/* print what we know */
		printf(" %2d      %3d   %3d    %3d    %3d     %3d     %s\n",
		       i,
		       r.temp_host[EC_TEMP_THRESH_WARN],
		       r.temp_host[EC_TEMP_THRESH_HIGH],
		       r.temp_host[EC_TEMP_THRESH_HALT],
		       r.temp_fan_off, r.temp_fan_max,
		       rv > 0 ? ri.sensor_name : "?");
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
	rv = ec_command(EC_CMD_THERMAL_GET_THRESHOLD, 1,
			&p, sizeof(p), &r, sizeof(r));
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
			s.cfg.temp_host[i-2] = val;
			break;
		case 5:
			s.cfg.temp_fan_off = val;
			break;
		case 6:
			s.cfg.temp_fan_max = val;
			break;
		}
	}

	rv = ec_command(EC_CMD_THERMAL_SET_THRESHOLD, 1,
			&s, sizeof(s), NULL, 0);

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

	if (!ec_cmd_version_supported(EC_CMD_THERMAL_AUTO_FAN_CTRL, cmdver)
	    || (argc == 1)) {
		/* If no argument is provided then enable auto fan ctrl */
		/* for all fans by using version 0 of the host command */

		rv = ec_command(EC_CMD_THERMAL_AUTO_FAN_CTRL, 0,
				NULL, 0, NULL, 0);
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

	rv = ec_command(EC_CMD_THERMAL_AUTO_FAN_CTRL, cmdver,
			&p_v1, sizeof(p_v1), NULL, 0);
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
	case EC_FAN_SPEED_STALLED:
		printf("Fan %d stalled!\n", idx);
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
			fprintf(stderr,
				"Usage: %s <targetrpm>\n", argv[0]);
			return -1;
		}
		p_v0.rpm = strtol(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad RPM.\n");
			return -1;
		}

		rv = ec_command(EC_CMD_PWM_SET_FAN_TARGET_RPM, cmdver,
				&p_v0, sizeof(p_v0), NULL, 0);
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

		rv = ec_command(EC_CMD_PWM_SET_FAN_TARGET_RPM, cmdver,
				&p_v0, sizeof(p_v0), NULL, 0);
		if (rv < 0)
			return rv;

		printf("Fan target RPM set for all fans.\n");
	} else {
		p_v1.fan_idx = strtol(argv[1], &e, 0);
		if ((e && *e) || (p_v1.fan_idx >= num_fans)) {
			fprintf(stderr, "Bad fan index.\n");
			return -1;
		}

		rv = ec_command(EC_CMD_PWM_SET_FAN_TARGET_RPM, cmdver,
				&p_v1, sizeof(p_v1), NULL, 0);
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

	rv = ec_command(EC_CMD_PWM_GET_KEYBOARD_BACKLIGHT, 0,
			NULL, 0, &r, sizeof(r));
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

	rv = ec_command(EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT, 0,
			&p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("Keyboard backlight set.\n");
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
			fprintf(stderr,
				"Usage: %s <percent>\n", argv[0]);
			return -1;
		}
		p_v0.percent = strtol(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad percent arg.\n");
			return -1;
		}

		rv = ec_command(EC_CMD_PWM_SET_FAN_DUTY, 0,
				&p_v0, sizeof(p_v0), NULL, 0);
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

		rv = ec_command(EC_CMD_PWM_SET_FAN_DUTY, cmdver,
				&p_v0, sizeof(p_v0), NULL, 0);
		if (rv < 0)
			return rv;

		printf("Fan duty cycle set for all fans.\n");
	} else {
		p_v1.fan_idx = strtol(argv[1], &e, 0);
		if ((e && *e) || (p_v1.fan_idx >= num_fans)) {
			fprintf(stderr, "Bad fan index.\n");
			return -1;
		}

		rv = ec_command(EC_CMD_PWM_SET_FAN_DUTY, cmdver,
				&p_v1, sizeof(p_v1), NULL, 0);
		if (rv < 0)
			return rv;

		printf("Fan %d duty cycle set.\n", p_v1.fan_idx);
	}

	return 0;
}

#define LBMSG(state) #state
#include "lightbar_msg_list.h"
static const char * const lightbar_cmds[] = {
	LIGHTBAR_MSG_LIST
};
#undef LBMSG

/* This needs to match the values defined in lightbar.h. I'd like to
 * define this in one and only one place, but I can't think of a good way to do
 * that without adding bunch of complexity. This will do for now.
 */
#define LB_SIZES(SUBCMD) { \
		sizeof(((struct ec_params_lightbar *)0)->SUBCMD) \
		+ sizeof(((struct ec_params_lightbar *)0)->cmd), \
		sizeof(((struct ec_response_lightbar *)0)->SUBCMD) }
static const struct {
	uint8_t insize;
	uint8_t outsize;
} lb_command_paramcount[] = {
	LB_SIZES(dump),
	LB_SIZES(off),
	LB_SIZES(on),
	LB_SIZES(init),
	LB_SIZES(set_brightness),
	LB_SIZES(seq),
	LB_SIZES(reg),
	LB_SIZES(set_rgb),
	LB_SIZES(get_seq),
	LB_SIZES(demo),
	LB_SIZES(get_params_v0),
	LB_SIZES(set_params_v0),
	LB_SIZES(version),
	LB_SIZES(get_brightness),
	LB_SIZES(get_rgb),
	LB_SIZES(get_demo),
	LB_SIZES(get_params_v1),
	LB_SIZES(set_params_v1),
	LB_SIZES(set_program),
	LB_SIZES(manual_suspend_ctrl),
	LB_SIZES(suspend),
	LB_SIZES(resume),
	LB_SIZES(get_params_v2_timing),
	LB_SIZES(set_v2par_timing),
	LB_SIZES(get_params_v2_tap),
	LB_SIZES(set_v2par_tap),
	LB_SIZES(get_params_v2_osc),
	LB_SIZES(set_v2par_osc),
	LB_SIZES(get_params_v2_bright),
	LB_SIZES(set_v2par_bright),
	LB_SIZES(get_params_v2_thlds),
	LB_SIZES(set_v2par_thlds),
	LB_SIZES(get_params_v2_colors),
	LB_SIZES(set_v2par_colors),
};
#undef LB_SIZES

static int lb_help(const char *cmd)
{
	printf("Usage:\n");
	printf("  %s                         - dump all regs\n", cmd);
	printf("  %s off                     - enter standby\n", cmd);
	printf("  %s on                      - leave standby\n", cmd);
	printf("  %s init                    - load default vals\n", cmd);
	printf("  %s brightness [NUM]        - get/set intensity(0-ff)\n", cmd);
	printf("  %s seq [NUM|SEQUENCE]      - run given pattern"
		 " (no arg for list)\n", cmd);
	printf("  %s CTRL REG VAL            - set LED controller regs\n", cmd);
	printf("  %s LED RED GREEN BLUE      - set color manually"
		 " (LED=4 for all)\n", cmd);
	printf("  %s LED                     - get current LED color\n", cmd);
	printf("  %s demo [0|1]              - turn demo mode on & off\n", cmd);
	printf("  %s params [setfile]        - get params"
	       " (or set from file)\n", cmd);
	printf("  %s params2 group [setfile] - get params by group\n"
	       " (or set from file)\n", cmd);
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

static int lb_do_cmd(enum lightbar_command cmd,
		     struct ec_params_lightbar *in,
		     struct ec_response_lightbar *out)
{
	int rv;
	in->cmd = cmd;
	rv = ec_command(EC_CMD_LIGHTBAR_CMD, 0,
			in, lb_command_paramcount[cmd].insize,
			out, lb_command_paramcount[cmd].outsize);
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
		fprintf(stderr, "Can't open %s: %s\n",
			filename, strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N) do {							\
		line++;							\
		want = (N);						\
		got = -1;						\
		if (!fgets(buf, sizeof(buf), fp))			\
			goto done;					\
		got = sscanf(buf, "%i %i %i %i",			\
			     &val[0], &val[1], &val[2], &val[3]);	\
		if (want != got)					\
			goto done;					\
	} while (0)


	/* Do it */
	READ(1); p->google_ramp_up = val[0];
	READ(1); p->google_ramp_down = val[0];
	READ(1); p->s3s0_ramp_up = val[0];
	READ(1); p->s0_tick_delay[0] = val[0];
	READ(1); p->s0_tick_delay[1] = val[0];
	READ(1); p->s0a_tick_delay[0] = val[0];
	READ(1); p->s0a_tick_delay[1] = val[0];
	READ(1); p->s0s3_ramp_down = val[0];
	READ(1); p->s3_sleep_for = val[0];
	READ(1); p->s3_ramp_up = val[0];
	READ(1); p->s3_ramp_down = val[0];
	READ(1); p->new_s0 = val[0];

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
	printf("0x%02x 0x%02x\t# .osc_min (battery, AC)\n",
	       p->osc_min[0], p->osc_min[1]);
	printf("0x%02x 0x%02x\t# .osc_max (battery, AC)\n",
	       p->osc_max[0], p->osc_max[1]);
	printf("%d %d\t\t# .w_ofs (battery, AC)\n",
	       p->w_ofs[0], p->w_ofs[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_off_fixed (battery, AC)\n",
	       p->bright_bl_off_fixed[0], p->bright_bl_off_fixed[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_on_min (battery, AC)\n",
	       p->bright_bl_on_min[0], p->bright_bl_on_min[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_on_max (battery, AC)\n",
	       p->bright_bl_on_max[0], p->bright_bl_on_max[1]);
	printf("%d %d %d\t\t# .battery_threshold\n",
	       p->battery_threshold[0],
	       p->battery_threshold[1],
	       p->battery_threshold[2]);
	printf("%d %d %d %d\t\t# .s0_idx[] (battery)\n",
	       p->s0_idx[0][0], p->s0_idx[0][1],
	       p->s0_idx[0][2], p->s0_idx[0][3]);
	printf("%d %d %d %d\t\t# .s0_idx[] (AC)\n",
	       p->s0_idx[1][0], p->s0_idx[1][1],
	       p->s0_idx[1][2], p->s0_idx[1][3]);
	printf("%d %d %d %d\t# .s3_idx[] (battery)\n",
	       p->s3_idx[0][0], p->s3_idx[0][1],
	       p->s3_idx[0][2], p->s3_idx[0][3]);
	printf("%d %d %d %d\t# .s3_idx[] (AC)\n",
	       p->s3_idx[1][0], p->s3_idx[1][1],
	       p->s3_idx[1][2], p->s3_idx[1][3]);
	for (i = 0; i < ARRAY_SIZE(p->color); i++)
		printf("0x%02x 0x%02x 0x%02x\t# color[%d]\n",
		       p->color[i].r,
		       p->color[i].g,
		       p->color[i].b, i);
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
		fprintf(stderr, "Can't open %s: %s\n",
			filename, strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N) do {							\
		line++;							\
		want = (N);						\
		got = -1;						\
		if (!fgets(buf, sizeof(buf), fp))			\
			goto done;					\
		got = sscanf(buf, "%i %i %i %i",			\
			     &val[0], &val[1], &val[2], &val[3]);	\
		if (want != got)					\
			goto done;					\
	} while (0)


	/* Do it */
	READ(1); p->google_ramp_up = val[0];
	READ(1); p->google_ramp_down = val[0];
	READ(1); p->s3s0_ramp_up = val[0];
	READ(1); p->s0_tick_delay[0] = val[0];
	READ(1); p->s0_tick_delay[1] = val[0];
	READ(1); p->s0a_tick_delay[0] = val[0];
	READ(1); p->s0a_tick_delay[1] = val[0];
	READ(1); p->s0s3_ramp_down = val[0];
	READ(1); p->s3_sleep_for = val[0];
	READ(1); p->s3_ramp_up = val[0];
	READ(1); p->s3_ramp_down = val[0];
	READ(1); p->tap_tick_delay = val[0];
	READ(1); p->tap_gate_delay = val[0];
	READ(1); p->tap_display_time = val[0];

	READ(1); p->tap_pct_red = val[0];
	READ(1); p->tap_pct_green = val[0];
	READ(1); p->tap_seg_min_on = val[0];
	READ(1); p->tap_seg_max_on = val[0];
	READ(1); p->tap_seg_osc = val[0];
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
	printf("%d %d %d\t\t# .tap_idx\n",
	       p->tap_idx[0], p->tap_idx[1], p->tap_idx[2]);
	printf("0x%02x 0x%02x\t# .osc_min (battery, AC)\n",
	       p->osc_min[0], p->osc_min[1]);
	printf("0x%02x 0x%02x\t# .osc_max (battery, AC)\n",
	       p->osc_max[0], p->osc_max[1]);
	printf("%d %d\t\t# .w_ofs (battery, AC)\n",
	       p->w_ofs[0], p->w_ofs[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_off_fixed (battery, AC)\n",
	       p->bright_bl_off_fixed[0], p->bright_bl_off_fixed[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_on_min (battery, AC)\n",
	       p->bright_bl_on_min[0], p->bright_bl_on_min[1]);
	printf("0x%02x 0x%02x\t# .bright_bl_on_max (battery, AC)\n",
	       p->bright_bl_on_max[0], p->bright_bl_on_max[1]);
	printf("%d %d %d\t# .battery_threshold\n",
	       p->battery_threshold[0],
	       p->battery_threshold[1],
	       p->battery_threshold[2]);
	printf("%d %d %d %d\t\t# .s0_idx[] (battery)\n",
	       p->s0_idx[0][0], p->s0_idx[0][1],
	       p->s0_idx[0][2], p->s0_idx[0][3]);
	printf("%d %d %d %d\t\t# .s0_idx[] (AC)\n",
	       p->s0_idx[1][0], p->s0_idx[1][1],
	       p->s0_idx[1][2], p->s0_idx[1][3]);
	printf("%d %d %d %d\t# .s3_idx[] (battery)\n",
	       p->s3_idx[0][0], p->s3_idx[0][1],
	       p->s3_idx[0][2], p->s3_idx[0][3]);
	printf("%d %d %d %d\t# .s3_idx[] (AC)\n",
	       p->s3_idx[1][0], p->s3_idx[1][1],
	       p->s3_idx[1][2], p->s3_idx[1][3]);
	for (i = 0; i < ARRAY_SIZE(p->color); i++)
		printf("0x%02x 0x%02x 0x%02x\t# color[%d]\n",
		       p->color[i].r,
		       p->color[i].g,
		       p->color[i].b, i);
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
		fprintf(stderr, "Can't open %s: %s\n",
			filename, strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N) do {							\
		line++;							\
		want = (N);						\
		got = -1;						\
		if (!fgets(buf, sizeof(buf), fp))			\
			goto done;					\
		got = sscanf(buf, "%i %i %i %i",			\
			     &val[0], &val[1], &val[2], &val[3]);	\
		if (want != got)					\
			goto done;					\
	} while (0)

	READ(1); p->google_ramp_up = val[0];
	READ(1); p->google_ramp_down = val[0];
	READ(1); p->s3s0_ramp_up = val[0];
	READ(1); p->s0_tick_delay[0] = val[0];
	READ(1); p->s0_tick_delay[1] = val[0];
	READ(1); p->s0a_tick_delay[0] = val[0];
	READ(1); p->s0a_tick_delay[1] = val[0];
	READ(1); p->s0s3_ramp_down = val[0];
	READ(1); p->s3_sleep_for = val[0];
	READ(1); p->s3_ramp_up = val[0];
	READ(1); p->s3_ramp_down = val[0];
	READ(1); p->tap_tick_delay = val[0];
	READ(1); p->tap_gate_delay = val[0];
	READ(1); p->tap_display_time = val[0];
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
		fprintf(stderr, "Can't open %s: %s\n",
			filename, strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N) do {							\
		line++;							\
		want = (N);						\
		got = -1;						\
		if (!fgets(buf, sizeof(buf), fp))			\
			goto done;					\
		got = sscanf(buf, "%i %i %i %i",			\
			     &val[0], &val[1], &val[2], &val[3]);	\
		if (want != got)					\
			goto done;					\
	} while (0)

	READ(1); p->tap_pct_red = val[0];
	READ(1); p->tap_pct_green = val[0];
	READ(1); p->tap_seg_min_on = val[0];
	READ(1); p->tap_seg_max_on = val[0];
	READ(1); p->tap_seg_osc = val[0];
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
		fprintf(stderr, "Can't open %s: %s\n",
			filename, strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N) do {							\
		line++;							\
		want = (N);						\
		got = -1;						\
		if (!fgets(buf, sizeof(buf), fp))			\
			goto done;					\
		got = sscanf(buf, "%i %i %i %i",			\
			     &val[0], &val[1], &val[2], &val[3]);	\
		if (want != got)					\
			goto done;					\
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
		fprintf(stderr, "Can't open %s: %s\n",
			filename, strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N) do {							\
		line++;							\
		want = (N);						\
		got = -1;						\
		if (!fgets(buf, sizeof(buf), fp))			\
			goto done;					\
		got = sscanf(buf, "%i %i %i %i",			\
			     &val[0], &val[1], &val[2], &val[3]);	\
		if (want != got)					\
			goto done;					\
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
		fprintf(stderr, "Can't open %s: %s\n",
			filename, strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N) do {							\
		line++;							\
		want = (N);						\
		got = -1;						\
		if (!fgets(buf, sizeof(buf), fp))			\
			goto done;					\
		got = sscanf(buf, "%i %i %i %i",			\
			     &val[0], &val[1], &val[2], &val[3]);	\
		if (want != got)					\
			goto done;					\
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
		fprintf(stderr, "Can't open %s: %s\n",
			filename, strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N) do {							\
		line++;							\
		want = (N);						\
		got = -1;						\
		if (!fgets(buf, sizeof(buf), fp))			\
			goto done;					\
		got = sscanf(buf, "%i %i %i %i",			\
			     &val[0], &val[1], &val[2], &val[3]);	\
		if (want != got)					\
			goto done;					\
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
	printf("%d %d %d\t\t# .tap_idx\n",
	       p->tap_idx[0], p->tap_idx[1], p->tap_idx[2]);
}

static void lb_show_v2par_osc(const struct lightbar_params_v2_oscillation *p)
{
	printf("0x%02x 0x%02x\t# .osc_min (battery, AC)\n",
	       p->osc_min[0], p->osc_min[1]);
	printf("0x%02x 0x%02x\t# .osc_max (battery, AC)\n",
	       p->osc_max[0], p->osc_max[1]);
	printf("%d %d\t\t# .w_ofs (battery, AC)\n",
	       p->w_ofs[0], p->w_ofs[1]);
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
	printf("%d %d %d\t# .battery_threshold\n",
	       p->battery_threshold[0],
	       p->battery_threshold[1],
	       p->battery_threshold[2]);
}

static void lb_show_v2par_colors(const struct lightbar_params_v2_colors *p)
{
	int i;

	printf("%d %d %d %d\t\t# .s0_idx[] (battery)\n",
	       p->s0_idx[0][0], p->s0_idx[0][1],
	       p->s0_idx[0][2], p->s0_idx[0][3]);
	printf("%d %d %d %d\t\t# .s0_idx[] (AC)\n",
	       p->s0_idx[1][0], p->s0_idx[1][1],
	       p->s0_idx[1][2], p->s0_idx[1][3]);
	printf("%d %d %d %d\t# .s3_idx[] (battery)\n",
	       p->s3_idx[0][0], p->s3_idx[0][1],
	       p->s3_idx[0][2], p->s3_idx[0][3]);
	printf("%d %d %d %d\t# .s3_idx[] (AC)\n",
	       p->s3_idx[1][0], p->s3_idx[1][1],
	       p->s3_idx[1][2], p->s3_idx[1][3]);

	for (i = 0; i < ARRAY_SIZE(p->color); i++)
		printf("0x%02x 0x%02x 0x%02x\t# color[%d]\n",
		       p->color[i].r,
		       p->color[i].g,
		       p->color[i].b, i);
}

static int lb_load_program(const char *filename, struct lightbar_program *prog)
{
	FILE *fp;
	size_t got;
	int rc;

	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open %s: %s\n",
			filename, strerror(errno));
		return 1;
	}

	rc = fseek(fp, 0, SEEK_END);
	if (rc) {
		fprintf(stderr, "Couldn't find end of file %s",
				filename);
		fclose(fp);
		return 1;
	}
	rc = (int) ftell(fp);
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
		r = lb_read_params_v0_from_file(argv[2],
						&param.set_params_v0);
		if (r)
			return r;
		return lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V0,
				 &param, &resp);
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
		r = lb_read_params_v1_from_file(argv[2],
						&param.set_params_v1);
		if (r)
			return r;
		return lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V1,
				 &param, &resp);
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
			r = lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V2_TIMING,
				      &p, &resp);
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
			r = lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V2_TAP,
				      &p, &resp);
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
			r = lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V2_BRIGHTNESS,
				      &p, &resp);
			if (r)
				return r;
		}
		r = lb_do_cmd(LIGHTBAR_CMD_GET_PARAMS_V2_BRIGHTNESS, &p,
			      &resp);
		if (r)
			return r;
		lb_show_v2par_bright(&resp.get_params_v2_bright);
	} else if (!strncasecmp(argv[2], "thresholds", 10)) {
		if (set) {
			r = lb_rd_thlds_v2par_from_file(argv[3],
							&p.set_v2par_thlds);
			if (r)
				return r;
			r = lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V2_THRESHOLDS,
				      &p, &resp);
			if (r)
				return r;
		}
		r = lb_do_cmd(LIGHTBAR_CMD_GET_PARAMS_V2_THRESHOLDS, &p,
			      &resp);
		if (r)
			return r;
		lb_show_v2par_thlds(&resp.get_params_v2_thlds);
	} else if (!strncasecmp(argv[2], "colors", 6)) {
		if (set) {
			r = lb_rd_colors_v2par_from_file(argv[3],
							 &p.set_v2par_colors);
			if (r)
				return r;
			r = lb_do_cmd(LIGHTBAR_CMD_SET_PARAMS_V2_COLORS,
				      &p, &resp);
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

	if (1 == argc) {		/* no args = dump 'em all */
		r = lb_do_cmd(LIGHTBAR_CMD_DUMP, &param, &resp);
		if (r)
			return r;
		for (i = 0; i < ARRAY_SIZE(resp.dump.vals); i++) {
			printf(" %02x     %02x     %02x\n",
			       resp.dump.vals[i].reg,
			       resp.dump.vals[i].ic0,
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
			printf("version %d flags 0x%x\n",
			       resp.version.num, resp.version.flags);
		return r;
	}

	if (argc > 1 && !strcasecmp(argv[1], "brightness")) {
		char *e;
		int rv;
		if (argc > 2) {
			param.set_brightness.num = 0xff &
				strtoul(argv[2], &e, 16);
			return lb_do_cmd(LIGHTBAR_CMD_SET_BRIGHTNESS,
					 &param, &resp);
		}
		rv = lb_do_cmd(LIGHTBAR_CMD_GET_BRIGHTNESS,
			       &param, &resp);
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
		num = 0xff & strtoul(argv[2], &e, 16);
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
		param.reg.ctrl = 0xff & strtoul(argv[1], &e, 16);
		param.reg.reg = 0xff & strtoul(argv[2], &e, 16);
		param.reg.value = 0xff & strtoul(argv[3], &e, 16);
		return lb_do_cmd(LIGHTBAR_CMD_REG, &param, &resp);
	}

	if (argc == 5) {
		char *e;
		param.set_rgb.led = strtoul(argv[1], &e, 16);
		param.set_rgb.red = strtoul(argv[2], &e, 16);
		param.set_rgb.green = strtoul(argv[3], &e, 16);
		param.set_rgb.blue = strtoul(argv[4], &e, 16);
		return lb_do_cmd(LIGHTBAR_CMD_SET_RGB, &param, &resp);
	}

	/* Only thing left is to try to read an LED value */
	if (argc == 2) {
		char *e;
		param.get_rgb.led = strtoul(argv[1], &e, 0);
		if (!(e && *e)) {
			r = lb_do_cmd(LIGHTBAR_CMD_GET_RGB, &param, &resp);
			if (r)
				return r;
			printf("%02x %02x %02x\n",
			       resp.get_rgb.red,
			       resp.get_rgb.green,
			       resp.get_rgb.blue);
			return 0;
		}
	}

	return lb_help(argv[0]);
}

/* Create an array to store sizes of motion sense param and response structs. */
#define MS_SIZES(SUBCMD) { \
		sizeof(((struct ec_params_motion_sense *)0)->SUBCMD) \
		+ sizeof(((struct ec_params_motion_sense *)0)->cmd), \
		sizeof(((struct ec_response_motion_sense *)0)->SUBCMD) }
/*
 * For ectool only, assume no more than 16 sensors.
 * More advanced implementation would allocate the right amount of
 * memory depending on the number of sensors.
 */
#define ECTOOL_MAX_SENSOR 16
#define MS_DUMP_SIZE() { \
		sizeof(((struct ec_params_motion_sense *)0)->dump) \
		+ sizeof(((struct ec_params_motion_sense *)0)->cmd), \
		sizeof(((struct ec_response_motion_sense *)0)->dump) \
		+ sizeof(struct ec_response_motion_sensor_data) * \
		  ECTOOL_MAX_SENSOR}

#define MS_FIFO_INFO_SIZE() { \
		sizeof(((struct ec_params_motion_sense *)0)->fifo_info) \
		+ sizeof(((struct ec_params_motion_sense *)0)->cmd), \
		sizeof(((struct ec_response_motion_sense *)0)->fifo_info) \
		+ sizeof(uint16_t) * ECTOOL_MAX_SENSOR}


static const struct {
	uint8_t outsize;
	uint8_t insize;
} ms_command_sizes[] = {
	MS_DUMP_SIZE(),
	MS_SIZES(info),
	MS_SIZES(ec_rate),
	MS_SIZES(sensor_odr),
	MS_SIZES(sensor_range),
	MS_SIZES(kb_wake_angle),
	MS_SIZES(data),
	MS_SIZES(fifo_flush),
	MS_FIFO_INFO_SIZE(),
	MS_SIZES(fifo_read),
	MS_SIZES(perform_calib),
	MS_SIZES(sensor_offset),
};
BUILD_ASSERT(ARRAY_SIZE(ms_command_sizes) == MOTIONSENSE_NUM_CMDS);
#undef MS_SIZES

static int ms_help(const char *cmd)
{
	printf("Usage:\n");
	printf("  %s                            - dump all motion data\n", cmd);
	printf("  %s active                     - print active flag\n", cmd);
	printf("  %s info NUM                   - print sensor info\n", cmd);
	printf("  %s ec_rate [RATE_MS]          - set/get sample rate\n", cmd);
	printf("  %s odr NUM [ODR [ROUNDUP]]    - set/get sensor ODR\n", cmd);
	printf("  %s range NUM [RANGE [ROUNDUP]]- set/get sensor range\n", cmd);
	printf("  %s offset NUM                 - get sensor offset\n", cmd);
	printf("  %s kb_wake NUM                - set/get KB wake ang\n", cmd);
	printf("  %s data NUM                   - read sensor latest data\n",
			cmd);
	printf("  %s fifo_info                  - print fifo info\n", cmd);
	printf("  %s fifo_read MAX_DATA         - read fifo data\n", cmd);
	printf("  %s fifo_flush NUM             - trigger fifo interrupt\n",
			cmd);

	return 0;
}

static int cmd_motionsense(int argc, char **argv)
{
	int i, rv, status_only = (argc == 2);
	struct ec_params_motion_sense param;
	/* The largest size using resp as a response buffer */
	uint8_t resp_buffer[ms_command_sizes[MOTIONSENSE_CMD_DUMP].insize];
	struct ec_response_motion_sense *resp =
		(struct ec_response_motion_sense *)resp_buffer;
	char *e;
	/*
	 * Warning: the following strings printed out are read in an
	 * autotest. Do not change string without consulting autotest
	 * for kernel_CrosECSysfsAccel.
	 */
	const char *motion_status_string[2][2] = {
		{ "Motion sensing inactive", "0"},
		{ "Motion sensing active", "1"},
	};

	/* No motionsense command has more than 5 args. */
	if (argc > 5)
		return ms_help(argv[0]);

	if ((argc == 1) ||
	    (argc == 2 && !strcasecmp(argv[1], "active"))) {
		param.cmd = MOTIONSENSE_CMD_DUMP;
		param.dump.max_sensor_count = ECTOOL_MAX_SENSOR;
		rv = ec_command(
			EC_CMD_MOTION_SENSE_CMD, 1,
			&param, ms_command_sizes[param.cmd].outsize,
			resp, ms_command_sizes[param.cmd].insize);
		if (rv > 0) {
			printf("%s\n", motion_status_string[
					!!(resp->dump.module_flags &
					   MOTIONSENSE_MODULE_FLAG_ACTIVE)][
					status_only]);
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

	if (argc == 3 && !strcasecmp(argv[1], "info")) {
		param.cmd = MOTIONSENSE_CMD_INFO;

		param.sensor_odr.sensor_num = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad %s arg.\n", argv[2]);
			return -1;
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);

		if (rv < 0)
			return rv;

		printf("Type:     ");
		switch (resp->info.type) {
		case MOTIONSENSE_TYPE_ACCEL:
			printf("accel\n");
			break;
		case MOTIONSENSE_TYPE_GYRO:
			printf("gyro\n");
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
		default:
			printf("unknown\n");
		}

		return 0;
	}

	if (argc < 4 && !strcasecmp(argv[1], "ec_rate")) {
		param.cmd = MOTIONSENSE_CMD_EC_RATE;
		param.ec_rate.data = EC_MOTION_SENSE_NO_VALUE;

		if (argc == 3) {
			param.ec_rate.data = strtol(argv[2], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad %s arg.\n", argv[2]);
				return -1;
			}
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);

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

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);

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

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);

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

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);

		if (rv < 0)
			return rv;

		printf("%d\n", resp->kb_wake_angle.ret);
		return 0;
	}

	if (argc == 2 && !strcasecmp(argv[1], "fifo_info")) {
		param.cmd = MOTIONSENSE_CMD_FIFO_INFO;
		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);
		if (rv < 0)
			return rv;

		printf("Size:     %d\n", resp->fifo_info.size);
		printf("Count:    %d\n", resp->fifo_info.count);
		printf("Timestamp:%" PRIx32 "\n", resp->fifo_info.timestamp);
		printf("Total lost: %d\n", resp->fifo_info.total_lost);
		for (i = 0; i < ECTOOL_MAX_SENSOR; i++) {
			int lost;
			lost = resp->fifo_info.lost[i];
			if (lost != 0)
				printf("Lost %d:     %d\n", i, lost);
		}
		return 0;
	}

	if (argc == 3 && !strcasecmp(argv[1], "fifo_read")) {
		/* large number to test fragmentation */
		struct {
			uint32_t number_data;
			struct ec_response_motion_sensor_data data[512];
		} fifo_read_buffer = {
			.number_data = -1,
		};
		int print_data = 0,  max_data = strtol(argv[2], &e, 0);

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

			rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2,
					&param,
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
					uint32_t timestamp = 0;

					memcpy(&timestamp, vector->data,
							sizeof(uint32_t));
					printf("Timestamp:%" PRIx32 "%s\n",
						timestamp,
						(vector->flags &
						 MOTIONSENSE_SENSOR_FLAG_FLUSH ?
						 " - Flush" : ""));
				} else {
					printf("Sensor %d: %d\t%d\t%d\n",
						vector->sensor_num,
						vector->data[0],
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

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);

		return rv < 0 ? rv : 0;
	}

	if (argc == 3 && !strcasecmp(argv[1], "offset")) {
		param.cmd = MOTIONSENSE_CMD_SENSOR_OFFSET;
		param.sensor_offset.flags = 0;

		param.sensor_offset.sensor_num = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad %s arg.\n", argv[2]);
			return -1;
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);

		if (rv < 0)
			return rv;

		printf("Offset vector: X:%d, Y:%d, Z:%d\n",
			resp->sensor_offset.offset[0],
			resp->sensor_offset.offset[1],
			resp->sensor_offset.offset[2]);
		if ((uint16_t)resp->sensor_offset.temp ==
		    EC_MOTION_SENSE_INVALID_CALIB_TEMP)
			printf("temperature at calibration unknown\n");
		else
			printf("temperature at calibration: %d.%02d C\n",
			       resp->sensor_offset.temp / 100,
			       resp->sensor_offset.temp % 100);
		return 0;
	}
	return ms_help(argv[0]);
}

int cmd_next_event(int argc, char *argv[])
{
	uint8_t *rdata = (uint8_t *)ec_inbuf;
	int rv;
	int i;

	rv = ec_command(EC_CMD_GET_NEXT_EVENT, 0,
			NULL, 0, rdata, ec_max_insize);
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
			"off | <color> | <color>=<value>...>\n", argv[0]);
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
		rv = ec_command(EC_CMD_LED_CONTROL, 1, &p, sizeof(p),
				&r, sizeof(r));
		printf("Brightness range for LED %d:\n", p.led_id);
		if (rv < 0) {
			fprintf(stderr, "Error: Unsupported LED.\n");
			return rv;
		}
		for (i = 0; i < EC_LED_COLOR_COUNT; ++i)
			printf("\t%s\t: 0x%x\n",
			       led_color_names[i],
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

	if (argc != 3) {
		fprintf(stderr,
			"Usage: %s <port_id> <mode_id>\n", argv[0]);
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

	printf("Setting port %d to mode %d...\n", p.usb_port_id, p.mode);

	rv = ec_command(EC_CMD_USB_CHARGE_SET_MODE, 0,
			&p, sizeof(p), NULL, 0);
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

	rv = ec_command(EC_CMD_USB_MUX, 0,
			&p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("Set USB mux to 0x%x.\n", p.mux);

	return 0;
}


int cmd_usb_pd(int argc, char *argv[])
{
	const char *role_str[] = {"", "toggle", "toggle-off", "sink", "source"};
	const char *mux_str[] = {"", "none", "usb", "dp", "dock", "auto"};
	struct ec_params_usb_pd_control p;
	struct ec_response_usb_pd_control_v1 *r_v1 =
		(struct ec_response_usb_pd_control_v1 *)ec_inbuf;
	struct ec_response_usb_pd_control *r =
		(struct ec_response_usb_pd_control *)ec_inbuf;
	int rv, i, j;
	int option_ok;
	char *e;
	int cmdver = 1;

	BUILD_ASSERT(ARRAY_SIZE(role_str) == USB_PD_CTRL_ROLE_COUNT);
	BUILD_ASSERT(ARRAY_SIZE(mux_str) == USB_PD_CTRL_MUX_COUNT);
	p.role = USB_PD_CTRL_ROLE_NO_CHANGE;
	p.mux = USB_PD_CTRL_MUX_NO_CHANGE;

	if (!ec_cmd_version_supported(EC_CMD_USB_PD_CONTROL, cmdver))
		cmdver = 0;

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

		if (!option_ok) {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			return -1;
		}
	}

	rv = ec_command(EC_CMD_USB_PD_CONTROL, cmdver, &p, sizeof(p),
			ec_inbuf, ec_max_insize);

	if (rv < 0 || argc != 2)
		return (rv < 0) ? rv : 0;

	if (cmdver == 0) {
		printf("Port C%d is %sabled, Role:%s Polarity:CC%d State:%d\n",
		       p.port, (r->enabled) ? "en" : "dis",
		       r->role == PD_ROLE_SOURCE ? "SRC" : "SNK",
		       r->polarity + 1, r->state);
	} else {
		printf("Port C%d is %s,%s, Role:%s %s Polarity:CC%d State:%s\n",
		       p.port, (r_v1->enabled & 1) ? "enabled" : "disabled",
		       (r_v1->enabled & 2) ? "connected" : "disconnected",
		       r_v1->role & PD_ROLE_SOURCE ? "SRC" : "SNK",
		       r_v1->role & (PD_ROLE_DFP << 1) ? "DFP" : "UFP",
		       r_v1->polarity + 1, r_v1->state);
	}
	return (rv < 0 ? rv : 0);
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
	printf(" %dmV / %dmA, max %dmV / %dmA",
		r->meas.voltage_now, r->meas.current_lim, r->meas.voltage_max,
		r->meas.current_max);
	if (r->max_power)
		printf(" / %dmW", r->max_power / 1000);
	printf("\n");
}

int cmd_usb_pd_power(int argc, char *argv[])
{
	struct ec_params_usb_pd_power_info p;
	struct ec_response_usb_pd_power_info *r =
		(struct ec_response_usb_pd_power_info *)ec_inbuf;
	int num_ports, i, rv;

	rv = ec_command(EC_CMD_USB_PD_PORTS, 0, NULL, 0,
			ec_inbuf, ec_max_insize);
	if (rv < 0)
		return rv;
	num_ports = ((struct ec_response_usb_pd_ports *)r)->num_ports;

	for (i = 0; i < num_ports; i++) {
		p.port = i;
		rv = ec_command(EC_CMD_USB_PD_POWER_INFO, 0,
				&p, sizeof(p),
				ec_inbuf, ec_max_insize);
		if (rv < 0)
			return rv;

		printf("Port %d: ", i);
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
		fprintf(stderr,
			"Usage: %s <row> <col> <0|1>\n", argv[0]);
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
				      p.row,
				      p.col);

	rv = ec_command(EC_CMD_MKBP_SIMULATE_KEY, 0,
			&p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;
	printf("Done.\n");
	return 0;
}


static void print_panic_reg(int regnum, const uint32_t *regs, int index)
{
	static const char * const regname[] = {
		"r0 ", "r1 ", "r2 ", "r3 ", "r4 ",
		"r5 ", "r6 ", "r7 ", "r8 ", "r9 ",
		"r10", "r11", "r12", "sp ", "lr ",
		"pc "};

	printf("%s:", regname[regnum]);
	if (regs)
		printf("%08x", regs[index]);
	else
		printf("        ");
	printf((regnum & 3) == 3 ? "\n" : " ");
}


int cmd_panic_info(int argc, char *argv[])
{
	int rv;
	struct panic_data *pdata = (struct panic_data *)ec_inbuf;
	const uint32_t *lregs = pdata->cm.regs;
	const uint32_t *sregs = NULL;
	enum {
		ORIG_UNKNOWN = 0,
		ORIG_PROCESS,
		ORIG_HANDLER
	} origin = ORIG_UNKNOWN;
	int i;
	const char *panic_origins[3] = {"", "PROCESS", "HANDLER"};

	rv = ec_command(EC_CMD_GET_PANIC_INFO, 0, NULL, 0,
			ec_inbuf, ec_max_insize);
	if (rv < 0)
		return rv;

	if (rv == 0) {
		printf("No panic data.\n");
		return 0;
	}

	/*
	 * We only understand panic data with version <= 2. Warn the user
	 * of higher versions.
	 */
	if (pdata->struct_version > 2)
		fprintf(stderr,
			"Unknown panic data version (%d). "
			"Following data may be incorrect!\n",
			pdata->struct_version);

	if (pdata->arch != PANIC_ARCH_CORTEX_M)
		fprintf(stderr, "Unknown architecture (%d). "
			"CPU specific data will be incorrect!\n",
			pdata->arch);

	printf("Saved panic data:%s\n",
	       (pdata->flags & PANIC_DATA_FLAG_OLD_HOSTCMD ? "" : " (NEW)"));

	if (pdata->struct_version == 2)
		origin = ((lregs[11] & 0xf) == 1 || (lregs[11] & 0xf) == 9) ?
			 ORIG_HANDLER : ORIG_PROCESS;

	/*
	 * In pdata struct, 'regs', which is allocated before 'frame', has
	 * one less elements in version 1. Therefore, if the data is from
	 * version 1, shift 'sregs' by one element to align with 'frame' in
	 * version 1.
	 */
	if (pdata->flags & PANIC_DATA_FLAG_FRAME_VALID)
		sregs = pdata->cm.frame - (pdata->struct_version == 1 ? 1 : 0);

	printf("=== %s EXCEPTION: %02x ====== xPSR: %08x ===\n",
	       panic_origins[origin],
	       lregs[1] & 0xff, sregs ? sregs[7] : -1);
	for (i = 0; i < 4; ++i)
		print_panic_reg(i, sregs, i);
	for (i = 4; i < 10; ++i)
		print_panic_reg(i, lregs, i - 1);
	print_panic_reg(10, lregs, 9);
	print_panic_reg(11, lregs, 10);
	print_panic_reg(12, sregs, 4);
	print_panic_reg(13, lregs, origin == ORIG_HANDLER ? 2 : 0);
	print_panic_reg(14, sregs, 5);
	print_panic_reg(15, sregs, 6);

	return 0;
}


int cmd_power_info(int argc, char *argv[])
{
	struct ec_response_power_info r;
	int rv;

	rv = ec_command(EC_CMD_POWER_INFO, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("AC Voltage: %d mV\n", r.voltage_ac);
	printf("System Voltage: %d mV\n", r.voltage_system);
	printf("System Current: %d mA\n", r.current_system);
	printf("System Power: %d mW\n",
			r.voltage_system * r.current_system / 1000);
	printf("USB Device Type: 0x%x\n", r.usb_dev_type);
	printf("USB Current Limit: %d mA\n", r.usb_current_limit);
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
		fprintf(stderr,
			"Usage: %s <offset> <size> <filename>\n", argv[0]);
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
		rv = ec_command(EC_CMD_PSTORE_READ, 0,
				&p, sizeof(p), rdata, sizeof(rdata));
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
		rv = ec_command(EC_CMD_PSTORE_WRITE, 0,
				&p, sizeof(p), NULL, 0);
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

	rv = ec_command(EC_CMD_HOST_EVENT_GET_B, 0,
			NULL, 0, &r, sizeof(r));
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

	rv = ec_command(EC_CMD_HOST_EVENT_GET_SMI_MASK, 0,
			NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("Current host event SMI mask: 0x%08x\n", r.mask);
	return 0;
}


int cmd_host_event_get_sci_mask(int argc, char *argv[])
{
	struct ec_response_host_event_mask r;
	int rv;

	rv = ec_command(EC_CMD_HOST_EVENT_GET_SCI_MASK, 0,
			NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("Current host event SCI mask: 0x%08x\n", r.mask);
	return 0;
}


int cmd_host_event_get_wake_mask(int argc, char *argv[])
{
	struct ec_response_host_event_mask r;
	int rv;

	rv = ec_command(EC_CMD_HOST_EVENT_GET_WAKE_MASK, 0,
			NULL, 0, &r, sizeof(r));
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

	rv = ec_command(EC_CMD_HOST_EVENT_SET_SMI_MASK, 0,
			&p, sizeof(p), NULL, 0);
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

	rv = ec_command(EC_CMD_HOST_EVENT_SET_SCI_MASK, 0,
			&p, sizeof(p), NULL, 0);
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

	rv = ec_command(EC_CMD_HOST_EVENT_SET_WAKE_MASK, 0,
			&p, sizeof(p), NULL, 0);
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

	rv = ec_command(EC_CMD_HOST_EVENT_CLEAR, 0,
			&p, sizeof(p), NULL, 0);
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

	rv = ec_command(EC_CMD_HOST_EVENT_CLEAR_B, 0,
			&p, sizeof(p), NULL, 0);
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
		rv = ec_command(EC_CMD_SWITCH_ENABLE_WIRELESS, 0,
				&p, sizeof(p), NULL, 0);
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
				EC_VER_SWITCH_ENABLE_WIRELESS,
				&p, sizeof(p), &r, sizeof(r));
		if (rv < 0)
			return rv;

		printf("Now=0x%x, suspend=0x%x\n",
		       r.now_flags, r.suspend_flags);
	}

	return 0;
}


int cmd_i2c_read(int argc, char *argv[])
{
	struct ec_params_i2c_read p;
	struct ec_response_i2c_read r;
	char *e;
	int rv;

	if (argc != 5) {
		fprintf(stderr, "Usage: %s <8 | 16> <port> <addr> <offset>\n",
				argv[0]);
		return -1;
	}

	p.read_size = strtol(argv[1], &e, 0);
	if ((e && *e) || (p.read_size != 8 && p.read_size != 16)) {
		fprintf(stderr, "Bad read size.\n");
		return -1;
	}

	p.port = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port.\n");
		return -1;
	}

	p.addr = strtol(argv[3], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad address.\n");
		return -1;
	}

	p.offset = strtol(argv[4], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}

	/*
	 * TODO(crosbug.com/p/23570): use I2C_XFER command if supported, then
	 * fall back to I2C_READ.
	 */

	rv = ec_command(EC_CMD_I2C_READ, 0, &p, sizeof(p), &r, sizeof(r));

	if (rv < 0)
		return rv;

	printf("Read from I2C port %d at 0x%x offset 0x%x = 0x%x\n",
	       p.port, p.addr, p.offset, r.data);
	return 0;
}


int cmd_i2c_write(int argc, char *argv[])
{
	struct ec_params_i2c_write p;
	char *e;
	int rv;

	if (argc != 6) {
		fprintf(stderr,
			"Usage: %s <8 | 16> <port> <addr> <offset> <data>\n",
			argv[0]);
		return -1;
	}

	p.write_size = strtol(argv[1], &e, 0);
	if ((e && *e) || (p.write_size != 8 && p.write_size != 16)) {
		fprintf(stderr, "Bad write size.\n");
		return -1;
	}

	p.port = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port.\n");
		return -1;
	}

	p.addr = strtol(argv[3], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad address.\n");
		return -1;
	}

	p.offset = strtol(argv[4], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}

	p.data = strtol(argv[5], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad data.\n");
		return -1;
	}

	/*
	 * TODO(crosbug.com/p/23570): use I2C_XFER command if supported, then
	 * fall back to I2C_WRITE.
	 */

	rv = ec_command(EC_CMD_I2C_WRITE, 0, &p, sizeof(p), NULL, 0);

	if (rv < 0)
		return rv;

	printf("Wrote 0x%x to I2C port %d at 0x%x offset 0x%x.\n",
	       p.data, p.port, p.addr, p.offset);
	return 0;
}


int cmd_i2c_xfer(int argc, char *argv[])
{
	struct ec_params_i2c_passthru *p =
		(struct ec_params_i2c_passthru *)ec_outbuf;
	struct ec_response_i2c_passthru *r =
		(struct ec_response_i2c_passthru *)ec_inbuf;
	struct ec_params_i2c_passthru_msg *msg = p->msg;
	unsigned int addr;
	uint8_t *pdata;
	char *e;
	int read_len, write_len;
	int size;
	int rv, i;

	if (argc < 4) {
		fprintf(stderr,
			"Usage: %s <port> <slave_addr> <read_count> "
			"[write bytes...]\n", argv[0]);
		return -1;
	}

	p->port = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port.\n");
		return -1;
	}

	addr = strtol(argv[2], &e, 0) & 0x7f;
	if (e && *e) {
		fprintf(stderr, "Bad slave address.\n");
		return -1;
	}

	read_len = strtol(argv[3], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad read length.\n");
		return -1;
	}

	/* Skip over params to bytes to write */
	argc -= 4;
	argv += 4;
	write_len = argc;
	p->num_msgs = (read_len != 0) + (write_len != 0);

	size = sizeof(*p) + p->num_msgs * sizeof(*msg);
	if (size + write_len > ec_max_outsize) {
		fprintf(stderr, "Params too large for buffer\n");
		return -1;
	}
	if (sizeof(*r) + read_len > ec_max_insize) {
		fprintf(stderr, "Read length too big for buffer\n");
		return -1;
	}

	pdata = (uint8_t *)p + size;
	if (write_len) {
		msg->addr_flags = addr;
		msg->len = write_len;

		for (i = 0; i < write_len; i++) {
			pdata[i] = strtol(argv[i], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad write byte %d\n", i);
				return -1;
			}
		}
		msg++;
	}

	if (read_len) {
		msg->addr_flags = addr | EC_I2C_FLAG_READ;
		msg->len = read_len;
	}

	rv = ec_command(EC_CMD_I2C_PASSTHRU, 0, p, size + write_len,
			r, sizeof(*r) + read_len);
	if (rv < 0)
		return rv;

	/* Parse response */
	if (r->i2c_status & (EC_I2C_STATUS_NAK | EC_I2C_STATUS_TIMEOUT)) {
		fprintf(stderr, "Transfer failed with status=0x%x\n",
			r->i2c_status);
		return -1;
	}

	if (rv < sizeof(*r) + read_len) {
		fprintf(stderr, "Truncated read response\n");
		return -1;
	}

	if (read_len) {
		printf("Read bytes:");
		for (i = 0; i < read_len; i++)
			printf(" %#02x", r->data[i]);
		printf("\n");
	} else {
		printf("Write successful.\n");
	}

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

	rv = ec_command(EC_CMD_SWITCH_ENABLE_BKLIGHT, 0,
			&p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("Success.\n");
	return 0;
}


int cmd_ext_power_current_limit(int argc, char *argv[])
{
	struct ec_params_ext_power_current_limit p;
	int rv;
	char *e;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <max_current_mA>\n", argv[0]);
		return -1;
	}

	p.limit = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad value.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_EXT_POWER_CURRENT_LIMIT, 0, &p, sizeof(p),
			NULL, 0);
	return rv;
}


int cmd_charge_current_limit(int argc, char *argv[])
{
	struct ec_params_current_limit p;
	int rv;
	char *e;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <max_current_mA>\n", argv[0]);
		return -1;
	}

	p.limit = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad value.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_CHARGE_CURRENT_LIMIT, 0, &p, sizeof(p),
			NULL, 0);
	return rv;
}


int cmd_charge_control(int argc, char *argv[])
{
	struct ec_params_charge_control p;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <normal | idle | discharge>\n",
			argv[0]);
		return -1;
	}

	if (!strcasecmp(argv[1], "normal")) {
		p.mode = CHARGE_CONTROL_NORMAL;
	} else if (!strcasecmp(argv[1], "idle")) {
		p.mode = CHARGE_CONTROL_IDLE;
	} else if (!strcasecmp(argv[1], "discharge")) {
		p.mode = CHARGE_CONTROL_DISCHARGE;
	} else {
		fprintf(stderr, "Bad value.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_CHARGE_CONTROL, 1, &p, sizeof(p), NULL, 0);
	if (rv < 0) {
		fprintf(stderr, "Is AC connected?\n");
		return rv;
	}

	switch (p.mode) {
	case CHARGE_CONTROL_NORMAL:
		printf("Charge state machine normal mode.\n");
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



/* Table of subcommand sizes for EC_CMD_CHARGE_STATE */
#define CB_SIZES(SUBCMD) { \
		sizeof(((struct ec_params_charge_state *)0)->SUBCMD) \
		+ sizeof(((struct ec_params_charge_state *)0)->cmd), \
		sizeof(((struct ec_response_charge_state *)0)->SUBCMD) }
static const struct {
	uint8_t to_ec_size;
	uint8_t from_ec_size;
} cs_paramcount[] = {
	/* Order must match enum charge_state_command */
	CB_SIZES(get_state),
	CB_SIZES(get_param),
	CB_SIZES(set_param),
};
#undef CB_SIZES
BUILD_ASSERT(ARRAY_SIZE(cs_paramcount) == CHARGE_STATE_NUM_CMDS);

static int cs_do_cmd(struct ec_params_charge_state *to_ec,
		     struct ec_response_charge_state *from_ec)
{
	int rv;
	int cmd = to_ec->cmd;

	rv = ec_command(EC_CMD_CHARGE_STATE, 0,
			to_ec, cs_paramcount[cmd].to_ec_size,
			from_ec, cs_paramcount[cmd].from_ec_size);

	return (rv < 0 ? 1 : 0);
}

static const char * const base_params[] = {
	"chg_voltage",
	"chg_current",
	"chg_input_current",
	"chg_status",
	"chg_option",
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
			p = strtoul(argv[2], &e, 0);
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
			p = strtoul(argv[2], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad param: %s\n", argv[2]);
				return -1;
			}
			v = strtoul(argv[3], &e, 0);
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

		rv = ec_command(EC_CMD_GPIO_GET, cmdver, &p,
				sizeof(p), &r, sizeof(r));
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

		rv = ec_command(EC_CMD_GPIO_GET, cmdver, &p_v1,
				sizeof(p_v1), &r_v1, sizeof(r_v1));

		if (rv < 0)
			return rv;

		printf("GPIO %s = %d\n", p_v1.get_value_by_name.name,
			r_v1.get_value_by_name.val);
		return 0;
	}

	/* Need GPIO count for EC_GPIO_GET_COUNT or EC_GPIO_GET_INFO */
	p_v1.subcmd = EC_GPIO_GET_COUNT;
	rv = ec_command(EC_CMD_GPIO_GET, cmdver, &p_v1,
			sizeof(p_v1), &r_v1, sizeof(r_v1));
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

		rv = ec_command(EC_CMD_GPIO_GET, cmdver, &p_v1,
				sizeof(p_v1), &r_v1, sizeof(r_v1));
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


int cmd_battery(int argc, char *argv[])
{
	char batt_text[EC_MEMMAP_TEXT_MAX];
	int rv, val;

	val = read_mapped_mem8(EC_MEMMAP_BATTERY_VERSION);
	if (val < 1) {
		fprintf(stderr, "Battery version %d is not supported\n", val);
		return -1;
	}

	printf("Battery info:\n");

	rv = read_mapped_string(EC_MEMMAP_BATT_MFGR, batt_text,
			sizeof(batt_text));
	if (rv < 0 || !is_string_printable(batt_text))
		goto cmd_error;
	printf("  OEM name:               %s\n", batt_text);

	rv = read_mapped_string(EC_MEMMAP_BATT_MODEL, batt_text,
			sizeof(batt_text));
	if (rv < 0 || !is_string_printable(batt_text))
		goto cmd_error;
	printf("  Model number:           %s\n", batt_text);

	rv = read_mapped_string(EC_MEMMAP_BATT_TYPE, batt_text,
			sizeof(batt_text));
	if (rv < 0 || !is_string_printable(batt_text))
		goto cmd_error;
	printf("  Chemistry   :           %s\n", batt_text);

	rv = read_mapped_string(EC_MEMMAP_BATT_SERIAL, batt_text,
			sizeof(batt_text));
	printf("  Serial number:          %s\n", batt_text);

	val = read_mapped_mem32(EC_MEMMAP_BATT_DCAP);
	if (!is_battery_range(val))
		goto cmd_error;
	printf("  Design capacity:        %u mAh\n", val);

	val = read_mapped_mem32(EC_MEMMAP_BATT_LFCC);
	if (!is_battery_range(val))
		goto cmd_error;
	printf("  Last full charge:       %u mAh\n", val);

	val = read_mapped_mem32(EC_MEMMAP_BATT_DVLT);
	if (!is_battery_range(val))
		goto cmd_error;
	printf("  Design output voltage   %u mV\n", val);

	val = read_mapped_mem32(EC_MEMMAP_BATT_CCNT);
	if (!is_battery_range(val))
		goto cmd_error;
	printf("  Cycle count             %u\n", val);

	val = read_mapped_mem32(EC_MEMMAP_BATT_VOLT);
	if (!is_battery_range(val))
		goto cmd_error;
	printf("  Present voltage         %u mV\n", val);

	val = read_mapped_mem32(EC_MEMMAP_BATT_RATE);
	if (!is_battery_range(val))
		goto cmd_error;
	printf("  Present current         %u mA\n", val);

	val = read_mapped_mem32(EC_MEMMAP_BATT_CAP);
	if (!is_battery_range(val))
		goto cmd_error;
	printf("  Remaining capacity      %u mAh\n", val);

	val = read_mapped_mem8(EC_MEMMAP_BATT_FLAG);
	printf("  Flags                   0x%02x", val);
	if (val & EC_BATT_FLAG_AC_PRESENT)
		printf(" AC_PRESENT");
	if (val & EC_BATT_FLAG_BATT_PRESENT)
		printf(" BATT_PRESENT");
	if (val & EC_BATT_FLAG_DISCHARGING)
		printf(" DISCHARGING");
	if (val & EC_BATT_FLAG_CHARGING)
		printf(" CHARGING");
	if (val & EC_BATT_FLAG_LEVEL_CRITICAL)
		printf(" LEVEL_CRITICAL");
	printf("\n");

	return 0;
cmd_error:
	fprintf(stderr, "Bad battery info value. Check protocol version.\n");
	return -1;
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
		fprintf(stderr, "It is expected if the rv is -%d "
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

	rv = ec_command(EC_CMD_BATTERY_VENDOR_PARAM, 0, &p, sizeof(p),
			&r, sizeof(r));

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

	rv = ec_command(EC_CMD_GET_PROTOCOL_INFO, 0, NULL, 0,
			&info, sizeof(info));
	if (rv < 0) {
		fprintf(stderr, "Protocol info unavailable.  EC probably only "
				"supports protocol version 2.\n");
		return rv;
	}

	printf("  protocol versions:");
	for (i = 0; i < 32; i++) {
		if (info.protocol_versions & (1 << i))
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

	if (argc < 2) {
		/* Get hash status */
		p.cmd = EC_VBOOT_HASH_GET;
		rv = ec_command(EC_CMD_VBOOT_HASH, 0,
				&p, sizeof(p), &r, sizeof(r));
		if (rv < 0)
			return rv;

		return ec_hash_print(&r);
	}

	if (argc == 2 && !strcasecmp(argv[1], "abort")) {
		/* Abort hash calculation */
		p.cmd = EC_VBOOT_HASH_ABORT;
		rv = ec_command(EC_CMD_VBOOT_HASH, 0,
				&p, sizeof(p), &r, sizeof(r));
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
		p.offset = EC_VBOOT_HASH_OFFSET_RW;
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
		rv = ec_command(EC_CMD_CONSOLE_READ, 0,
				NULL, 0, ec_inbuf, ec_max_insize);
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
	const char *name;	/* name of this parameter */
	const char *help;	/* help message */
	int size;		/* size in bytes */
	int offset;		/* offset within structure */
};

#define FIELD(fname, field, help_str) \
	{ \
		.name = fname, \
		.help = help_str, \
		.size = sizeof(((struct ec_mkbp_config *)NULL)->field), \
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
		int count, const char *name, unsigned int *nump)
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
		mask = -1U;	/* show all fields */
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
		if (mask & (1 << i)) {
			fprintf(stderr, "%-12s   %u\n", param->name,
				get_value(param, (char *)config));
		}
	}

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

		fprintf(stderr, "Usage: %s get [<param>] - print params\n"
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
	rv = ec_command(EC_CMD_TMP006_GET_CALIBRATION, 0,
			&pg, sizeof(pg), &rg, sizeof(rg));
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
	return ec_command(EC_CMD_TMP006_SET_CALIBRATION, 0,
			  &ps, sizeof(ps), NULL, 0);
}

/* Index is already checked. argv[0] is first param value */
static int cmd_tmp006cal_v1(int idx, int argc, char *argv[])
{
	struct ec_params_tmp006_get_calibration pg;
	struct ec_response_tmp006_get_calibration_v1 *rg = ec_inbuf;
	struct ec_params_tmp006_set_calibration_v1 *ps = ec_outbuf;
	float val;
	char *e;
	int i, rv, cmdsize;

	/* Algorithm 1 parameter names */
	static const char * const alg1_pname[] = {
		"s0", "a1", "a2", "b0", "b1", "b2", "c2",
		"d0", "d1", "ds", "e0", "e1",
	};

	/* Get current values */
	pg.index = idx;
	rv = ec_command(EC_CMD_TMP006_GET_CALIBRATION, 1,
			&pg, sizeof(pg), rg, ec_max_insize);
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
	return ec_command(EC_CMD_TMP006_SET_CALIBRATION, 1,
			  ps, cmdsize, NULL, 0);
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
	char *e;

	memset(&req, 0, sizeof(req));

	if (argc == 2 && !strcasecmp(argv[1], "stop")) {
		req.flags = EC_HANG_STOP_NOW;
		return ec_command(EC_CMD_HANG_DETECT, 0, &req, sizeof(req),
				  NULL, 0);
	}

	if (argc == 2 && !strcasecmp(argv[1], "start")) {
		req.flags = EC_HANG_START_NOW;
		return ec_command(EC_CMD_HANG_DETECT, 0, &req, sizeof(req),
				  NULL, 0);
	}

	if (argc == 4) {
		req.flags = strtol(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad flags.\n");
			return -1;
		}

		req.host_event_timeout_msec = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad event timeout.\n");
			return -1;
		}

		req.warm_reboot_timeout_msec = strtol(argv[3], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad reboot timeout.\n");
			return -1;
		}

		printf("hang flags=0x%x\n"
		       "event_timeout=%d ms\n"
		       "reboot_timeout=%d ms\n",
		       req.flags, req.host_event_timeout_msec,
		       req.warm_reboot_timeout_msec);

		return ec_command(EC_CMD_HANG_DETECT, 0, &req, sizeof(req),
				  NULL, 0);
	}

	fprintf(stderr,
		"Must specify start/stop or <flags> <event_ms> <reboot_ms>\n");
	return -1;
}

enum port_80_event {
	PORT_80_EVENT_RESUME = 0x1001,  /* S3->S0 transition */
	PORT_80_EVENT_RESET = 0x1002,   /* RESET transition */
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
		rv = ec_command(EC_CMD_PORT80_LAST_BOOT, 0,
				NULL, 0, &r, sizeof(r));
		fprintf(stderr, "Last boot %2x\n", r.code);
		printf("done.\n");
		return 0;
	}


	/* read writes and history_size */
	p.subcmd = EC_PORT80_GET_INFO;
	rv = ec_command(EC_CMD_PORT80_READ, cmdver,
			&p, sizeof(p), &rsp, sizeof(rsp));
	if (rv < 0) {
		fprintf(stderr, "Read error at writes\n");
		return rv;
	}
	writes = rsp.get_info.writes;
	history_size = rsp.get_info.history_size;

	history = malloc(history_size*sizeof(uint16_t));
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
		rv = ec_command(EC_CMD_PORT80_READ, cmdver,
				&p, sizeof(p), &rsp, sizeof(rsp));
		if (rv < 0) {
			fprintf(stderr, "Read error at offset %d\n", i);
			free(history);
			return rv;
		}
		memcpy((void *)(history + i), rsp.data.codes,
			EC_PORT80_SIZE_MAX*sizeof(uint16_t));
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

struct command {
	const char *name;
	int (*handler)(int argc, char *argv[]);
};

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

	rv = ec_command(EC_CMD_PD_CHARGE_PORT_OVERRIDE, 0, &p, sizeof(p),
			NULL, 0);
	if (rv < 0)
		return rv;

	printf("Override port set to %d\n", p.override_port);
	return 0;
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
	unsigned seconds;
	time_t now;
	struct tm ltime;
	char time_str[64];

	while (1) {
		now = time(NULL);
		rv = ec_command(EC_CMD_PD_GET_LOG_ENTRY, 0,
				NULL, 0, &u, sizeof(u));
		if (rv < 0)
			return rv;

		if (u.r.type == PD_EVENT_NO_ENTRY) {
			printf("--- END OF LOG ---\n");
			break;
		}

		/* the timestamp is in 1024th of seconds */
		milliseconds = ((uint64_t)u.r.timestamp <<
					 PD_LOG_TIMESTAMP_SHIFT) / 1000;
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
			pinfo.type = (u.r.data & CHARGE_FLAGS_TYPE_MASK)
					>> CHARGE_FLAGS_TYPE_SHIFT;
			pinfo.max_power = 0;
			print_pd_power_info(&pinfo);
		} else if (u.r.type == PD_EVENT_MCU_CONNECT) {
			printf("New connection\n");
		} else if (u.r.type == PD_EVENT_MCU_BOARD_CUSTOM) {
			printf("Board-custom event\n");
		} else if (u.r.type == PD_EVENT_ACC_RW_FAIL) {
			printf("RW signature check failed\n");
		} else if (u.r.type == PD_EVENT_PS_FAULT) {
			static const char * const fault_names[] = {
				"---", "OCP", "fast OCP", "OVP", "Discharge"
			};
			const char *fault = u.r.data < ARRAY_SIZE(fault_names) ?
					fault_names[u.r.data] : "???";
			printf("Power supply fault: %s\n", fault);
		} else if (u.r.type == PD_EVENT_VIDEO_DP_MODE) {
			printf("DP mode %sabled\n", (u.r.data == 1) ?
			       "en" : "dis");
		} else if (u.r.type == PD_EVENT_VIDEO_CODEC) {
			memcpy(&minfo, u.r.payload,
			       sizeof(struct mcdp_info));
			printf("HDMI info: family:%04x chipid:%04x "
			       "irom:%d.%d.%d fw:%d.%d.%d\n",
			       MCDP_FAMILY(minfo.family),
			       MCDP_CHIPID(minfo.chipid),
			       minfo.irom.major, minfo.irom.minor,
			       minfo.irom.build, minfo.fw.major,
			       minfo.fw.minor, minfo.fw.build);
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

int cmd_pd_write_log(int argc, char *argv[])
{
	struct ec_params_pd_write_log_entry p;
	char *e;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <log_type> <port>\n",
			argv[0]);
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

/* NULL-terminated list of commands */
const struct command commands[] = {
	{"extpwrcurrentlimit", cmd_ext_power_current_limit},
	{"autofanctrl", cmd_thermal_auto_fan_ctrl},
	{"backlight", cmd_lcd_backlight},
	{"battery", cmd_battery},
	{"batterycutoff", cmd_battery_cut_off},
	{"batteryparam", cmd_battery_vendor_param},
	{"boardversion", cmd_board_version},
	{"chargecurrentlimit", cmd_charge_current_limit},
	{"chargecontrol", cmd_charge_control},
	{"chargeoverride", cmd_charge_port_override},
	{"chargestate", cmd_charge_state},
	{"chipinfo", cmd_chipinfo},
	{"cmdversions", cmd_cmdversions},
	{"console", cmd_console},
	{"echash", cmd_ec_hash},
	{"eventclear", cmd_host_event_clear},
	{"eventclearb", cmd_host_event_clear_b},
	{"eventget", cmd_host_event_get_raw},
	{"eventgetb", cmd_host_event_get_b},
	{"eventgetscimask", cmd_host_event_get_sci_mask},
	{"eventgetsmimask", cmd_host_event_get_smi_mask},
	{"eventgetwakemask", cmd_host_event_get_wake_mask},
	{"eventsetscimask", cmd_host_event_set_sci_mask},
	{"eventsetsmimask", cmd_host_event_set_smi_mask},
	{"eventsetwakemask", cmd_host_event_set_wake_mask},
	{"fanduty", cmd_fanduty},
	{"flasherase", cmd_flash_erase},
	{"flashprotect", cmd_flash_protect},
	{"flashread", cmd_flash_read},
	{"flashwrite", cmd_flash_write},
	{"flashinfo", cmd_flash_info},
	{"flashpd", cmd_flash_pd},
	{"forcelidopen", cmd_force_lid_open},
	{"gpioget", cmd_gpio_get},
	{"gpioset", cmd_gpio_set},
	{"hangdetect", cmd_hang_detect},
	{"hello", cmd_hello},
	{"kbpress", cmd_kbpress},
	{"i2cread", cmd_i2c_read},
	{"i2cwrite", cmd_i2c_write},
	{"i2cxfer", cmd_i2c_xfer},
	{"infopddev", cmd_pd_device_info},
	{"inventory", cmd_inventory},
	{"led", cmd_led},
	{"lightbar", cmd_lightbar},
	{"keyconfig", cmd_keyconfig},
	{"keyscan", cmd_keyscan},
	{"motionsense", cmd_motionsense},
	{"nextevent", cmd_next_event},
	{"panicinfo", cmd_panic_info},
	{"pause_in_s5", cmd_s5},
	{"pdgetmode", cmd_pd_get_amode},
	{"pdsetmode", cmd_pd_set_amode},
	{"port80read", cmd_port80_read},
	{"pdlog", cmd_pd_log},
	{"pdwritelog", cmd_pd_write_log},
	{"powerinfo", cmd_power_info},
	{"protoinfo", cmd_proto_info},
	{"pstoreinfo", cmd_pstore_info},
	{"pstoreread", cmd_pstore_read},
	{"pstorewrite", cmd_pstore_write},
	{"pwmgetfanrpm", cmd_pwm_get_fan_rpm},
	{"pwmgetkblight", cmd_pwm_get_keyboard_backlight},
	{"pwmgetnumfans", cmd_pwm_get_num_fans},
	{"pwmsetfanrpm", cmd_pwm_set_fan_rpm},
	{"pwmsetkblight", cmd_pwm_set_keyboard_backlight},
	{"readtest", cmd_read_test},
	{"reboot_ec", cmd_reboot_ec},
	{"rtcget", cmd_rtc_get},
	{"rtcset", cmd_rtc_set},
	{"rwhashpd", cmd_rw_hash_pd},
	{"sertest", cmd_serial_test},
	{"port80flood", cmd_port_80_flood},
	{"switches", cmd_switches},
	{"temps", cmd_temperature},
	{"tempsinfo", cmd_temp_sensor_info},
	{"test", cmd_test},
	{"thermalget", cmd_thermal_get_threshold},
	{"thermalset", cmd_thermal_set_threshold},
	{"tmp006cal", cmd_tmp006cal},
	{"tmp006raw", cmd_tmp006raw},
	{"usbchargemode", cmd_usb_charge_set_mode},
	{"usbmux", cmd_usb_mux},
	{"usbpd", cmd_usb_pd},
	{"usbpdpower", cmd_usb_pd_power},
	{"version", cmd_version},
	{"wireless", cmd_wireless},
	{NULL, NULL}
};

int main(int argc, char *argv[])
{
	const struct command *cmd;
	int dev = 0;
	int interfaces = COMM_ALL;
	char device_name[40] = "cros_ec";
	int rv = 1;
	int parse_error = 0;
	char *e;
	int i;

	BUILD_ASSERT(ARRAY_SIZE(lb_command_paramcount) == LIGHTBAR_NUM_CMDS);

	while ((i = getopt_long(argc, argv, "?", long_opts, NULL)) != -1) {
		switch (i) {
		case '?':
			/* Unhandled option */
			parse_error = 1;
			break;

		case OPT_DEV:
			dev = strtoul(optarg, &e, 0);
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
			} else {
				fprintf(stderr, "Invalid --interface\n");
				parse_error = 1;
			}
			break;
		case OPT_NAME:
			strncpy(device_name, optarg, 40);
			break;
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
	} else if (dev != 0) {
		fprintf(stderr, "Bad device number %d\n", dev);
		parse_error = 1;
	}

	if (parse_error) {
		print_help(argv[0], 0);
		exit(1);
	}

	if (acquire_gec_lock(GEC_LOCK_TIMEOUT_SECS) < 0) {
		fprintf(stderr, "Could not acquire GEC lock.\n");
		exit(1);
	}

	if (comm_init(interfaces, device_name)) {
		fprintf(stderr, "Couldn't find EC\n");
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
	return !!rv;
}
