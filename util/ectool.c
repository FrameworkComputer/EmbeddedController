/* Copyright 2013 The Chromium OS Authors. All rights reserved.
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
#include <signal.h>
#include <stdbool.h>

#include "battery.h"
#include "comm-host.h"
#include "chipset.h"
#include "compile_time_macros.h"
#include "cros_ec_dev.h"
#include "ec_panicinfo.h"
#include "ec_flash.h"
#include "ec_version.h"
#include "ectool.h"
#include "i2c.h"
#include "lightbar.h"
#include "lock/gec_lock.h"
#include "misc_util.h"
#include "panic.h"
#include "usb_pd.h"

/* Maximum flash size (16 MB, conservative) */
#define MAX_FLASH_SIZE 0x1000000

/*
 * Calculate the expected response for a hello ec command.
 */
#define HELLO_RESP(in_data) ((in_data) + 0x01020304)

/* Command line options */
enum {
	OPT_DEV = 1000,
	OPT_INTERFACE,
	OPT_NAME,
	OPT_ASCII,
	OPT_I2C_BUS,
};

static struct option long_opts[] = {
	{"dev", 1, 0, OPT_DEV},
	{"interface", 1, 0, OPT_INTERFACE},
	{"name", 1, 0, OPT_NAME},
	{"ascii", 0, 0, OPT_ASCII},
	{"i2c_bus", 1, 0, OPT_I2C_BUS},
	{NULL, 0, 0, 0}
};

#define GEC_LOCK_TIMEOUT_SECS	30  /* 30 secs */

const char help_str[] =
	"Commands:\n"
	"  adcread <channel>\n"
	"      Read an ADC channel.\n"
	"  addentropy [reset]\n"
	"      Add entropy to device secret\n"
	"  apreset\n"
	"      Issue AP reset\n"
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
	"  button [vup|vdown|rec] <Delay-ms>\n"
	"      Simulates button press.\n"
	"  cbi\n"
	"      Get/Set/Remove Cros Board Info\n"
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
	"  cec\n"
	"      Read or write CEC messages and settings\n"
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
	"  extpwrlimit\n"
	"      Set the maximum external power limit\n"
	"  fanduty <percent>\n"
	"      Forces the fan PWM to a constant duty cycle\n"
	"  flasherase <offset> <size>\n"
	"      Erases EC flash\n"
	"  flasheraseasync <offset> <size>\n"
	"      Erases EC flash asynchronously\n"
	"  flashinfo\n"
	"      Prints information on the EC flash\n"
	"  flashspiinfo\n"
	"      Prints information on EC SPI flash, if present\n"
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
	"  fpcontext\n"
	"      Sets the fingerprint sensor context\n"
	"  fpencstatus\n"
	"      Prints status of Fingerprint sensor encryption engine\n"
	"  fpframe\n"
	"      Retrieve the finger image as a PGM image\n"
	"  fpinfo\n"
	"      Prints information about the Fingerprint sensor\n"
	"  fpmode [capture|deepsleep|fingerdown|fingerup]\n"
	"      Configure/Read the fingerprint sensor current mode\n"
	"  fpseed\n"
	"      Sets the value of the TPM seed.\n"
	"  fpstats\n"
	"      Prints timing statisitcs relating to capture and matching\n"
	"  fptemplate [<infile>|<index 0..2>]\n"
	"      Add a template if <infile> is provided, else dump it\n"
	"  gpioget <GPIO name>\n"
	"      Get the value of GPIO signal\n"
	"  gpioset <GPIO name>\n"
	"      Set the value of GPIO signal\n"
	"  hangdetect <flags> <event_msec> <reboot_msec> | stop | start\n"
	"      Configure or start/stop the hang detect timer\n"
	"  hello\n"
	"      Checks for basic communication with EC\n"
	"  hibdelay [sec]\n"
	"      Set the delay before going into hibernation\n"
	"  hostsleepstate\n"
	"      Report host sleep state to the EC\n"
	"  hostevent\n"
	"      Get & set host event masks.\n"
	"  i2cprotect <port> [status]\n"
	"      Protect EC's I2C bus\n"
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
	"  kbfactorytest\n"
	"      Scan out keyboard if any pins are shorted\n"
	"  kbid\n"
	"      Get keyboard ID of supported keyboards\n"
	"  kbinfo\n"
	"      Dump keyboard matrix dimensions\n"
	"  kbpress\n"
	"      Simulate key press\n"
	"  keyscan <beat_us> <filename>\n"
	"      Test low-level key scanning\n"
	"  led <name> <query | auto | off | <color> | <color>=<value>...>\n"
	"      Set the color of an LED or query brightness range\n"
	"  lightbar [CMDS]\n"
	"      Various lightbar control commands\n"
	"  mkbpget <buttons|switches>\n"
	"      Get MKBP buttons/switches supported mask and current state\n"
	"  mkbpwakemask <get|set> <event|hostevent> [mask]\n"
	"      Get or Set the MKBP event wake mask, or host event wake mask\n"
	"  motionsense [CMDS]\n"
	"      Various motion sense control commands\n"
	"  panicinfo\n"
	"      Prints saved panic info\n"
	"  pause_in_s5 [on|off]\n"
	"      Whether or not the AP should pause in S5 on shutdown\n"
	"  pdcontrol [suspend|resume|reset|disable|on]\n"
	"      Controls the PD chip\n"
	"  pdchipinfo <port>\n"
	"      Get PD chip information\n"
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
	"      Prints power-related information\n"
	"  protoinfo\n"
	"       Prints EC host protocol information\n"
	"  pse\n"
	"      Get and set PoE PSE port power status\n"
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
	"  pwmgetduty\n"
	"      Prints the current 16 bit duty cycle for given PWM\n"
	"  pwmsetfanrpm <targetrpm>\n"
	"      Set target fan RPM\n"
	"  pwmsetkblight <percent>\n"
	"      Set keyboard backlight in percent\n"
	"  pwmsetduty\n"
	"      Set 16 bit duty cycle of given PWM\n"
	"  rand <num_bytes>\n"
	"      generate <num_bytes> of random numbers\n"
	"  readtest <patternoffset> <size>\n"
	"      Reads a pattern from the EC via LPC\n"
	"  reboot_ec <RO|RW|cold|hibernate|hibernate-clear-ap-off|disable-jump>"
			" [at-shutdown|switch-slot]\n"
	"      Reboot EC to RO or RW\n"
	"  reboot_ap_on_g3\n"
	"      Requests that the EC will automatically reboot the AP the next time\n"
	"      we enter the G3 power state.\n"
	"  rollbackinfo\n"
	"      Print rollback block information\n"
	"  rtcget\n"
	"      Print real-time clock\n"
	"  rtcgetalarm\n"
	"      Print # of seconds before real-time clock alarm goes off.\n"
	"  rtcset <time>\n"
	"      Set real-time clock\n"
	"  rtcsetalarm <sec>\n"
	"      Set real-time clock alarm to go off in <sec> seconds\n"
	"  rwhashpd <dev_id> <HASH[0] ... <HASH[4]>\n"
	"      Set entry in PD MCU's device rw_hash table.\n"
	"  rwsig <info|dump|action|status> ...\n"
	"      info: get all info about rwsig\n"
	"      dump: show individual rwsig field\n"
	"      action: Control the behavior of RWSIG task.\n"
	"      status: Run RW signature verification and get status.\n{"
	"  rwsigaction (DEPRECATED; use \"rwsig action\")\n"
	"      Control the behavior of RWSIG task.\n"
	"  rwsigstatus (DEPRECATED; use \"rwsig status\"\n"
	"      Run RW signature verification and get status.\n"
	"  sertest\n"
	"      Serial output test for COM2\n"
	"  smartdischarge\n"
	"      Set/Get smart discharge parameters\n"
	"  stress [reboot] [help]\n"
	"      Stress test the ec host command interface.\n"
	"  sysinfo [flags|reset_flags|firmware_copy]\n"
	"      Display system info.\n"
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
	"  tpselftest\n"
	"      Run touchpad self test.\n"
	"  tpframeget\n"
	"      Get touchpad frame data.\n"
	"  tmp006cal <tmp006_index> [params...]\n"
	"      Get/set TMP006 calibration\n"
	"  tmp006raw <tmp006_index>\n"
	"      Get raw TMP006 data\n"
	"  typeccontrol <port> <command>\n"
	"      Control USB PD policy\n"
	"  typecdiscovery <port> <type>\n"
	"      Get discovery information for port and type\n"
	"  typecstatus <port>\n"
	"      Get status information for port\n"
	"  uptimeinfo\n"
	"      Get info about how long the EC has been running and the most\n"
	"      recent AP resets\n"
	"  usbchargemode <port> <mode> [<inhibit_charge>]\n"
	"      Set USB charging mode\n"
	"  usbmux <mux>\n"
	"      Set USB mux switch state\n"
	"  usbpd <port> <auto | "
			"[toggle|toggle-off|sink|source] [none|usb|dp|dock] "
			"[dr_swap|pr_swap|vconn_swap]>\n"
	"      Control USB PD/type-C [deprecated]\n"
	"  usbpdmuxinfo\n"
	"      Get USB-C SS mux info\n"
	"  usbpdpower [port]\n"
	"      Get USB PD power information\n"
	"  version\n"
	"      Prints EC version\n"
	"  waitevent <type> [<timeout>]\n"
	"      Wait for the MKBP event of type and display it\n"
	"  wireless <flags> [<mask> [<suspend_flags> <suspend_mask>]]\n"
	"      Enable/disable WLAN/Bluetooth radio\n"
	"";

/* Note: depends on enum ec_image */
static const char * const image_names[] = {"unknown", "RO", "RW"};

/* Note: depends on enum ec_led_colors */
static const char * const led_color_names[] = {
	"red", "green", "blue", "yellow", "white", "amber"};
BUILD_ASSERT(ARRAY_SIZE(led_color_names) == EC_LED_COLOR_COUNT);

/* Note: depends on enum ec_led_id */
static const char * const led_names[] = {
	"battery", "power", "adapter", "left", "right", "recovery_hwreinit",
	"sysrq debug" };
BUILD_ASSERT(ARRAY_SIZE(led_names) == EC_LED_ID_COUNT);

/* ASCII mode for printing, default off */
static int ascii_mode = 0;

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
	printf("Usage: %s [--dev=n] [--interface=dev|i2c|lpc] [--i2c_bus=n]",
	       prog);
	printf("[--name=cros_ec|cros_fp|cros_pd|cros_scp|cros_ish] [--ascii] ");
	printf("<command> [params]\n\n");
	printf("  --i2c_bus=n  Specifies the number of an I2C bus to use. For\n"
	       "               example, to use /dev/i2c-7, pass --i2c_bus=7.\n"
	       "               Implies --interface=i2c.\n\n");
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

	p.adc_channel = (uint8_t)strtoul(argv[1], &e, 0);
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
	struct ec_params_rollback_add_entropy p;
	int rv;
	int tries = 100; /* Wait for 10 seconds at most */

	if (argc >= 2 && !strcmp(argv[1], "reset"))
		p.action = ADD_ENTROPY_RESET_ASYNC;
	else
		p.action = ADD_ENTROPY_ASYNC;

	rv = ec_command(EC_CMD_ADD_ENTROPY, 0, &p, sizeof(p), NULL, 0);

	if (rv != EC_RES_SUCCESS)
		goto out;

	while (tries--) {
		usleep(100000);

		p.action = ADD_ENTROPY_GET_RESULT;
		rv = ec_command(EC_CMD_ADD_ENTROPY, 0, &p, sizeof(p), NULL, 0);

		if (rv == EC_RES_SUCCESS) {
			printf("Entropy added successfully\n");
			return EC_RES_SUCCESS;
		}

		/* Abort if EC returns an error other than EC_RES_BUSY. */
		if (rv <= -EECRESULT && rv != -EECRESULT-EC_RES_BUSY)
			goto out;
	}

	rv = -EECRESULT-EC_RES_TIMEOUT;
out:
	fprintf(stderr, "Failed to add entropy: %d\n", rv);
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
		p.seconds = strtoul(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "invalid number\n");
			return -1;
		}
	}

	rv = ec_command(EC_CMD_HIBERNATION_DELAY, 0, &p, sizeof(p),
			&r, sizeof(r));
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
	"      8: EC_HOST_EVENT_LAZY_WAKE_MASK_S5\n"
		, cmd, cmd);
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
		p.value = strtoul(argv[3], &e, 0);
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
	rv = ec_command(EC_CMD_GET_CMD_VERSIONS, 0, &p, sizeof(p),
			&r, sizeof(r));
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
		fprintf(stderr, "Usage: %s "
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
					strtoul(argv[2], &afterscan, 0);

				if ((*afterscan != '\0') ||
				    (afterscan == argv[2])) {
					fprintf(stderr,
						"Invalid value: %s\n",
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
		       timeout ? "Timeout: " : "",
		       transitions);
	}

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

/*
 * Convert a reset cause ID to human-readable string, providing total coverage
 * of the 'cause' space.  The returned string points to static storage and must
 * not be free()ed.
 */
static const char *reset_cause_to_str(uint16_t cause)
{
	static const char * const reset_causes[] = {
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

	static const char * const shutdown_causes[] = {
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
	static const char * const reset_flag_descs[] = {
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

	printf("EC uptime: %d.%03d seconds\n",
		r.time_since_ec_boot_ms / 1000,
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
	struct ec_response_get_version r;
	char *build_string = (char *)ec_inbuf;
	int rv;

	rv = ec_command(EC_CMD_GET_VERSION, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0) {
		fprintf(stderr, "ERROR: EC_CMD_GET_VERSION failed: %d\n", rv);
		goto exit;
	}
	rv = ec_command(EC_CMD_GET_BUILD_INFO, 0,
			NULL, 0, ec_inbuf, ec_max_insize);
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

	/* Print versions */
	printf("RO version:    %s\n", r.version_string_ro);
	printf("RW version:    %s\n", r.version_string_rw);
	printf("Firmware copy: %s\n",
	       (r.current_image < ARRAY_SIZE(image_names) ?
		image_names[r.current_image] : "?"));
	printf("Build info:    %s\n", build_string);
exit:
	printf("Tool version:  %s %s %s\n", CROS_ECTOOL_VERSION, DATE, BUILDER);

	return rv;
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
	if ((e && *e) || size <= 0 || size > MAX_FLASH_SIZE) {
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
	else if (!strcmp(argv[1], "RW"))
		p.cmd = EC_REBOOT_JUMP_RW;
	else if (!strcmp(argv[1], "cold"))
		p.cmd = EC_REBOOT_COLD;
	else if (!strcmp(argv[1], "disable-jump"))
		p.cmd = EC_REBOOT_DISABLE_JUMP;
	else if (!strcmp(argv[1], "hibernate"))
		p.cmd = EC_REBOOT_HIBERNATE;
	else if (!strcmp(argv[1], "hibernate-clear-ap-off"))
		p.cmd = EC_REBOOT_HIBERNATE_CLEAR_AP_OFF;
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
	int rv;

	rv = ec_command(EC_CMD_REBOOT_AP_ON_G3, 0, NULL, 0, NULL, 0);
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
		printf("WriteIdealSize %d\nFlags 0x%x\n",
		       r.write_ideal_size, r.flags);
	}

	return 0;
}

int cmd_rand(int argc, char *argv[])
{
	struct ec_params_rand_num p;
	struct ec_response_rand_num *r;
	size_t r_size;
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

	r = ec_inbuf;

	for (i = 0; i < num_bytes; i += ec_max_insize) {
		p.num_rand_bytes = ec_max_insize;
		if (num_bytes - i < p.num_rand_bytes)
			p.num_rand_bytes = num_bytes - i;

		r_size = p.num_rand_bytes;

		rv = ec_command(EC_CMD_RAND_NUM, EC_VER_RAND_NUM, &p, sizeof(p),
				r, r_size);
		if (rv < 0) {
			fprintf(stderr, "Random number command failed\n");
			return -1;
		}

		rv = write(STDOUT_FILENO, r->rand, r_size);
		if (rv != r_size) {
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
	char *buf;

	if (argc < 4) {
		fprintf(stderr,
			"Usage: %s <offset> <size> <filename>\n", argv[0]);
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


static void print_flash_protect_flags(const char *desc, uint32_t flags)
{
	printf("%s 0x%08x", desc, flags);
	if (flags & EC_FLASH_PROTECT_GPIO_ASSERTED)
		printf(" wp_gpio_asserted");
	if (flags & EC_FLASH_PROTECT_RO_AT_BOOT)
		printf(" ro_at_boot");
	if (flags & EC_FLASH_PROTECT_RW_AT_BOOT)
		printf(" rw_at_boot");
	if (flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT)
		printf(" rollback_at_boot");
	if (flags & EC_FLASH_PROTECT_ALL_AT_BOOT)
		printf(" all_at_boot");
	if (flags & EC_FLASH_PROTECT_RO_NOW)
		printf(" ro_now");
	if (flags & EC_FLASH_PROTECT_RW_NOW)
		printf(" rw_now");
	if (flags & EC_FLASH_PROTECT_ROLLBACK_NOW)
		printf(" rollback_now");
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

int cmd_rwsig_status(int argc, char *argv[])
{
	int rv;
	struct ec_response_rwsig_check_status resp;

	rv = ec_command(EC_CMD_RWSIG_CHECK_STATUS, 0, NULL, 0,
			&resp, sizeof(resp));
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
	RWSIG_INFO_FIELD_ALL = RWSIG_INFO_FIELD_SIG_ALG |
		RWSIG_INFO_FIELD_KEY_VERSION | RWSIG_INFO_FIELD_HASH_ALG |
		RWSIG_INFO_FIELD_KEY_IS_VALID | RWSIG_INFO_FIELD_KEY_ID
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
	SYSINFO_FIELD_RESET_FLAGS = BIT(0),
	SYSINFO_FIELD_CURRENT_IMAGE = BIT(1),
	SYSINFO_FIELD_FLAGS = BIT(2),
	SYSINFO_INFO_FIELD_ALL = SYSINFO_FIELD_RESET_FLAGS |
				 SYSINFO_FIELD_CURRENT_IMAGE |
				 SYSINFO_FIELD_FLAGS
};

static int sysinfo(struct ec_response_sysinfo *info)
{
	struct ec_response_sysinfo r;
	int rv;

	rv = ec_command(EC_CMD_SYSINFO, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0) {
		fprintf(stderr, "ERROR: EC_CMD_SYSINFO failed: %d\n", rv);
		return rv;
	}

	return 0;
}

int cmd_sysinfo(int argc, char **argv)
{
	struct ec_response_sysinfo r = {};
	enum sysinfo_fields fields = 0;
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
	fprintf(stderr, "Usage: %s "
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
	int rsize = cmdver == 1 ? sizeof(*info)
				: sizeof(struct ec_response_fp_info_v0);

	/* templates not supported in command v0 */
	if (index > 0 && cmdver == 0)
		return NULL;

	rv = ec_command(EC_CMD_FP_INFO, cmdver, NULL, 0, info, rsize);
	if (rv < 0)
		return NULL;

	if (index == FP_FRAME_INDEX_SIMPLE_IMAGE) {
		size = (size_t)info->width * info->bpp/8 * info->height;
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

	ptr = buffer;
	p.offset = index << FP_FRAME_INDEX_SHIFT;
	while (size) {
		stride = MIN(ec_max_insize, size);
		p.size = stride;
		rv = ec_command(EC_CMD_FP_FRAME, 0, &p, sizeof(p),
				ptr, stride);
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
	int rsize = cmdver == 1 ? sizeof(r)
				: sizeof(struct ec_response_fp_info_v0);
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

static void print_fp_enc_flags(const char *desc, uint32_t flags)
{
	printf("%s 0x%08x", desc, flags);
	if (flags & FP_ENC_STATUS_SEED_SET)
		printf(" FPTPM_seed_set");
	printf("\n");
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
	struct ec_response_fp_encryption_status resp = { 0 };

	rv = ec_command(EC_CMD_FP_ENC_STATUS, 0, NULL, 0, &resp, sizeof(resp));
	if (rv < 0) {
		printf("Get FP sensor encryption status failed.\n");
	} else {
		print_fp_enc_flags("FPMCU encryption status:", resp.status);
		print_fp_enc_flags("Valid flags:            ",
				   resp.valid_flags);
		rv = 0;
	}
	return rv;
}

int cmd_fp_frame(int argc, char *argv[])
{
	struct ec_response_fp_info r;
	int idx = (argc == 2 && !strcasecmp(argv[1], "raw")) ?
		FP_FRAME_INDEX_RAW_IMAGE : FP_FRAME_INDEX_SIMPLE_IMAGE;
	void *buffer = fp_download_frame(&r, idx);
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
	struct ec_params_fp_template *p = ec_outbuf;
	/* TODO(b/78544921): removing 32 bits is a workaround for the MCU bug */
	int max_chunk = ec_max_outsize
			- offsetof(struct ec_params_fp_template, data) - 4;
	int idx = -1;
	char *e;
	int size;
	void *buffer = NULL;
	uint32_t offset = 0;
	int rv = 0;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s [<infile>|<index>]\n", argv[0]);
		return -1;
	}

	idx = strtol(argv[1], &e, 0);
	if (!(e && *e)) {
		buffer = fp_download_frame(&r, idx + 1);
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
		rv = ec_command(EC_CMD_FP_TEMPLATE, 0, p, tlen +
				offsetof(struct ec_params_fp_template, data),
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
	struct ec_params_smart_discharge *p = ec_outbuf;
	struct ec_response_smart_discharge *r = ec_inbuf;
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

	rv = ec_command(EC_CMD_SMART_DISCHARGE, 0, p, sizeof(*p),
			r, ec_max_insize);
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
	printf("%-27s %5d mAh (%d %%)\n", "Stay-up threshold:",
	       r->dzone.stayup, cap > 0 ? r->dzone.stayup * 100 / cap : -1);
	printf("%-27s %5d mAh (%d %%)\n", "Cutoff threshold:",
	       r->dzone.cutoff, cap > 0 ? r->dzone.cutoff * 100 / cap : -1);
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

	printf("Stress test tool version: %s %s %s\n",
	       CROS_ECTOOL_VERSION, DATE, BUILDER);

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
		rv = ec_command(EC_CMD_GET_VERSION, 0,
				NULL, 0, &ver_r, sizeof(ver_r));
		if (rv < 0) {
			failures++;
			perror("ERROR: EC_CMD_GET_VERSION failed");
		}
		ver_r.version_string_ro[sizeof(ver_r.version_string_ro) - 1]
			= '\0';
		ver_r.version_string_rw[sizeof(ver_r.version_string_rw) - 1]
			= '\0';
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
		rv = ec_command(EC_CMD_GET_BUILD_INFO, 0,
				NULL, 0, ec_inbuf, ec_max_insize);
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
			       attempt, round,
			       difftime(now, last_update_time));
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
				printf("%d: %d K\n", id,
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
		printf("%d K\n", rv + EC_TEMP_SENSOR_OFFSET);
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
			if (read_mapped_temperature(p.id) ==
			    EC_TEMP_SENSOR_NOT_PRESENT)
				continue;
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

	rv = ec_command(EC_CMD_PWM_SET_DUTY, 0,
			&p, sizeof(p), NULL, 0);
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
	{ ST_PRM_SIZE(set_brightness), 0},
	{ ST_PRM_SIZE(seq), 0},
	{ ST_PRM_SIZE(reg), 0},
	{ ST_PRM_SIZE(set_rgb), 0},
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_seq) },
	{ ST_PRM_SIZE(demo), 0},
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v0) },
	{ ST_PRM_SIZE(set_params_v0), 0},
	{ ST_CMD_SIZE, ST_RSP_SIZE(version) },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_brightness) },
	{ ST_PRM_SIZE(get_rgb), ST_RSP_SIZE(get_rgb) },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_demo) },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v1) },
	{ ST_PRM_SIZE(set_params_v1), 0},
	{ ST_PRM_SIZE(set_program), 0},
	{ ST_PRM_SIZE(manual_suspend_ctrl), 0},
	{ ST_CMD_SIZE, 0 },
	{ ST_CMD_SIZE, 0 },
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v2_timing) },
	{ ST_PRM_SIZE(set_v2par_timing), 0},
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v2_tap) },
	{ ST_PRM_SIZE(set_v2par_tap), 0},
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v2_osc) },
	{ ST_PRM_SIZE(set_v2par_osc), 0},
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v2_bright) },
	{ ST_PRM_SIZE(set_v2par_bright), 0},
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v2_thlds) },
	{ ST_PRM_SIZE(set_v2par_thlds), 0},
	{ ST_CMD_SIZE, ST_RSP_SIZE(get_params_v2_colors) },
	{ ST_PRM_SIZE(set_v2par_colors), 0},
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
#define ST_CMD_SIZE ST_FLD_SIZE(ec_params_motion_sense, cmd)
#define ST_PRM_SIZE(SUBCMD) \
	(ST_CMD_SIZE + ST_FLD_SIZE(ec_params_motion_sense, SUBCMD))
#define ST_RSP_SIZE(SUBCMD) ST_FLD_SIZE(ec_response_motion_sense, SUBCMD)
#define ST_BOTH_SIZES(SUBCMD) { ST_PRM_SIZE(SUBCMD), ST_RSP_SIZE(SUBCMD) }

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
	{
		ST_PRM_SIZE(dump),
		ST_RSP_SIZE(dump) +
		sizeof(struct ec_response_motion_sensor_data) *
		ECTOOL_MAX_SENSOR
	},
	ST_BOTH_SIZES(info_4),
	ST_BOTH_SIZES(ec_rate),
	ST_BOTH_SIZES(sensor_odr),
	ST_BOTH_SIZES(sensor_range),
	ST_BOTH_SIZES(kb_wake_angle),
	ST_BOTH_SIZES(data),
	{
		ST_CMD_SIZE,
		ST_RSP_SIZE(fifo_info) + sizeof(uint16_t) * ECTOOL_MAX_SENSOR
	},
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
	printf("  %s                              - dump all motion data\n", cmd);
	printf("  %s active                       - print active flag\n", cmd);
	printf("  %s info NUM                     - print sensor info\n", cmd);
	printf("  %s ec_rate [RATE_MS]            - set/get sample rate\n", cmd);
	printf("  %s odr NUM [ODR [ROUNDUP]]      - set/get sensor ODR\n", cmd);
	printf("  %s range NUM [RANGE [ROUNDUP]]  - set/get sensor range\n", cmd);
	printf("  %s offset NUM [-- X Y Z [TEMP]] - set/get sensor offset\n", cmd);
	printf("  %s kb_wake NUM                  - set/get KB wake ang\n", cmd);
	printf("  %s fifo_info                    - print fifo info\n", cmd);
	printf("  %s fifo_int_enable [0/1]        - enable/disable/get fifo interrupt "
		"status\n", cmd);
	printf("  %s fifo_read MAX_DATA           - read fifo data\n", cmd);
	printf("  %s fifo_flush NUM               - trigger fifo interrupt\n", cmd);
	printf("  %s list_activities NUM          - list supported activities\n", cmd);
	printf("  %s set_activity NUM ACT EN      - enable/disable activity\n", cmd);
	printf("  %s lid_angle                    - print lid angle\n", cmd);
	printf("  %s spoof -- NUM [0/1] [X Y Z]   - enable/disable spoofing\n", cmd);
	printf("  %s tablet_mode_angle ANG HYS    - set/get tablet mode angle\n", cmd);
	printf("  %s calibrate NUM                - run sensor calibration\n", cmd);

	return 0;
}

static void motionsense_display_activities(uint32_t activities)
{
	if (activities & BIT(MOTIONSENSE_ACTIVITY_SIG_MOTION))
		printf("%d: Significant motion\n",
		       MOTIONSENSE_ACTIVITY_SIG_MOTION);
	if (activities & BIT(MOTIONSENSE_ACTIVITY_DOUBLE_TAP))
		printf("%d: Double tap\n",
		       MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
	if (activities & BIT(MOTIONSENSE_ACTIVITY_ORIENTATION))
		printf("%d: Orientation\n",
		       MOTIONSENSE_ACTIVITY_ORIENTATION);
	if (activities & BIT(MOTIONSENSE_ACTIVITY_BODY_DETECTION))
		printf("%d: Body Detection\n",
		       MOTIONSENSE_ACTIVITY_BODY_DETECTION);
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

	/* No motionsense command has more than 7 args. */
	if (argc > 7)
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
		int version = 0;

		rv = get_latest_cmd_version(EC_CMD_MOTION_SENSE_CMD, &version);
		if (rv < 0)
			return rv;

		param.cmd = MOTIONSENSE_CMD_INFO;
		param.sensor_odr.sensor_num = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad %s arg.\n", argv[2]);
			return -1;
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, version,
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
			param.tablet_mode_threshold.lid_angle = strtol(argv[2],
								     &e, 0);

			if (e && *e) {
				fprintf(stderr, "Bad %s arg.\n", argv[2]);
				return -1;
			}

			param.tablet_mode_threshold.hys_degree = strtol(argv[3],
								     &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad %s arg.\n", argv[3]);
				return -1;
			}
		} else if (argc != 2) {
			return ms_help(argv[0]);
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);

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
		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);
		if (rv < 0)
			return rv;
		sensor_count = resp->dump.sensor_count;

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
		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);
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
					printf("Sensor %d: %d\t%d\t%d "
					       "(as uint16: %u\t%u\t%u)\n",
						vector->sensor_num,
						vector->data[0],
						vector->data[1],
						vector->data[2],
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

	if (argc == 3 && !strcasecmp(argv[1], "calibrate")) {
		param.cmd = MOTIONSENSE_CMD_PERFORM_CALIB;
		param.perform_calib.enable = 1;
		param.perform_calib.sensor_num = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad %s arg.\n", argv[2]);
			return -1;
		}

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 1,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);

		if (rv < 0)
			return rv;

		printf("--- Calibrated well ---\n");
		printf("New offset vector: X:%d, Y:%d, Z:%d\n",
			resp->perform_calib.offset[0],
			resp->perform_calib.offset[1],
			resp->perform_calib.offset[2]);
		if ((uint16_t)resp->perform_calib.temp ==
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
				param.sensor_offset.flags = MOTION_SENSE_SET_OFFSET;
				for (i = 0; i < 3; i++) {
					param.sensor_offset.offset[i] = strtol(argv[3+i], &e, 0);
					if (e && *e) {
						fprintf(stderr, "Bad %s arg.\n", argv[3+i]);
						return -1;
					}
				}
				if (argc == 7) {
					/* Set offset : Temperature */
					param.sensor_offset.temp = strtol(argv[6], &e, 0);
					if (e && *e) {
						fprintf(stderr, "Bad %s arg.\n", argv[6]);
						return -1;
					}
				}
			} else {
				return ms_help(argv[0]);
			}
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

	if (argc == 3 && !strcasecmp(argv[1], "list_activities")) {
		param.cmd = MOTIONSENSE_CMD_LIST_ACTIVITIES;
		param.list_activities.sensor_num = strtol(argv[2], &e, 0);
		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);
		if (rv < 0)
			return rv;

		printf("Enabled:\n");
		motionsense_display_activities(resp->list_activities.enabled);
		printf("Disabled:\n");
		motionsense_display_activities(resp->list_activities.disabled);
		return 0;
	}
	if (argc == 5 && !strcasecmp(argv[1], "set_activity")) {
		param.cmd = MOTIONSENSE_CMD_SET_ACTIVITY;
		param.set_activity.sensor_num = strtol(argv[2], &e, 0);
		param.set_activity.activity = strtol(argv[3], &e, 0);
		param.set_activity.enable = strtol(argv[4], &e, 0);

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);
		if (rv < 0)
			return rv;
		return 0;
	}
	if (argc == 4 && !strcasecmp(argv[1], "get_activity")) {
		param.cmd = MOTIONSENSE_CMD_GET_ACTIVITY;
		param.get_activity.sensor_num = strtol(argv[2], &e, 0);
		param.get_activity.activity = strtol(argv[3], &e, 0);

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);
		if (rv < 0)
			return rv;
		printf("State: %d\n", resp->get_activity.state);
		return 0;
	}

	if (argc == 2 && !strcasecmp(argv[1], "lid_angle")) {
		param.cmd = MOTIONSENSE_CMD_LID_ANGLE;
		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);
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

		if (argc >= 4) {
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
					val = strtol(argv[4+i], &e, 0);
					if (e && *e) {
						fprintf(stderr, "Bad %s arg.\n",
							argv[4+i]);
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

		rv = ec_command(EC_CMD_MOTION_SENSE_CMD, 2,
				&param, ms_command_sizes[param.cmd].outsize,
				resp, ms_command_sizes[param.cmd].insize);
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
		if ((e && *e) || (p.inhibit_charge != 0 &&
			p.inhibit_charge != 1)) {
			fprintf(stderr, "Bad value\n");
			return -1;
		}
	}

	printf("Setting port %d to mode %d inhibit_charge %d...\n",
		p.usb_port_id, p.mode, p.inhibit_charge);

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
	const char *role_str[] = {"", "toggle", "toggle-off", "sink", "source",
				  "freeze"};
	const char *mux_str[] = {"", "none", "usb", "dp", "dock", "auto"};
	const char *swap_str[] = {"", "dr_swap", "pr_swap", "vconn_swap"};
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
		printf("Port C%d: %s, %s  State:%s\n"
		       "Role:%s %s%s, Polarity:CC%d\n",
		       p.port,
		       (r_v1->enabled & PD_CTRL_RESP_ENABLED_COMMS) ?
				"enabled" : "disabled",
		       (r_v1->enabled & PD_CTRL_RESP_ENABLED_CONNECTED) ?
				"connected" : "disconnected",
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
					"Active" : "Passive");

			printf("TBT Adapter type:%s\n",
				r_v2->control_flags &
				USB_PD_CTRL_TBT_LEGACY_ADAPTER ?
					"Legacy" : "Gen3");

			printf("Optical Cable:%s\n",
				r_v2->control_flags &
				USB_PD_CTRL_OPTICAL_CABLE ? "True" : "False");

			printf("Link LSRX Communication:%s-directional\n",
				r_v2->control_flags &
				USB_PD_CTRL_ACTIVE_LINK_UNIDIR ? "Uni" : "Bi");

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
					" DR power\n" : "",
				(r_v1->role & PD_CTRL_RESP_ROLE_DR_DATA) ?
					" DR data\n" : "",
				(r_v1->role & PD_CTRL_RESP_ROLE_USB_COMM) ?
					" USB capable\n" : "",
				(r_v1->role & PD_CTRL_RESP_ROLE_UNCONSTRAINED) ?
					" Unconstrained power\n" : "");
	}
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

	if ((r->role == USB_PD_PORT_POWER_SOURCE) &&
	    (r->meas.current_max))
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
	printf(" %dmV / %dmA, max %dmV / %dmA",
		r->meas.voltage_now, r->meas.current_lim, r->meas.voltage_max,
		r->meas.current_max);
	if (r->max_power)
		printf(" / %dmW", r->max_power / 1000);
	printf("\n");
}

int cmd_usb_pd_mux_info(int argc, char *argv[])
{
	struct ec_params_usb_pd_mux_info p;
	struct ec_response_usb_pd_mux_info r;
	int num_ports, rv, i;

	rv = ec_command(EC_CMD_USB_PD_PORTS, 0, NULL, 0,
			ec_inbuf, ec_max_insize);
	if (rv < 0)
		return rv;
	num_ports = ((struct ec_response_usb_pd_ports *)ec_inbuf)->num_ports;

	for (i = 0; i < num_ports; i++) {
		p.port = i;
		rv = ec_command(EC_CMD_USB_PD_MUX_INFO, 0,
				&p, sizeof(p),
				&r, sizeof(r));
		if (rv < 0)
			return rv;

		printf("Port %d: ", i);
		printf("USB=%d ", !!(r.flags & USB_PD_MUX_USB_ENABLED));
		printf("DP=%d ", !!(r.flags & USB_PD_MUX_DP_ENABLED));
		printf("POLARITY=%s ", r.flags & USB_PD_MUX_POLARITY_INVERTED ?
					"INVERTED" : "NORMAL");
		printf("HPD_IRQ=%d ", !!(r.flags & USB_PD_MUX_HPD_IRQ));
		printf("HPD_LVL=%d ", !!(r.flags & USB_PD_MUX_HPD_LVL));
		printf("SAFE=%d ", !!(r.flags & USB_PD_MUX_SAFE_MODE));
		printf("TBT=%d ", !!(r.flags & USB_PD_MUX_TBT_COMPAT_ENABLED));
		printf("USB4=%d ", !!(r.flags & USB_PD_MUX_USB4_ENABLED));
		printf("\n");
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

	rv = ec_command(EC_CMD_USB_PD_PORTS, 0, NULL, 0,
			ec_inbuf, ec_max_insize);
	if (rv < 0)
		return rv;
	num_ports = ((struct ec_response_usb_pd_ports *)r)->num_ports;

	if (argc < 2) {
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
	} else {
		p.port = strtol(argv[1], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad port.\n");
			return -1;
		}
		rv = ec_command(EC_CMD_USB_PD_POWER_INFO, 0,
				&p, sizeof(p),
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

int cmd_keyboard_factory_test(int argc, char *argv[])
{
	struct ec_response_keyboard_factory_test r;
	int rv;

	rv = ec_command(EC_CMD_KEYBOARD_FACTORY_TEST, 0,
			NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;

	if (r.shorted != 0)
		printf("Keyboard %d and %d pin are shorted.\n",
				r.shorted & 0x00ff, r.shorted >> 8);
	else
		printf("Keyboard factory test passed.\n");

	return 0;
}

int cmd_panic_info(int argc, char *argv[])
{
	int rv;
	struct panic_data *pdata = (struct panic_data *)ec_inbuf;

	rv = ec_command(EC_CMD_GET_PANIC_INFO, 0, NULL, 0,
			ec_inbuf, ec_max_insize);
	if (rv < 0)
		return rv;

	if (rv == 0) {
		printf("No panic data.\n");
		return 0;
	}

	return parse_panic_info(pdata);
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


int cmd_i2c_protect(int argc, char *argv[])
{
	struct ec_params_i2c_passthru_protect p;
	char *e;
	int rv;

	if (argc != 2 && (argc != 3 || strcmp(argv[2], "status"))) {
		fprintf(stderr, "Usage: %s <port> [status]\n",
				argv[0]);
		return -1;
	}

	p.port = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port.\n");
		return -1;
	}

	if (argc == 3) {
		struct ec_response_i2c_passthru_protect r;

		p.subcmd = EC_CMD_I2C_PASSTHRU_PROTECT_STATUS;

		rv = ec_command(EC_CMD_I2C_PASSTHRU_PROTECT, 0, &p, sizeof(p),
				&r, sizeof(r));

		if (rv < 0)
			return rv;

		printf("I2C port %d: %s (%d)\n", p.port,
			r.status ? "Protected" : "Unprotected", r.status);
	} else {
		p.subcmd = EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE;

		rv = ec_command(EC_CMD_I2C_PASSTHRU_PROTECT, 0, &p, sizeof(p),
				NULL, 0);

		if (rv < 0)
			return rv;
	}
	return 0;
}


int do_i2c_xfer(unsigned int port, unsigned int addr,
		uint8_t *write_buf, int write_len,
		uint8_t **read_buf, int read_len) {
	struct ec_params_i2c_passthru *p =
		(struct ec_params_i2c_passthru *)ec_outbuf;
	struct ec_response_i2c_passthru *r =
		(struct ec_response_i2c_passthru *)ec_inbuf;
	struct ec_params_i2c_passthru_msg *msg = p->msg;
	uint8_t *pdata;
	int size;
	int rv;

	p->port = port;
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

		memcpy(pdata, write_buf, write_len);
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

	if (read_len)
		*read_buf = r->data;

	return 0;
}

static void cmd_i2c_help(void)
{
	fprintf(stderr,
	"  Usage: i2cread <8 | 16> <port> <addr8> <offset>\n"
	"  Usage: i2cwrite <8 | 16> <port> <addr8> <offset> <data>\n"
	"  Usage: i2cxfer <port> <addr7> <read_count> [bytes...]\n"
	"    <port> i2c port number\n"
	"    <addr8> 8-bit i2c address\n"
	"    <addr7> 7-bit i2c address\n"
	"    <offset> offset to read from or write to\n"
	"    <data> data to write\n"
	"    <read_count> number of bytes to read\n"
	"    [bytes ...] data to write\n"
	);

}

int cmd_i2c_read(int argc, char *argv[])
{
	unsigned int port, addr;
	int read_len, write_len;
	uint8_t write_buf[1];
	uint8_t *read_buf = NULL;
	char *e;
	int rv;

	if (argc != 5) {
		cmd_i2c_help();
		return -1;
	}

	read_len = strtol(argv[1], &e, 0);
	if ((e && *e) || (read_len != 8 && read_len != 16)) {
		fprintf(stderr, "Bad read size.\n");
		return -1;
	}
	read_len = read_len / 8;

	port = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port.\n");
		return -1;
	}

	addr = strtol(argv[3], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad address.\n");
		return -1;
	}
	/* Convert from 8-bit to 7-bit address */
	addr = addr >> 1;

	write_buf[0] = strtol(argv[4], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}
	write_len = 1;

	rv = do_i2c_xfer(port, addr, write_buf, write_len, &read_buf, read_len);

	if (rv < 0)
		return rv;

	printf("Read from I2C port %d at 0x%x offset 0x%x = 0x%x\n",
		port, addr, write_buf[0], *(uint16_t *)read_buf);
	return 0;
}


int cmd_i2c_write(int argc, char *argv[])
{
	unsigned int port, addr;
	int write_len;
	uint8_t write_buf[3];
	char *e;
	int rv;

	if (argc != 6) {
		cmd_i2c_help();
		return -1;
	}

	write_len = strtol(argv[1], &e, 0);
	if ((e && *e) || (write_len != 8 && write_len != 16)) {
		fprintf(stderr, "Bad write size.\n");
		return -1;
	}
	/* Include offset (length 1) */
	write_len = 1 + write_len / 8;

	port = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port.\n");
		return -1;
	}

	addr = strtol(argv[3], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad address.\n");
		return -1;
	}
	/* Convert from 8-bit to 7-bit address */
	addr = addr >> 1;

	write_buf[0] = strtol(argv[4], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}

	*((uint16_t *)&write_buf[1]) = strtol(argv[5], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad data.\n");
		return -1;
	}

	rv = do_i2c_xfer(port, addr, write_buf, write_len, NULL, 0);

	if (rv < 0)
		return rv;

	printf("Wrote 0x%x to I2C port %d at 0x%x offset 0x%x.\n",
	       *((uint16_t *)&write_buf[1]), port, addr, write_buf[0]);
	return 0;
}

int cmd_i2c_xfer(int argc, char *argv[])
{
	unsigned int port, addr;
	int read_len, write_len;
	uint8_t *write_buf = NULL;
	uint8_t *read_buf;
	char *e;
	int rv, i;

	if (argc < 4) {
		cmd_i2c_help();
		return -1;
	}

	port = strtol(argv[1], &e, 0);
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

	if (write_len) {
		write_buf = malloc(write_len);
		if (write_buf == NULL)
			return -1;
		for (i = 0; i < write_len; i++) {
			write_buf[i] = strtol(argv[i], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad write byte %d\n", i);
				free(write_buf);
				return -1;
			}
		}
	}

	rv = do_i2c_xfer(port, addr, write_buf, write_len, &read_buf, read_len);

	if (write_len)
		free(write_buf);

	if (rv)
		return rv;

	if (read_len) {
		if (ascii_mode) {
			for (i = 0; i < read_len; i++)
				printf(isprint(read_buf[i]) ? "%c" : "\\x%02x",
				       read_buf[i]);
		} else {
			printf("Read bytes:");
			for (i = 0; i < read_len; i++)
				printf(" %#02x", read_buf[i]);
		}
		printf("\n");
	} else {
		printf("Write successful.\n");
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

static const char *bus_type[] = {
	"I2C",
	"EMBEDDED"
};

int cmd_locate_chip(int argc, char *argv[])
{
	struct ec_params_locate_chip p;
	struct ec_response_locate_chip r = {0};
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

	if (r.bus_type >= EC_BUS_TYPE_COUNT
			|| r.bus_type >= ARRAY_SIZE(bus_type)) {
		fprintf(stderr, "Unknown bus type (%d)\n", r.bus_type);
		return -1;
	}

	/*
	 * When changing the format of this print, make sure FAFT
	 * (firmware_ECCbiEeprom) still passes. It may silently skip the test.
	 */
	printf("Bus: %s; Port: %d; Address: 0x%02x (7-bit format)\n",
	       bus_type[r.bus_type], r.i2c_info.port,
	       I2C_GET_ADDR(r.i2c_info.addr_flags));

	printf("reserved: 0x%x\n", r.reserved);

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


int cmd_ext_power_limit(int argc, char *argv[])
{
	/* Version 1 is used, no support for obsolete version 0 */
	struct ec_params_external_power_limit_v1 p;
	char *e;

	if (argc != 3) {
		fprintf(stderr,
			"Usage: %s <max_current_mA> <max_voltage_mV>\n",
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
	return ec_command(EC_CMD_EXTERNAL_POWER_LIMIT, 1, &p, sizeof(p),
			  NULL, 0);
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
	{ ST_PRM_SIZE(set_param), 0},
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
	"limit_power",
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
	printf("\n");
}

int get_battery_command(int index)
{
	struct ec_params_battery_static_info static_p;
	struct ec_response_battery_static_info static_r;
	struct ec_params_battery_dynamic_info dynamic_p;
	struct ec_response_battery_dynamic_info dynamic_r;
	int rv;

	printf("Battery %d info:\n", index);

	static_p.index = index;
	rv = ec_command(EC_CMD_BATTERY_GET_STATIC, 0,
			&static_p, sizeof(static_p),
			&static_r, sizeof(static_r));
	if (rv < 0)
		return -1;

	dynamic_p.index = index;
	rv = ec_command(EC_CMD_BATTERY_GET_DYNAMIC, 0,
			&dynamic_p, sizeof(dynamic_p),
			&dynamic_r, sizeof(dynamic_r));
	if (rv < 0)
		return -1;

	if (dynamic_r.flags & EC_BATT_FLAG_INVALID_DATA) {
		printf("  Invalid data (not present?)\n");
		return -1;
	}

	if (!is_string_printable(static_r.manufacturer))
		goto cmd_error;
	printf("  OEM name:               %s\n", static_r.manufacturer);

	if (!is_string_printable(static_r.model))
		goto cmd_error;
	printf("  Model number:           %s\n", static_r.model);

	if (!is_string_printable(static_r.type))
		goto cmd_error;
	printf("  Chemistry   :           %s\n", static_r.type);

	if (!is_string_printable(static_r.serial))
		goto cmd_error;
	printf("  Serial number:          %s\n", static_r.serial);

	if (!is_battery_range(static_r.design_capacity))
		goto cmd_error;
	printf("  Design capacity:        %u mAh\n", static_r.design_capacity);

	if (!is_battery_range(dynamic_r.full_capacity))
		goto cmd_error;
	printf("  Last full charge:       %u mAh\n", dynamic_r.full_capacity);

	if (!is_battery_range(static_r.design_voltage))
		goto cmd_error;
	printf("  Design output voltage   %u mV\n", static_r.design_voltage);

	if (!is_battery_range(static_r.cycle_count))
		goto cmd_error;
	printf("  Cycle count             %u\n", static_r.cycle_count);

	if (!is_battery_range(dynamic_r.actual_voltage))
		goto cmd_error;
	printf("  Present voltage         %u mV\n", dynamic_r.actual_voltage);

	/* current can be negative */
	printf("  Present current         %d mA\n", dynamic_r.actual_current);

	if (!is_battery_range(dynamic_r.remaining_capacity))
		goto cmd_error;
	printf("  Remaining capacity      %u mAh\n",
						dynamic_r.remaining_capacity);

	if (!is_battery_range(dynamic_r.desired_voltage))
		goto cmd_error;
	printf("  Desired voltage         %u mV\n", dynamic_r.desired_voltage);

	if (!is_battery_range(dynamic_r.desired_current))
		goto cmd_error;
	printf("  Desired current         %u mA\n", dynamic_r.desired_current);

	print_battery_flags(dynamic_r.flags);
	return 0;

cmd_error:
	fprintf(stderr, "Bad battery info value.\n");
	return -1;
}

int cmd_battery(int argc, char *argv[])
{
	char batt_text[EC_MEMMAP_TEXT_MAX];
	int rv, val;
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

		if (index > 0)
			return get_battery_command(index);
	}

	/*
	 * TODO(b:65697620): When supported/required, read battery 0 information
	 * through EC commands as well.
	 */

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
	print_battery_flags(val);

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

static void cmd_cbi_help(char *cmd)
{
	fprintf(stderr,
	"  Usage: %s get <tag> [get_flag]\n"
	"  Usage: %s set <tag> <value/string> <size> [set_flag]\n"
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
	"    <size> is the size of the data in byte. It should be zero for\n"
	"      string types.\n"
	"    <value/string> is an integer or a string to be set\n"
	"    [get_flag] is combination of:\n"
	"      01b: Invalidate cache and reload data from EEPROM\n"
	"    [set_flag] is combination of:\n"
	"      01b: Skip write to EEPROM. Use for back-to-back writes\n"
	"      10b: Set all fields to defaults first\n", cmd, cmd, cmd);
}

static int cmd_cbi_is_string_field(enum cbi_data_tag tag)
{
	return tag == CBI_TAG_DRAM_PART_NUM || tag == CBI_TAG_OEM_NAME;
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

	if (argc < 3) {
		fprintf(stderr, "Invalid number of params\n");
		cmd_cbi_help(argv[0]);
		return -1;
	}

	/* Tag */
	tag = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad tag\n");
		return -1;
	}

	if (!strcasecmp(argv[1], "get")) {
		struct ec_params_get_cbi p = { 0 };
		int i;

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
		} else {
			const uint8_t * const buffer = ec_inbuf;

			if (rv <= sizeof(uint32_t)) {
				uint32_t int_value = 0;

				for (i = 0; i < rv; i++)
					int_value |= buffer[i] << (i * 8);

				printf("As uint: %u (0x%x)\n", int_value,
				       int_value);
			}
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
		uint32_t val;
		uint8_t size;
		if (argc < 5) {
			fprintf(stderr, "Invalid number of params\n");
			cmd_cbi_help(argv[0]);
			return -1;
		}
		memset(p, 0, ec_max_outsize);
		p->tag = tag;

		if (cmd_cbi_is_string_field(tag)) {
			val_ptr = argv[3];
			size = strlen(val_ptr) + 1;
		} else {
			val = strtol(argv[3], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad value\n");
				return -1;
			}
			size = strtol(argv[4], &e, 0);
			if ((e && *e) || size < 1 || 4 < size ||
					val >= (1ull << size*8)) {
				fprintf(stderr, "Bad size: %d\n", size);
				return -1;
			}
			val_ptr = &val;
		}

		if (size > ec_max_outsize - sizeof(*p)) {
			fprintf(stderr, "Size exceeds parameter buffer: %d\n",
				size);
			return -1;
		}
		/* Little endian */
		memcpy(p->data, val_ptr, size);
		p->size = size;
		if (argc > 5) {
			p->flag = strtol(argv[5], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad flag\n");
				return -1;
			}
		}
		rv = ec_command(EC_CMD_SET_CROS_BOARD_INFO, 0,
				p, sizeof(*p) + size, NULL, 0);
		if (rv < 0) {
			if (rv == -EC_RES_ACCESS_DENIED - EECRESULT)
				fprintf(stderr, "Write-protect is enabled or "
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
		rv = ec_command(EC_CMD_SET_CROS_BOARD_INFO, 0,
				&p, sizeof(p), NULL, 0);
		if (rv < 0) {
			if (rv == -EC_RES_ACCESS_DENIED - EECRESULT)
				fprintf(stderr, "Write-protect is enabled or "
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

	rv = ec_command(EC_CMD_GET_PROTOCOL_INFO, 0, NULL, 0,
			&info, sizeof(info));
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

static int cmd_kbid(int argc, char *argv[])
{
	struct ec_response_keyboard_id response;
	int rv;

	if (argc > 1) {
		fprintf(stderr, "Too many args\n");
		return -1;
	}

	rv = ec_command(EC_CMD_GET_KEYBOARD_ID, 0, NULL, 0, &response,
			sizeof(response));
	if (rv < 0)
		return rv;
	switch (response.keyboard_id) {
	case KEYBOARD_ID_UNSUPPORTED:
		/* Keyboard ID was not supported */
		printf("Keyboard doesn't support ID\n");
		break;
	case KEYBOARD_ID_UNREADABLE:
		/* Ghosting ID was detected */
		printf("Reboot and keep hands off the keyboard during"
		       " next boot-up\n");
		break;
	default:
		/* Valid keyboard ID value was reported*/
		printf("%x\n", response.keyboard_id);
	}
	return rv;
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

static const char * const mkbp_button_strings[] = {
	[EC_MKBP_POWER_BUTTON] = "Power",
	[EC_MKBP_VOL_UP] = "Volume up",
	[EC_MKBP_VOL_DOWN] = "Volume down",
	[EC_MKBP_RECOVERY] = "Recovery",
};

static const char * const mkbp_switch_strings[] = {
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
	rv = ec_command(EC_CMD_MKBP_INFO, 0, &p, sizeof(p), &r,
			sizeof(r));
	if (rv < 0)
		return rv;
	if (p.event_type == EC_MKBP_EVENT_BUTTON)
		supported = r.buttons;
	else if (p.event_type == EC_MKBP_EVENT_SWITCH)
		supported = r.switches;
	else
		return -1;

	p.info_type = EC_MKBP_INFO_CURRENT;
	rv = ec_command(EC_CMD_MKBP_INFO, 0, &p, sizeof(p), &r,
			sizeof(r));
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
		fprintf(stderr, "Usage: %s get <event|hostevent>\n"
			"\t%s set <event|hostevent> <mask>\n", argv[0],
			argv[0]);
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

	rv = ec_command(EC_CMD_MKBP_WAKE_MASK, 0, &p, sizeof(p), &r,
			sizeof(r));
	if (rv < 0) {
		if (rv == -EECRESULT-EC_RES_INVALID_PARAM) {
			fprintf(stderr, "Unknown mask, or mask is not in use.  "
				"You may need to enable the "
				"CONFIG_MKBP_%s_WAKEUP_MASK option in the EC.\n"
				, p.mask_type == EC_MKBP_EVENT_WAKE_MASK ?
				"EVENT" : "HOSTEVENT");
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
		fprintf(stderr, "Usage: %s <port> [<live>]\n"
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

int cmd_typec_control(int argc, char *argv[])
{
	struct ec_params_typec_control p;
	char *endptr;
	int rv;

	if (argc < 3) {
		fprintf(stderr,
			"Usage: %s <port> <command> [args]\n"
			"  <port> is the type-c port to query\n"
			"  <type> is one of:\n"
			"    0: Exit modes\n"
			"    1: Clear events\n", argv[0]);
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

	if (p.command == TYPEC_CONTROL_COMMAND_CLEAR_EVENTS) {
		if (argc < 4) {
			fprintf(stderr, "Missing event mask\n");
			return -1;
		}

		p.clear_events_mask = strtol(argv[3], &endptr, 0);
		if (endptr && *endptr) {
			fprintf(stderr, "Bad event mask\n");
			return -1;
		}
	}

	rv = ec_command(EC_CMD_TYPEC_CONTROL, 0, &p, sizeof(p),
			ec_inbuf, ec_max_insize);
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
			"    1: SOP prime\n", argv[0]);
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

	rv = ec_command(EC_CMD_TYPEC_DISCOVERY, 0, &p, sizeof(p),
			ec_inbuf, ec_max_insize);
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

int cmd_typec_status(int argc, char *argv[])
{
	struct ec_params_typec_status p;
	struct ec_response_typec_status *r =
				(struct ec_response_typec_status *)ec_inbuf;
	char *endptr;
	int rv;
	char *desc;

	if (argc != 2) {
		fprintf(stderr,
			"Usage: %s <port>\n"
			"  <port> is the type-c port to query\n", argv[0]);
		return -1;
	}

	p.port = strtol(argv[1], &endptr, 0);
	if (endptr && *endptr) {
		fprintf(stderr, "Bad port\n");
		return -1;
	}

	rv = ec_command(EC_CMD_TYPEC_STATUS, 0, &p, sizeof(p),
			ec_inbuf, ec_max_insize);
	if (rv == -EC_RES_INVALID_COMMAND - EECRESULT)
		/* Fall back to PD_CONTROL to support older ECs */
		return cmd_usb_pd(argc, argv);
	else if (rv < 0)
		return -1;

	printf("Port C%d: %s, %s  State:%s\n"
	       "Role:%s %s%s, Polarity:CC%d\n",
		p.port,
		r->pd_enabled ? "enabled" : "disabled",
		r->dev_connected ? "connected" : "disconnected",
		r->tc_state,
		(r->power_role == PD_ROLE_SOURCE) ? "SRC" : "SNK",
		(r->data_role == PD_ROLE_DFP) ? "DFP" :
			(r->data_role == PD_ROLE_UFP) ? "UFP" : "",
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
						"INVERTED" : "NORMAL",
		       !!(r->mux_state & USB_PD_MUX_HPD_IRQ),
		       !!(r->mux_state & USB_PD_MUX_HPD_LVL),
		       !!(r->mux_state & USB_PD_MUX_SAFE_MODE),
		       !!(r->mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED),
		       !!(r->mux_state & USB_PD_MUX_USB4_ENABLED));
	}

	printf("Port events: 0x%08x\n", r->events);

	return 0;
}

int cmd_tp_self_test(int argc, char* argv[])
{
	int rv;

	rv = ec_command(EC_CMD_TP_SELF_TEST, 0, NULL, 0, NULL, 0);
	if (rv < 0)
		return rv;

	printf("Touchpad self test: %s\n",
	       rv == EC_RES_SUCCESS ? "passed" : "failed");

	return rv;
}

int cmd_tp_frame_get(int argc, char* argv[])
{
	int i, j;
	uint32_t remaining = 0, offset = 0;
	int rv = EC_SUCCESS;
	uint8_t *data;
	struct ec_response_tp_frame_info* r;
	struct ec_params_tp_frame_get p;

	data = malloc(ec_max_insize);
	r = malloc(ec_max_insize);

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

			rv = ec_command(EC_CMD_TP_FRAME_GET, 0,
					&p, sizeof(p), data, p.size);
			if (rv < 0) {
				fprintf(stderr, "Failed to get frame data "
						"at offset 0x%x\n", offset);
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

static int wait_event(long event_type,
		      struct ec_response_get_next_event_v1 *buffer,
		      size_t buffer_size, long timeout)
{
	int rv;

	rv = ec_pollevent(1 << event_type, buffer, buffer_size, timeout);
	if (rv == 0) {
		fprintf(stderr, "Timeout waiting for MKBP event\n");
		return -ETIMEDOUT;
	} else if (rv < 0) {
		perror("Error polling for MKBP event\n");
		return -EIO;
	}

	return rv;
}

int cmd_wait_event(int argc, char *argv[])
{
	int rv, i;
	struct ec_response_get_next_event_v1 buffer;
	long timeout = 5000;
	long event_type;
	char *e;

	if (!ec_pollevent) {
		fprintf(stderr, "Polling for MKBP event not supported\n");
		return -EINVAL;
	}

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <type> [<timeout>]\n",
			argv[0]);
		return -1;
	}

	event_type = strtol(argv[1], &e, 0);
	if ((e && *e) || event_type < 0 || event_type >= EC_MKBP_EVENT_COUNT) {
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

	return 0;
}

static void cmd_cec_help(const char *cmd)
{
	fprintf(stderr,
		"  Usage: %s write [write bytes...]\n"
		"    Write message on the CEC bus\n"
		"  Usage: %s read [timeout]\n"
		"    [timeout] in seconds\n"
		"  Usage: %s get <param>\n"
		"  Usage: %s set <param> <val>\n"
		"    <param> is one of:\n"
		"      address: CEC receive address\n"
		"        <val> is the new CEC address\n"
		"      enable: Enable or disable CEC\n"
		"        <val> is 1 to enable, 0 to disable\n",
		cmd, cmd, cmd, cmd);

}

static int cmd_cec_write(int argc, char *argv[])
{
	char *e;
	long val;
	int rv, i, msg_len;
	struct ec_params_cec_write p;
	struct ec_response_get_next_event_v1 buffer;

	if (argc < 3 || argc > 18) {
		fprintf(stderr, "Invalid number of params\n");
		cmd_cec_help(argv[0]);
		return -1;
	}

	msg_len = argc - 2;
	for (i = 0; i < msg_len; i++) {
		val = strtol(argv[i + 2], &e, 16);
		if (e && *e)
			return -1;
		if (val < 0 || val > 0xff)
			return -1;
		p.msg[i] = (uint8_t)val;
	}

	printf("Write to CEC: ");
	for (i = 0; i < msg_len; i++)
		printf("0x%02x ", p.msg[i]);
	printf("\n");

	rv = ec_command(EC_CMD_CEC_WRITE_MSG, 0, &p, msg_len, NULL, 0);
	if (rv < 0)
		return rv;

	rv = wait_event(EC_MKBP_EVENT_CEC_EVENT, &buffer, sizeof(buffer), 1000);
	if (rv < 0)
		return rv;

	if (buffer.data.cec_events & EC_MKBP_CEC_SEND_OK)
		return 0;

	if (buffer.data.cec_events & EC_MKBP_CEC_SEND_FAILED) {
		fprintf(stderr, "Send failed\n");
		return -1;
	}

	fprintf(stderr, "No send result received\n");

	return -1;
}

static int cmd_cec_read(int argc, char *argv[])
{
	int i, rv;
	char *e;
	struct ec_response_get_next_event_v1 buffer;
	long timeout = 5000;

	if (!ec_pollevent) {
		fprintf(stderr, "Polling for MKBP event not supported\n");
		return -EINVAL;
	}

	if (argc >= 3) {
		timeout = strtol(argv[2], &e, 0);
		if (e && *e) {
			fprintf(stderr, "Bad timeout value '%s'.\n", argv[2]);
			return -1;
		}
	}

	rv = wait_event(EC_MKBP_EVENT_CEC_MESSAGE, &buffer,
			sizeof(buffer), timeout);
	if (rv < 0)
		return rv;

	printf("CEC data: ");
	for (i = 0; i < rv - 1; i++)
		printf("0x%02x ", buffer.data.cec_message[i]);
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

static int cmd_cec_set(int argc, char *argv[])
{
	char *e;
	struct ec_params_cec_set p;
	uint8_t val;
	int cmd;

	if (argc != 4) {
		fprintf(stderr, "Invalid number of params\n");
		cmd_cec_help(argv[0]);
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
	p.val = val;

	return ec_command(EC_CMD_CEC_SET,
			  0, &p, sizeof(p), NULL, 0);
}


static int cmd_cec_get(int argc, char *argv[])
{
	int rv, cmd;
	struct ec_params_cec_get p;
	struct ec_response_cec_get r;


	if (argc != 3) {
		fprintf(stderr, "Invalid number of params\n");
		cmd_cec_help(argv[0]);
		return -1;
	}

	cmd = cec_cmd_from_str(argv[2]);
	if (cmd < 0) {
		fprintf(stderr, "Invalid command '%s'.\n", argv[2]);
		return -1;
	}
	p.cmd = cmd;


	rv = ec_command(EC_CMD_CEC_GET, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("%d\n", r.val);

	return 0;
}

int cmd_cec(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Invalid number of params\n");
		cmd_cec_help(argv[0]);
		return -1;
	}
	if (!strcmp(argv[1], "write"))
		return cmd_cec_write(argc, argv);
	if (!strcmp(argv[1], "read"))
		return cmd_cec_read(argc, argv);
	if (!strcmp(argv[1], "get"))
		return cmd_cec_get(argc, argv);
	if (!strcmp(argv[1], "set"))
		return cmd_cec_set(argc, argv);

	fprintf(stderr, "Invalid sub command: %s\n", argv[1]);
	cmd_cec_help(argv[0]);

	return -1;
}

/* NULL-terminated list of commands */
const struct command commands[] = {
	{"adcread", cmd_adc_read},
	{"addentropy", cmd_add_entropy},
	{"apreset", cmd_apreset},
	{"autofanctrl", cmd_thermal_auto_fan_ctrl},
	{"backlight", cmd_lcd_backlight},
	{"battery", cmd_battery},
	{"batterycutoff", cmd_battery_cut_off},
	{"batteryparam", cmd_battery_vendor_param},
	{"boardversion", cmd_board_version},
	{"button", cmd_button},
	{"cbi", cmd_cbi},
	{"chargecurrentlimit", cmd_charge_current_limit},
	{"chargecontrol", cmd_charge_control},
	{"chargeoverride", cmd_charge_port_override},
	{"chargestate", cmd_charge_state},
	{"chipinfo", cmd_chipinfo},
	{"cmdversions", cmd_cmdversions},
	{"console", cmd_console},
	{"cec", cmd_cec},
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
	{"extpwrlimit", cmd_ext_power_limit},
	{"fanduty", cmd_fanduty},
	{"flasherase", cmd_flash_erase},
	{"flasheraseasync", cmd_flash_erase},
	{"flashprotect", cmd_flash_protect},
	{"flashread", cmd_flash_read},
	{"flashwrite", cmd_flash_write},
	{"flashinfo", cmd_flash_info},
	{"flashspiinfo", cmd_flash_spi_info},
	{"flashpd", cmd_flash_pd},
	{"forcelidopen", cmd_force_lid_open},
	{"fpcontext", cmd_fp_context},
	{"fpencstatus", cmd_fp_enc_status},
	{"fpframe", cmd_fp_frame},
	{"fpinfo", cmd_fp_info},
	{"fpmode", cmd_fp_mode},
	{"fpseed", cmd_fp_seed},
	{"fpstats", cmd_fp_stats},
	{"fptemplate", cmd_fp_template},
	{"gpioget", cmd_gpio_get},
	{"gpioset", cmd_gpio_set},
	{"hangdetect", cmd_hang_detect},
	{"hello", cmd_hello},
	{"hibdelay", cmd_hibdelay},
	{"hostevent", cmd_hostevent},
	{"hostsleepstate", cmd_hostsleepstate},
	{"locatechip", cmd_locate_chip},
	{"i2cprotect", cmd_i2c_protect},
	{"i2cread", cmd_i2c_read},
	{"i2cwrite", cmd_i2c_write},
	{"i2cxfer", cmd_i2c_xfer},
	{"infopddev", cmd_pd_device_info},
	{"inventory", cmd_inventory},
	{"led", cmd_led},
	{"lightbar", cmd_lightbar},
	{"kbfactorytest", cmd_keyboard_factory_test},
	{"kbid", cmd_kbid},
	{"kbinfo", cmd_kbinfo},
	{"kbpress", cmd_kbpress},
	{"keyconfig", cmd_keyconfig},
	{"keyscan", cmd_keyscan},
	{"mkbpget", cmd_mkbp_get},
	{"mkbpwakemask", cmd_mkbp_wake_mask},
	{"motionsense", cmd_motionsense},
	{"nextevent", cmd_next_event},
	{"panicinfo", cmd_panic_info},
	{"pause_in_s5", cmd_s5},
	{"pdgetmode", cmd_pd_get_amode},
	{"pdsetmode", cmd_pd_set_amode},
	{"port80read", cmd_port80_read},
	{"pdlog", cmd_pd_log},
	{"pdcontrol", cmd_pd_control},
	{"pdchipinfo", cmd_pd_chip_info},
	{"pdwritelog", cmd_pd_write_log},
	{"powerinfo", cmd_power_info},
	{"protoinfo", cmd_proto_info},
	{"pse", cmd_pse},
	{"pstoreinfo", cmd_pstore_info},
	{"pstoreread", cmd_pstore_read},
	{"pstorewrite", cmd_pstore_write},
	{"pwmgetfanrpm", cmd_pwm_get_fan_rpm},
	{"pwmgetkblight", cmd_pwm_get_keyboard_backlight},
	{"pwmgetnumfans", cmd_pwm_get_num_fans},
	{"pwmgetduty", cmd_pwm_get_duty},
	{"pwmsetfanrpm", cmd_pwm_set_fan_rpm},
	{"pwmsetkblight", cmd_pwm_set_keyboard_backlight},
	{"pwmsetduty", cmd_pwm_set_duty},
	{"rand", cmd_rand},
	{"readtest", cmd_read_test},
	{"reboot_ec", cmd_reboot_ec},
	{"rollbackinfo", cmd_rollback_info},
	{"rtcget", cmd_rtc_get},
	{"rtcgetalarm", cmd_rtc_get_alarm},
	{"rtcset", cmd_rtc_set},
	{"rtcsetalarm", cmd_rtc_set_alarm},
	{"rwhashpd", cmd_rw_hash_pd},
	{"rwsig", cmd_rwsig},
	{"rwsigaction", cmd_rwsig_action_legacy},
	{"rwsigstatus", cmd_rwsig_status},
	{"sertest", cmd_serial_test},
	{"smartdischarge", cmd_smart_discharge},
	{"stress", cmd_stress_test},
	{"sysinfo", cmd_sysinfo},
	{"port80flood", cmd_port_80_flood},
	{"switches", cmd_switches},
	{"temps", cmd_temperature},
	{"tempsinfo", cmd_temp_sensor_info},
	{"test", cmd_test},
	{"thermalget", cmd_thermal_get_threshold},
	{"thermalset", cmd_thermal_set_threshold},
	{"tpselftest", cmd_tp_self_test},
	{"tpframeget", cmd_tp_frame_get},
	{"tmp006cal", cmd_tmp006cal},
	{"tmp006raw", cmd_tmp006raw},
	{"typeccontrol", cmd_typec_control},
	{"typecdiscovery", cmd_typec_discovery},
	{"typecstatus", cmd_typec_status},
	{"uptimeinfo", cmd_uptimeinfo},
	{"usbchargemode", cmd_usb_charge_set_mode},
	{"usbmux", cmd_usb_mux},
	{"usbpd", cmd_usb_pd},
	{"usbpdmuxinfo", cmd_usb_pd_mux_info},
	{"usbpdpower", cmd_usb_pd_power},
	{"version", cmd_version},
	{"waitevent", cmd_wait_event},
	{"wireless", cmd_wireless},
	{"reboot_ap_on_g3", cmd_reboot_ap_on_g3},
	{NULL, NULL}
};

int main(int argc, char *argv[])
{
	const struct command *cmd;
	int dev = 0;
	int interfaces = COMM_ALL;
	int i2c_bus = -1;
	char device_name[41] = CROS_EC_DEV_NAME;
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
			} else if (!strcasecmp(optarg, "servo")) {
				interfaces = COMM_SERVO;
			} else {
				fprintf(stderr, "Invalid --interface\n");
				parse_error = 1;
			}
			break;
		case OPT_NAME:
			strncpy(device_name, optarg, 40);
			device_name[40] = '\0';
			break;
		case OPT_I2C_BUS:
			i2c_bus = strtoul(optarg, &e, 0);
			if (*optarg == '\0' || (e && *e != '\0')
			    || i2c_bus < 0) {
				fprintf(stderr, "Invalid --i2c_bus\n");
				parse_error = 1;
			}
			break;
		case OPT_ASCII:
			ascii_mode = 1;
			break;
		}
	}

	if (i2c_bus != -1)  {
		if (!(interfaces & COMM_I2C)) {
			fprintf(stderr, "--i2c_bus is specified, but --interface is set to something other than I2C\n");
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
		if (acquire_gec_lock(GEC_LOCK_TIMEOUT_SECS) < 0) {
			fprintf(stderr, "Could not acquire GEC lock.\n");
			exit(1);
		}
		if (comm_init_alt(interfaces, device_name, i2c_bus)) {
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
	return !!rv;
}
