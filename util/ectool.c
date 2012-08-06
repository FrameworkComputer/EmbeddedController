/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <unistd.h>

#include "battery.h"
#include "comm-host.h"
#include "lightbar.h"

/* Handy tricks */
#define BUILD_ASSERT(cond) ((void)sizeof(char[1 - 2*!(cond)]))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
/* Don't use a macro where an inline will do... */
static inline int MIN(int a, int b) { return a < b ? a : b; }


const char help_str[] =
	"Commands:\n"
	"  autofanctrl <on>\n"
	"      Turn on automatic fan speed control.\n"
	"  backlight <enabled>\n"
	"      Enable/disable LCD backlight\n"
	"  battery\n"
	"      Prints battery info\n"
	"  chargeforceidle\n"
	"      Force charge state machine to stop in idle mode\n"
	"  chipinfo\n"
	"      Prints chip info\n"
	"  cmdversions <cmd>\n"
	"      Prints supported version mask for a command number\n"
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
	"  flashprotect [now] [enable | disable]\n"
	"      Prints or sets EC flash protection state\n"
	"  flashread <offset> <size> <outfile>\n"
	"      Reads from EC flash to a file\n"
	"  flashwrite <offset> <infile>\n"
	"      Writes to EC flash from a file\n"
	"  gpioget <GPIO name>\n"
	"      Get the value of GPIO signal\n"
	"  gpioset <GPIO name>\n"
	"      Set the value of GPIO signal\n"
	"  hello\n"
	"      Checks for basic communication with EC\n"
	"  kbpress\n"
	"      Simulate key press\n"
	"  i2cread\n"
	"      Read I2C bus\n"
	"  i2cwrite\n"
	"      Write I2C bus\n"
	"  lightbar [CMDS]\n"
	"      Various lightbar control commands\n"
	"  pstoreinfo\n"
	"      Prints information on the EC host persistent storage\n"
	"  pstoreread <offset> <size> <outfile>\n"
	"      Reads from EC host persistent storage to a file\n"
	"  pstorewrite <offset> <infile>\n"
	"      Writes to EC host persistent storage from a file\n"
	"  pwmgetfanrpm\n"
	"      Prints current fan RPM\n"
	"  pwmgetkblight\n"
	"      Prints current keyboard backlight percent\n"
	"  pwmsetfanrpm <targetrpm>\n"
	"      Set target fan RPM\n"
	"  pwmsetkblight <percent>\n"
	"      Set keyboard backlight in percent\n"
	"  readtest <patternoffset> <size>\n"
	"      Reads a pattern from the EC via LPC\n"
	"  reboot_ec <RO|A|disable-jump> [at-shutdown]\n"
	"      Reboot EC to RO or RW\n"
	"  rtcget\n"
	"      Print real-time clock\n"
	"  rtcset <time>\n"
	"      Set real-time clock\n"
	"  sertest\n"
	"      Serial output test for COM2\n"
	"  switches\n"
	"      Prints current EC switch positions\n"
	"  temps <sensorid>\n"
	"      Print temperature.\n"
	"  tempsinfo <sensorid>\n"
	"      Print temperature sensor info.\n"
	"  thermalget <sensor_id> <threshold_id>\n"
	"      Get the threshold temperature value from thermal engine.\n"
	"  thermalset <sensor_id> <threshold_id> <value>\n"
	"      Set the threshold temperature value for thermal engine.\n"
	"  usbchargemode <port> <mode>\n"
	"      Set USB charging mode\n"
	"  version\n"
	"      Prints EC version\n"
	"  wireless <mask>\n"
	"      Enable/disable WLAN/Bluetooth radio\n"
	"\n"
	"Not working for you?  Make sure LPC I/O is configured:\n"
	"  pci_write32 0 0x1f 0 0x88 0x00fc0801\n"
	"  pci_write32 0 0x1f 0 0x8c 0x00fc0901\n"
	"  pci_write16 0 0x1f 0 0x80 0x0010\n"
	"  pci_write16 0 0x1f 0 0x82 0x3d01\n"
	"";

/* Note: depends on enum system_image_copy_t */
static const char * const image_names[] = {"unknown", "RO", "RW"};

/* Write a buffer to the file.  Return non-zero if error. */
static int write_file(const char *filename, const char *buf, int size)
{
	FILE *f;
	int i;

	/* Write to file */
	f = fopen(filename, "wb");
	if (!f) {
		perror("Error opening output file");
		return -1;
	}
	i = fwrite(buf, 1, size, f);
	fclose(f);
	if (i != size) {
		perror("Error writing to file");
		return -1;
	}

	return 0;
}


/* Read a file into a buffer.  Sets *size to the size of the buffer.  Returns
 * the buffer, which must be freed with free() by the caller.  Returns NULL if
 * error. */
static char *read_file(const char *filename, int *size)
{
	FILE *f = fopen(filename, "rb");
	char *buf;
	int i;

	if (!f) {
		perror("Error opening input file");
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	rewind(f);
	if (*size > 0x100000) {
		fprintf(stderr, "File seems unreasonably large\n");
		fclose(f);
		return NULL;
	}

	buf = (char *)malloc(*size);
	if (!buf) {
		fprintf(stderr, "Unable to allocate buffer.\n");
		fclose(f);
		return NULL;
	}

	printf("Reading %d bytes from %s...\n", *size, filename);
	i = fread(buf, 1, *size, f);
	fclose(f);
	if (i != *size) {
		perror("Error reading file");
		free(buf);
		return NULL;
	}

	return buf;
}


int is_string_printable(const char *buf)
{
	while (*buf) {
		if (!isprint(*buf))
			return 0;
		buf++;
	}

	return 1;
}


/* Check SBS numerical value range */
int is_battery_range(int val)
{
	return (val >= 0 && val <= 65535) ? 1 : 0;
}


void print_help(const char *prog)
{
	printf("Usage: %s <command> [params]\n\n", prog);
	puts(help_str);
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
	char build_string[EC_HOST_PARAM_SIZE];
	int rv;

	rv = ec_command(EC_CMD_GET_VERSION, 0,
			NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;
	rv = ec_command(EC_CMD_GET_BUILD_INFO, 0,
			NULL, 0, build_string, sizeof(build_string));
	if (rv < 0)
		return rv;

	/* Ensure versions are null-terminated before we print them */
	r.version_string_ro[sizeof(r.version_string_ro) - 1] = '\0';
	r.version_string_rw[sizeof(r.version_string_rw) - 1] = '\0';
	build_string[sizeof(build_string) - 1] = '\0';

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
		/* TODO: remove "A" once all scripts are updated to use "RW" */
		p.cmd = EC_REBOOT_JUMP_RW;
	} else if (!strcmp(argv[1], "cold"))
		p.cmd = EC_REBOOT_COLD;
	else if (!strcmp(argv[1], "disable-jump"))
		p.cmd = EC_REBOOT_DISABLE_JUMP;
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
	struct ec_response_flash_info r;
	int rv;

	rv = ec_command(EC_CMD_FLASH_INFO, 0, NULL, 0, &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("FlashSize %d\nWriteSize %d\nEraseSize %d\nProtectSize %d\n",
	       r.flash_size, r.write_block_size, r.erase_block_size,
	       r.protect_block_size);

	return 0;
}


int cmd_flash_read(int argc, char *argv[])
{
	struct ec_params_flash_read p;
	uint8_t rdata[EC_OLD_PARAM_SIZE];
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
	for (i = 0; i < size; i += sizeof(rdata)) {
		p.offset = offset + i;
		p.size = MIN(size - i, sizeof(rdata));
		rv = ec_command(EC_CMD_FLASH_READ, 0,
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


int cmd_flash_write(int argc, char *argv[])
{
	struct ec_params_flash_write p;
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
	for (i = 0; i < size; i += sizeof(p.data)) {
		p.offset = offset + i;
		p.size = MIN(size - i, sizeof(p.data));
		memcpy(p.data, buf + i, p.size);
		rv = ec_command(EC_CMD_FLASH_WRITE, 0, &p, sizeof(p), NULL, 0);
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


int cmd_flash_erase(int argc, char *argv[])
{
	struct ec_params_flash_erase p;
	char *e;
	int rv;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <offset> <size>\n", argv[0]);
		return -1;
	}
	p.offset = strtol(argv[1], &e, 0);
	if ((e && *e) || p.offset < 0 || p.offset > 0x100000) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}
	p.size = strtol(argv[2], &e, 0);
	if ((e && *e) || p.size <= 0 || p.size > 0x100000) {
		fprintf(stderr, "Bad size.\n");
		return -1;
	}

	printf("Erasing %d bytes at offset %d...\n", p.size, p.offset);
	rv = ec_command(EC_CMD_FLASH_ERASE, 0, &p, sizeof(p), NULL, 0);
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
	if (flags & EC_FLASH_PROTECT_RO_NOW)
		printf(" ro_now");
	if (flags & EC_FLASH_PROTECT_RW_NOW)
		printf(" rw_now");
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
			p.mask |= EC_FLASH_PROTECT_RW_NOW;
			p.flags |= EC_FLASH_PROTECT_RW_NOW;
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


int cmd_serial_test(int argc, char *argv[])
{
	const char *c = "COM2 sample serial output from host!\r\n";

	printf("Writing sample serial output to COM2\n");

	while (*c) {
		/* Wait for space in transmit FIFO */
		while (!(inb(0x2fd) & 0x20)) {}

		/* Put the next character */
		outb(*c++, 0x2f8);
	}

	printf("done.\n");
	return 0;
}


int read_mapped_temperature(int id)
{
	int rv;

	if (id < EC_TEMP_SENSOR_ENTRIES)
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


int cmd_thermal_get_threshold(int argc, char *argv[])
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


int cmd_thermal_set_threshold(int argc, char *argv[])
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


int cmd_thermal_auto_fan_ctrl(int argc, char *argv[])
{
	int rv;

	rv = ec_command(EC_CMD_THERMAL_AUTO_FAN_CTRL, 0, NULL, 0, NULL, 0);
	if (rv < 0)
		return rv;

	printf("Automatic fan control is now on.\n");
	return 0;
}


int cmd_pwm_get_fan_rpm(int argc, char *argv[])
{
	int rv;

	rv = read_mapped_mem16(EC_MEMMAP_FAN);
	switch (rv) {
	case EC_FAN_SPEED_NOT_PRESENT:
		return -1;
	case EC_FAN_SPEED_STALLED:
		printf("Fan stalled!\n");
		break;
	default:
		printf("Current fan RPM: %d\n", rv);
		break;
	}

	return 0;
}


int cmd_pwm_set_fan_rpm(int argc, char *argv[])
{
	struct ec_params_pwm_set_fan_target_rpm p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr,
			"Usage: %s <targetrpm>\n", argv[0]);
		return -1;
	}
	p.rpm = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad RPM.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_PWM_SET_FAN_TARGET_RPM, 0,
			&p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("Fan target RPM set.\n");
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
	struct ec_params_pwm_set_fan_duty p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr,
			"Usage: %s <targetrpm>\n", argv[0]);
		return -1;
	}
	p.percent = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad percent arg.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_PWM_SET_FAN_DUTY, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("Fan duty cycle set.\n");
	return 0;
}

#define LBMSG(state) #state
#include "lightbar_msg_list.h"
static const char const *lightbar_cmds[] = {
	LIGHTBAR_MSG_LIST
};
#undef LBMSG

/* This needs to match the values defined in lightbar.h. I'd like to
 * define this in one and only one place, but I can't think of a good way to do
 * that without adding bunch of complexity. This will do for now.
 */
static const struct {
	uint8_t insize;
	uint8_t outsize;
} lb_command_paramcount[] = {
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.dump),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.dump) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.off),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.off) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.on),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.on) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.init),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.init) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.brightness),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.brightness) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.seq),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.seq) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.reg),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.reg) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.rgb),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.rgb) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.get_seq),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.get_seq) },
};

static int lb_help(const char *cmd)
{
	printf("Usage:\n");
	printf("  %s                       - dump all regs\n", cmd);
	printf("  %s off                   - enter standby\n", cmd);
	printf("  %s on                    - leave standby\n", cmd);
	printf("  %s init                  - load default vals\n", cmd);
	printf("  %s brightness NUM        - set intensity (0-ff)\n", cmd);
	printf("  %s seq [NUM|SEQUENCE]    - run given pattern"
		 " (no arg for list)\n", cmd);
	printf("  %s CTRL REG VAL          - set LED controller regs\n", cmd);
	printf("  %s LED RED GREEN BLUE    - set color manually"
		 " (LED=4 for all)\n", cmd);
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
		     struct ec_params_lightbar_cmd *ptr)
{
	int rv;
	ptr->in.cmd = cmd;
	rv = ec_command(EC_CMD_LIGHTBAR_CMD, 0,
			ptr, lb_command_paramcount[cmd].insize,
			ptr, lb_command_paramcount[cmd].outsize);
	return (rv < 0 ? rv : 0);
}

static void lb_show_msg_names(void)
{
	int i, current_state;
	struct ec_params_lightbar_cmd param;

	(void)lb_do_cmd(LIGHTBAR_CMD_GET_SEQ, &param);
	current_state = param.out.get_seq.num;

	printf("sequence names:");
	for (i = 0; i < LIGHTBAR_NUM_SEQUENCES; i++)
		printf(" %s", lightbar_cmds[i]);
	printf("\nCurrent = 0x%x %s\n", current_state,
	       lightbar_cmds[current_state]);
}

static int cmd_lightbar(int argc, char **argv)
{
	int i, r;
	struct ec_params_lightbar_cmd param;

	if (1 == argc) {		/* no args = dump 'em all */
		r = lb_do_cmd(LIGHTBAR_CMD_DUMP, &param);
		if (r)
			return r;
		for (i = 0; i < ARRAY_SIZE(param.out.dump.vals); i++) {
			printf(" %02x     %02x     %02x\n",
			       param.out.dump.vals[i].reg,
			       param.out.dump.vals[i].ic0,
			       param.out.dump.vals[i].ic1);
		}
		return 0;
	}

	if (argc == 2 && !strcasecmp(argv[1], "init"))
		return lb_do_cmd(LIGHTBAR_CMD_INIT, &param);

	if (argc == 2 && !strcasecmp(argv[1], "off"))
		return lb_do_cmd(LIGHTBAR_CMD_OFF, &param);

	if (argc == 2 && !strcasecmp(argv[1], "on"))
		return lb_do_cmd(LIGHTBAR_CMD_ON, &param);

	if (argc == 3 && !strcasecmp(argv[1], "brightness")) {
		char *e;
		param.in.brightness.num = 0xff & strtoul(argv[2], &e, 16);
		return lb_do_cmd(LIGHTBAR_CMD_BRIGHTNESS, &param);
	}

	if (argc >= 2 && !strcasecmp(argv[1], "seq")) {
		char *e;
		uint8_t num;
		if (argc == 2) {
			lb_show_msg_names();
			return 0;
		}
		num = 0xff & strtoul(argv[2], &e, 16);
		if (e && *e)
			num = lb_find_msg_by_name(argv[2]);
		if (num >= LIGHTBAR_NUM_SEQUENCES) {
			fprintf(stderr, "Invalid arg\n");
			return -1;
		}
		param.in.seq.num = num;
		return lb_do_cmd(LIGHTBAR_CMD_SEQ, &param);
	}

	if (argc == 4) {
		char *e;
		param.in.reg.ctrl = 0xff & strtoul(argv[1], &e, 16);
		param.in.reg.reg = 0xff & strtoul(argv[2], &e, 16);
		param.in.reg.value = 0xff & strtoul(argv[3], &e, 16);
		return lb_do_cmd(LIGHTBAR_CMD_REG, &param);
	}

	if (argc == 5) {
		char *e;
		param.in.rgb.led = strtoul(argv[1], &e, 16);
		param.in.rgb.red = strtoul(argv[2], &e, 16);
		param.in.rgb.green = strtoul(argv[3], &e, 16);
		param.in.rgb.blue = strtoul(argv[4], &e, 16);
		return lb_do_cmd(LIGHTBAR_CMD_RGB, &param);
	}

	return lb_help(argv[0]);
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
	printf("Keyboard recovery:  %sABLED\n",
	       (s & EC_SWITCH_KEYBOARD_RECOVERY ? "EN" : "DIS"));
	printf("Dedicated recovery: %sABLED\n",
	       (s & EC_SWITCH_DEDICATED_RECOVERY ? "EN" : "DIS"));

	return 0;
}


int cmd_wireless(int argc, char *argv[])
{
	struct ec_params_switch_enable_wireless p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <mask>\n", argv[0]);
		fprintf(stderr, "  0x1 = WLAN\n"
				"  0x2 = Bluetooth\n");
		return -1;
	}
	p.enabled = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad value.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_SWITCH_ENABLE_WIRELESS, 0,
			&p, sizeof(p), NULL, 0);
	if (rv < 0)
		return rv;

	printf("Success.\n");
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

	rv = ec_command(EC_CMD_I2C_WRITE, 0, &p, sizeof(p), NULL, 0);

	if (rv < 0)
		return rv;

	printf("Wrote 0x%x to I2C port %d at 0x%x offset 0x%x.\n",
	       p.data, p.port, p.addr, p.offset);
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


int cmd_charge_force_idle(int argc, char *argv[])
{
	struct ec_params_force_idle p;
	int rv;
	char *e;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <0|1>\n", argv[0]);
		return -1;
	}

	p.enabled = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad value.\n");
		return -1;
	}

	rv = ec_command(EC_CMD_CHARGE_FORCE_IDLE, 0, &p, sizeof(p), NULL, 0);
	if (rv < 0) {
		fprintf(stderr, "Is AC connected?\n");
		return rv;
	}

	if (p.enabled)
		printf("Charge state machine force idle.\n");
	else
		printf("Charge state machine normal mode.\n");
	return 0;
}


int cmd_gpio_get(int argc, char *argv[])
{
	struct ec_params_gpio_get p;
	struct ec_response_gpio_get r;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <GPIO name>\n", argv[0]);
		return -1;
	}

	if (strlen(argv[1]) + 1 > sizeof(p.name)) {
		fprintf(stderr, "GPIO name too long.\n");
		return -1;
	}
	strcpy(p.name, argv[1]);

	rv = ec_command(EC_CMD_GPIO_GET, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;

	printf("GPIO %s = %d\n", p.name, r.val);
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

	printf("Battery info:\n");

	rv = read_mapped_string(EC_MEMMAP_BATT_MFGR, batt_text);
	if (rv < 0 || !is_string_printable(batt_text))
		goto cmd_error;
	printf("  OEM name:               %s\n", batt_text);

	rv = read_mapped_string(EC_MEMMAP_BATT_MODEL, batt_text);
	if (rv < 0 || !is_string_printable(batt_text))
		goto cmd_error;
	printf("  Model number:           %s\n", batt_text);

	rv = read_mapped_string(EC_MEMMAP_BATT_TYPE, batt_text);
	if (rv < 0 || !is_string_printable(batt_text))
		goto cmd_error;
	printf("  Chemistry   :           %s\n", batt_text);

	rv = read_mapped_string(EC_MEMMAP_BATT_SERIAL, batt_text);
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

	val = read_mapped_mem32(EC_MEMMAP_BATT_DCAP);
	if (!is_battery_range(val))
		goto cmd_error;
	printf("  Design capacity warning %u mAh\n",
		val * BATTERY_LEVEL_WARNING / 100);
	printf("  Design capacity low     %u mAh\n",
		val * BATTERY_LEVEL_LOW / 100);

	val = read_mapped_mem32(EC_MEMMAP_BATT_CCNT);
	if (!is_battery_range(val))
		goto cmd_error;
	printf("  Cycle count             %u\n", val);

	return 0;
cmd_error:
	fprintf(stderr, "Bad battery info value. Check protocol version.");
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


static int ec_hash_help(const char *cmd)
{
	printf("Usage:\n");
	printf("  %s                        - get last hash\n", cmd);
	printf("  %s abort                  - abort hashing\n", cmd);
	printf("  %s start [<offset> <size> [<nonce>]] - start hashing\n", cmd);
	printf("  %s recalc [<offset> <size> [<nonce>]] - sync rehash\n", cmd);
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

	if (argc < 4) {
		fprintf(stderr, "Must specify offset and size\n");
		return -1;
	}

	p.hash_type = EC_VBOOT_HASH_TYPE_SHA256;
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

	printf("Hashing %d bytes at offset %d...\n", p.size, p.offset);
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


struct command {
	const char *name;
	int (*handler)(int argc, char *argv[]);
};

/* NULL-terminated list of commands */
const struct command commands[] = {
	{"autofanctrl", cmd_thermal_auto_fan_ctrl},
	{"backlight", cmd_lcd_backlight},
	{"battery", cmd_battery},
	{"chargeforceidle", cmd_charge_force_idle},
	{"chipinfo", cmd_chipinfo},
	{"cmdversions", cmd_cmdversions},
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
	{"gpioget", cmd_gpio_get},
	{"gpioset", cmd_gpio_set},
	{"hello", cmd_hello},
	{"kbpress", cmd_kbpress},
	{"i2cread", cmd_i2c_read},
	{"i2cwrite", cmd_i2c_write},
	{"lightbar", cmd_lightbar},
	{"pstoreinfo", cmd_pstore_info},
	{"pstoreread", cmd_pstore_read},
	{"pstorewrite", cmd_pstore_write},
	{"pwmgetfanrpm", cmd_pwm_get_fan_rpm},
	{"pwmgetkblight", cmd_pwm_get_keyboard_backlight},
	{"pwmsetfanrpm", cmd_pwm_set_fan_rpm},
	{"pwmsetkblight", cmd_pwm_set_keyboard_backlight},
	{"readtest", cmd_read_test},
	{"reboot_ec", cmd_reboot_ec},
	{"rtcget", cmd_rtc_get},
	{"rtcset", cmd_rtc_set},
	{"sertest", cmd_serial_test},
	{"switches", cmd_switches},
	{"temps", cmd_temperature},
	{"tempsinfo", cmd_temp_sensor_info},
	{"thermalget", cmd_thermal_get_threshold},
	{"thermalset", cmd_thermal_set_threshold},
	{"usbchargemode", cmd_usb_charge_set_mode},
	{"version", cmd_version},
	{"wireless", cmd_wireless},
	{NULL, NULL}
};


int main(int argc, char *argv[])
{
	const struct command *cmd;

	BUILD_ASSERT(ARRAY_SIZE(lb_command_paramcount) == LIGHTBAR_NUM_CMDS);

	if (argc < 2 || !strcasecmp(argv[1], "-?") ||
	    !strcasecmp(argv[1], "help")) {
		print_help(argv[0]);
		return -2;
	}

	if (comm_init() < 0)
		return -3;

	/* Handle commands */
	for (cmd = commands; cmd->name; cmd++) {
		if (!strcasecmp(argv[1], cmd->name))
			return cmd->handler(argc - 1, argv + 1);
	}

	/* If we're still here, command was unknown */
	fprintf(stderr, "Unknown command '%s'\n\n", argv[1]);
	print_help(argv[0]);
	return -2;
}
