/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <unistd.h>

#include "lpc_commands.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
/* Don't use a macro where an inline will do... */
static inline int MIN(int a, int b) { return a < b ? a : b; }


const char help_str[] =
	"Commands:\n"
	"  battery\n"
	"      Prints battery info\n"
	"  eventclear <mask>\n"
	"      Clears EC host events flags where mask has bits set\n"
	"  eventget\n"
	"      Prints raw EC host event flags\n"
	"  eventgetscimask\n"
	"      Prints SCI mask for EC host events\n"
	"  eventgetsmimask\n"
	"      Prints SMI mask for EC host events\n"
	"  eventsetscimask <mask>\n"
	"      Sets the SCI mask for EC host events\n"
	"  eventsetsmimask <mask>\n"
	"      Sets the SMI mask for EC host events\n"
	"  flashinfo\n"
	"      Prints information on the EC flash\n"
	"  flashread <offset> <size> <outfile>\n"
	"      Reads from EC flash to a file\n"
	"  flashwrite <offset> <infile>\n"
	"      Writes to EC flash from a file\n"
	"  flasherase <offset> <size>\n"
	"      Erases EC flash\n"
	"  hello\n"
	"      Checks for basic communication with EC\n"
	"  pstoreinfo\n"
	"      Prints information on the EC host persistent storage\n"
	"  pstoreread <offset> <size> <outfile>\n"
	"      Reads from EC host persistent storage to a file\n"
	"  pstorewrite <offset> <infile>\n"
	"      Writes to EC host persistent storage from a file\n"
	"  queryec\n"
	"      Does an ACPI Query Embedded Controller command\n"
	"  readtest <patternoffset> <size>\n"
	"      Reads a pattern from the EC via LPC\n"
	"  sertest\n"
	"      Serial output test for COM2\n"
	"  switches\n"
	"      Prints current EC switch positions\n"
	"  version\n"
	"      Prints EC version\n"
	"  temps <sensorid>\n"
	"      Print temperature.\n"
	"  thermalget <sensor_id> <threshold_id>\n"
	"      Get the threshold temperature value from thermal engine.\n"
	"  thermalset <sensor_id> <threshold_id> <value>\n"
	"      Set the threshold temperature value for thermal engine.\n"
	"  autofanctrl <on>\n"
	"      Turn on automatic fan speed control.\n"
	"  pwmgetfanrpm\n"
	"      Prints current fan RPM\n"
	"  pwmsetfanrpm <targetrpm>\n"
	"      Set target fan RPM\n"
	"  pwmgetkblight\n"
	"      Prints current keyboard backlight percent\n"
	"  pwmsetkblight <percent>\n"
	"      Set keyboard backlight in percent\n"
	"  usbchargemode <port> <mode>\n"
	"      Set USB charging mode\n"
	"\n"
	"Not working for you?  Make sure LPC I/O is configured:\n"
	"  pci_write32 0 0x1f 0 0x88 0x00fc0801\n"
	"  pci_write32 0 0x1f 0 0x8c 0x00fc0901\n"
	"  pci_write16 0 0x1f 0 0x80 0x0010\n"
	"  pci_write16 0 0x1f 0 0x82 0x3d01\n"
	"";


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


/* Waits for the EC to be unbusy.  Returns 0 if unbusy, non-zero if
 * timeout. */
int wait_for_ec(int status_addr, int timeout_usec)
{
	int i;
	for (i = 0; i < timeout_usec; i += 10) {
		usleep(10);  /* Delay first, in case we just sent a command */
		if (!(inb(status_addr) & EC_LPC_STATUS_BUSY_MASK))
			return 0;
	}
	return -1;  /* Timeout */
}


/* Sends a command to the EC.  Returns the command status code, or
 * -1 if other error. */
int ec_command(int command, const void *indata, int insize,
	       void *outdata, int outsize) {
	uint8_t *d;
	int i;

	/* TODO: add command line option to use kernel command/param window */
	int cmd_addr = EC_LPC_ADDR_USER_CMD;
	int data_addr = EC_LPC_ADDR_USER_DATA;
	int param_addr = EC_LPC_ADDR_USER_PARAM;

	if (insize > EC_LPC_PARAM_SIZE || outsize > EC_LPC_PARAM_SIZE) {
		fprintf(stderr, "Data size too big\n");
		return -1;
	}

	if (wait_for_ec(cmd_addr, 1000000)) {
		fprintf(stderr, "Timeout waiting for EC ready\n");
		return -1;
	}

	/* Write data, if any */
	/* TODO: optimized copy using outl() */
	for (i = 0, d = (uint8_t *)indata; i < insize; i++, d++)
		outb(*d, param_addr + i);

	outb(command, cmd_addr);

	if (wait_for_ec(cmd_addr, 1000000)) {
		fprintf(stderr, "Timeout waiting for EC response\n");
		return -1;
	}

	/* Check result */
	i = inb(data_addr);
	if (i) {
		fprintf(stderr, "EC returned error result code %d\n", i);
		return i;
	}

	/* Read data, if any */
	/* TODO: optimized copy using outl() */
	for (i = 0, d = (uint8_t *)outdata; i < outsize; i++, d++)
		*d = inb(param_addr + i);

	return 0;
}


uint8_t read_mapped_mem8(uint8_t offset)
{
	return inb(EC_LPC_ADDR_MEMMAP + offset);
}


uint16_t read_mapped_mem16(uint8_t offset)
{
	return inw(EC_LPC_ADDR_MEMMAP + offset);
}


uint32_t read_mapped_mem32(uint8_t offset)
{
	return inl(EC_LPC_ADDR_MEMMAP + offset);
}


void print_help(const char *prog)
{
	printf("Usage: %s <command> [params]\n\n", prog);
	puts(help_str);
}


int cmd_hello(int argc, char *argv[])
{
	struct lpc_params_hello p;
	struct lpc_response_hello r;
	int rv;

	p.in_data = 0xa0b0c0d0;

	rv = ec_command(EC_LPC_COMMAND_HELLO, &p, sizeof(p), &r, sizeof(r));
	if (rv)
		return rv;

	if (r.out_data != 0xa1b2c3d4) {
		fprintf(stderr, "Expected response 0x%08x, got 0x%08x\n",
			0xa1b2c3d4, r.out_data);
		return -1;
	}

	printf("EC says hello!\n");
	return 0;
}


int cmd_version(int argc, char *argv[])
{
  static const char * const fw_copies[] = {"unknown", "RO", "A", "B"};
	struct lpc_response_get_version r;
	struct lpc_response_get_build_info r2;
	int rv;

	rv = ec_command(EC_LPC_COMMAND_GET_VERSION, NULL, 0, &r, sizeof(r));
	if (rv)
		return rv;
	rv = ec_command(EC_LPC_COMMAND_GET_BUILD_INFO,
			NULL, 0, &r2, sizeof(r2));
	if (rv)
		return rv;

	/* Ensure versions are null-terminated before we print them */
	r.version_string_ro[sizeof(r.version_string_ro) - 1] = '\0';
	r.version_string_rw_a[sizeof(r.version_string_rw_a) - 1] = '\0';
	r.version_string_rw_b[sizeof(r.version_string_rw_b) - 1] = '\0';
	r2.build_string[sizeof(r2.build_string) - 1] = '\0';

	/* Print versions */
	printf("RO version:    %s\n", r.version_string_ro);
	printf("RW-A version:  %s\n", r.version_string_rw_a);
	printf("RW-B version:  %s\n", r.version_string_rw_b);
	printf("Firmware copy: %s\n",
	       (r.current_image < ARRAY_SIZE(fw_copies) ?
		fw_copies[r.current_image] : "?"));
	printf("Build info:    %s\n", r2.build_string);

	return 0;
}


int cmd_read_test(int argc, char *argv[])
{
	struct lpc_params_read_test p;
	struct lpc_response_read_test r;
	int offset, size;
	int errors = 0;
	int rv;
	int i;
	char *e;
	char *buf;
	uint32_t *b;

	if (argc < 2) {
		fprintf(stderr, "Usage: readtest <pattern_offset> <size>\n");
		return -1;
	}
	offset = strtol(argv[0], &e, 0);
	size = strtol(argv[1], &e, 0);
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
		rv = ec_command(EC_LPC_COMMAND_READ_TEST, &p, sizeof(p),
				&r, sizeof(r));
		if (rv) {
			fprintf(stderr, "Read error at offset %d\n", i);
			free(buf);
			return -1;
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


int cmd_flash_info(int argc, char *argv[])
{
	struct lpc_response_flash_info r;
	int rv;

	rv = ec_command(EC_LPC_COMMAND_FLASH_INFO, NULL, 0, &r, sizeof(r));
	if (rv)
		return rv;

	printf("FlashSize %d\nWriteSize %d\nEraseSize %d\nProtectSize %d\n",
	       r.flash_size, r.write_block_size, r.erase_block_size,
	       r.protect_block_size);

	return 0;
}


int cmd_flash_read(int argc, char *argv[])
{
	struct lpc_params_flash_read p;
	struct lpc_response_flash_read r;
	int offset, size;
	int rv;
	int i;
	char *e;
	char *buf;

	if (argc < 3) {
		fprintf(stderr,
			"Usage: flashread <offset> <size> <filename>\n");
		return -1;
	}
	offset = strtol(argv[0], &e, 0);
	if ((e && *e) || offset < 0 || offset > 0x100000) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}
	size = strtol(argv[1], &e, 0);
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
	for (i = 0; i < size; i += EC_LPC_FLASH_SIZE_MAX) {
		p.offset = offset + i;
		p.size = MIN(size - i, EC_LPC_FLASH_SIZE_MAX);
		rv = ec_command(EC_LPC_COMMAND_FLASH_READ,
				&p, sizeof(p), &r, sizeof(r));
		if (rv) {
			fprintf(stderr, "Read error at offset %d\n", i);
			free(buf);
			return -1;
		}
		memcpy(buf + i, r.data, p.size);
	}

	rv = write_file(argv[2], buf, size);
	free(buf);
	if (rv)
		return -1;

	printf("done.\n");
	return 0;
}


int cmd_flash_write(int argc, char *argv[])
{
	struct lpc_params_flash_write p;
	int offset, size;
	int rv;
	int i;
	char *e;
	char *buf;

	if (argc < 2) {
		fprintf(stderr, "Usage: flashwrite <offset> <filename>\n");
		return -1;
	}
	offset = strtol(argv[0], &e, 0);
	if ((e && *e) || offset < 0 || offset > 0x100000) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}

	/* Read the input file */
	buf = read_file(argv[1], &size);
	if (!buf)
		return -1;

	printf("Writing to offset %d...\n", offset);

	/* Write data in chunks */
	for (i = 0; i < size; i += EC_LPC_FLASH_SIZE_MAX) {
		p.offset = offset + i;
		p.size = MIN(size - i, EC_LPC_FLASH_SIZE_MAX);
		memcpy(p.data, buf + i, p.size);
		rv = ec_command(EC_LPC_COMMAND_FLASH_WRITE,
				&p, sizeof(p), NULL, 0);
		if (rv) {
			fprintf(stderr, "Write error at offset %d\n", i);
			free(buf);
			return -1;
		}
	}

	free(buf);
	printf("done.\n");
	return 0;
}


int cmd_flash_erase(int argc, char *argv[])
{
	struct lpc_params_flash_erase p;
	char *e;

	if (argc < 2) {
		fprintf(stderr, "Usage: flasherase <offset> <size>\n");
		return -1;
	}
	p.offset = strtol(argv[0], &e, 0);
	if ((e && *e) || p.offset < 0 || p.offset > 0x100000) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}
	p.size = strtol(argv[1], &e, 0);
	if ((e && *e) || p.size <= 0 || p.size > 0x100000) {
		fprintf(stderr, "Bad size.\n");
		return -1;
	}

	printf("Erasing %d bytes at offset %d...\n", p.size, p.offset);
	if (ec_command(EC_LPC_COMMAND_FLASH_ERASE, &p, sizeof(p), NULL, 0))
		return -1;

	printf("done.\n");
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


int cmd_temperature(int argc, char *argv[])
{
	int rv;
	int id;
	char *e;

	if (argc != 1) {
		fprintf(stderr, "Usage: temps <sensorid>\n");
		return -1;
	}

	id = strtol(argv[0], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad sensor ID.\n");
		return -1;
	}

	/* Currently we only store up to 16 temperature sensor data in
	 * mapped memory. */
	if (id >= 16) {
		printf("Sensor with ID greater than 16 unsupported.\n");
		return -1;
	}

	printf("Reading temperature...");
	rv = read_mapped_mem8(EC_LPC_MEMMAP_TEMP_SENSOR + id);
	if (rv == 0xff) {
		printf("Sensor not present\n");
		return -1;
	} else if (rv == 0xfe) {
		printf("Error\n");
		return -1;
	} else {
		printf("%d\n", rv + EC_LPC_TEMP_SENSOR_OFFSET);
		return 0;
	}
}


int cmd_thermal_get_threshold(int argc, char *argv[])
{
	struct lpc_params_thermal_get_threshold p;
	struct lpc_response_thermal_get_threshold r;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr, "Usage: thermalget <sensorid> <thresholdid>\n");
		return -1;
	}

	p.sensor_id = strtol(argv[0], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad sensor ID.\n");
		return -1;
	}

	p.threshold_id = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad threshold ID.\n");
		return -1;
	}

	rv = ec_command(EC_LPC_COMMAND_THERMAL_GET_THRESHOLD,
			&p, sizeof(p), &r, sizeof(r));
	if (rv)
		return rv;

	if (r.value < 0)
		return -1;

	printf("Threshold %d for sensor %d is %d K.\n",
			p.threshold_id, p.sensor_id, r.value);

	return 0;
}


int cmd_thermal_set_threshold(int argc, char *argv[])
{
	struct lpc_params_thermal_set_threshold p;
	char *e;
	int rv;

	if (argc != 3) {
		fprintf(stderr,
			"Usage: thermalset <sensorid> <thresholdid> <value>\n");
		return -1;
	}

	p.sensor_id = strtol(argv[0], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad sensor ID.\n");
		return -1;
	}

	p.threshold_id = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad threshold ID.\n");
		return -1;
	}

	p.value = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad threshold value.\n");
		return -1;
	}

	rv = ec_command(EC_LPC_COMMAND_THERMAL_SET_THRESHOLD,
			&p, sizeof(p), NULL, 0);
	if (rv)
		return rv;

	printf("Threshold %d for sensor %d set to %d.\n",
			p.threshold_id, p.sensor_id, p.value);

	return 0;
}


int cmd_thermal_auto_fan_ctrl(int argc, char *argv[])
{
	int rv;

	rv = ec_command(EC_LPC_COMMAND_THERMAL_AUTO_FAN_CTRL,
			NULL, 0, NULL, 0);
	if (rv)
		return rv;

	printf("Automatic fan control is now on.\n");
	return 0;
}


int cmd_pwm_get_fan_rpm(int argc, char *argv[])
{
	int rv;

	rv = read_mapped_mem16(EC_LPC_MEMMAP_FAN);
	if (rv == 0xffff)
		return -1;

	if (rv == 0xfffe)
		printf("Fan stalled!\n");
	else
		printf("Current fan RPM: %d\n", rv);

	return 0;
}


int cmd_pwm_set_fan_rpm(int argc, char *argv[])
{
	struct lpc_params_pwm_set_fan_target_rpm p;
	char *e;
	int rv;

	if (argc != 1) {
		fprintf(stderr,
			"Usage: pwmsetfanrpm <targetrpm>\n");
		return -1;
	}
	p.rpm = strtol(argv[0], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad RPM.\n");
		return -1;
	}

	rv = ec_command(EC_LPC_COMMAND_PWM_SET_FAN_TARGET_RPM,
			&p, sizeof(p), NULL, 0);
	if (rv)
		return rv;

	printf("Fan target RPM set.\n");
	return 0;
}


int cmd_pwm_get_keyboard_backlight(int argc, char *argv[])
{
	struct lpc_response_pwm_get_keyboard_backlight r;
	int rv;

	rv = ec_command(EC_LPC_COMMAND_PWM_GET_KEYBOARD_BACKLIGHT,
			NULL, 0, &r, sizeof(r));
	if (rv)
		return rv;

	printf("Current keyboard backlight percent: %d\n", r.percent);

	return 0;
}


int cmd_pwm_set_keyboard_backlight(int argc, char *argv[])
{
	struct lpc_params_pwm_set_keyboard_backlight p;
	char *e;
	int rv;

	if (argc != 1) {
		fprintf(stderr,
			"Usage: pwmsetkblight <percent>\n");
		return -1;
	}
	p.percent = strtol(argv[0], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad percent.\n");
		return -1;
	}

	rv = ec_command(EC_LPC_COMMAND_PWM_SET_KEYBOARD_BACKLIGHT,
			&p, sizeof(p), NULL, 0);
	if (rv)
		return rv;

	printf("Keyboard backlight set.\n");
	return 0;
}


int cmd_usb_charge_set_mode(int argc, char *argv[])
{
	struct lpc_params_usb_charge_set_mode p;
	char *e;
	int rv;

	if (argc != 2) {
		fprintf(stderr,
			"Usage: usbchargemode <port_id> <mode_id>\n");
		return -1;
	}
	p.usb_port_id = strtol(argv[0], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port ID.\n");
		return -1;
	}
	p.mode = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad mode ID.\n");
		return -1;
	}

	printf("Setting port %d to mode %d...\n", p.usb_port_id, p.mode);

	rv = ec_command(EC_LPC_COMMAND_USB_CHARGE_SET_MODE,
			&p, sizeof(p), NULL, 0);
	if (rv)
		return rv;

	printf("USB charging mode set.\n");
	return 0;
}


int cmd_pstore_info(int argc, char *argv[])
{
	struct lpc_response_pstore_info r;
	int rv;

	rv = ec_command(EC_LPC_COMMAND_PSTORE_INFO, NULL, 0, &r, sizeof(r));
	if (rv)
		return rv;

	printf("PstoreSize %d\nAccessSize %d\n", r.pstore_size, r.access_size);
	return 0;
}


int cmd_pstore_read(int argc, char *argv[])
{
	struct lpc_params_pstore_read p;
	struct lpc_response_pstore_read r;
	int offset, size;
	int rv;
	int i;
	char *e;
	char *buf;

	if (argc < 3) {
		fprintf(stderr,
			"Usage: pstoreread <offset> <size> <filename>\n");
		return -1;
	}
	offset = strtol(argv[0], &e, 0);
	if ((e && *e) || offset < 0 || offset > 0x10000) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}
	size = strtol(argv[1], &e, 0);
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
	for (i = 0; i < size; i += EC_LPC_PSTORE_SIZE_MAX) {
		p.offset = offset + i;
		p.size = MIN(size - i, EC_LPC_PSTORE_SIZE_MAX);
		rv = ec_command(EC_LPC_COMMAND_PSTORE_READ,
				&p, sizeof(p), &r, sizeof(r));
		if (rv) {
			fprintf(stderr, "Read error at offset %d\n", i);
			free(buf);
			return -1;
		}
		memcpy(buf + i, r.data, p.size);
	}

	rv = write_file(argv[2], buf, size);
	free(buf);
	if (rv)
		return -1;

	printf("done.\n");
	return 0;
}


int cmd_pstore_write(int argc, char *argv[])
{
	struct lpc_params_pstore_write p;
	int offset, size;
	int rv;
	int i;
	char *e;
	char *buf;

	if (argc < 2) {
		fprintf(stderr, "Usage: pstorewrite <offset> <filename>\n");
		return -1;
	}
	offset = strtol(argv[0], &e, 0);
	if ((e && *e) || offset < 0 || offset > 0x10000) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}

	/* Read the input file */
	buf = read_file(argv[1], &size);
	if (!buf)
		return -1;

	printf("Writing to offset %d...\n", offset);

	/* Write data in chunks */
	for (i = 0; i < size; i += EC_LPC_PSTORE_SIZE_MAX) {
		p.offset = offset + i;
		p.size = MIN(size - i, EC_LPC_PSTORE_SIZE_MAX);
		memcpy(p.data, buf + i, p.size);
		rv = ec_command(EC_LPC_COMMAND_PSTORE_WRITE,
				&p, sizeof(p), NULL, 0);
		if (rv) {
			fprintf(stderr, "Write error at offset %d\n", i);
			free(buf);
			return -1;
		}
	}

	free(buf);
	printf("done.\n");
	return 0;
}


int cmd_acpi_query_ec(int argc, char *argv[])
{
	int rv;

	rv = ec_command(EC_LPC_COMMAND_ACPI_QUERY_EVENT, NULL, 0, NULL, 0);
	if (rv)
		printf("Got host event %d (mask 0x%08x)\n", rv, 1 << (rv - 1));
	else
		printf("No host event pending.\n");
	return 0;
}


int cmd_host_event_get_raw(int argc, char *argv[])
{
	printf("Current host events: 0x%08x\n",
	       read_mapped_mem32(EC_LPC_MEMMAP_HOST_EVENTS));
	return 0;
}


int cmd_host_event_get_smi_mask(int argc, char *argv[])
{
	struct lpc_response_host_event_get_smi_mask r;
	int rv;

	rv = ec_command(EC_LPC_COMMAND_HOST_EVENT_GET_SMI_MASK,
			NULL, 0, &r, sizeof(r));
	if (rv)
		return rv;

	printf("Current host event SMI mask: 0x%08x\n", r.mask);
	return 0;
}


int cmd_host_event_get_sci_mask(int argc, char *argv[])
{
	struct lpc_response_host_event_get_sci_mask r;
	int rv;

	rv = ec_command(EC_LPC_COMMAND_HOST_EVENT_GET_SCI_MASK,
			NULL, 0, &r, sizeof(r));
	if (rv)
		return rv;

	printf("Current host event SCI mask: 0x%08x\n", r.mask);
	return 0;
}


int cmd_host_event_set_smi_mask(int argc, char *argv[])
{
	struct lpc_params_host_event_set_smi_mask p;
	char *e;
	int rv;

	if (argc != 1) {
		fprintf(stderr,
			"Usage: eventsmimask <mask>\n");
		return -1;
	}
	p.mask = strtol(argv[0], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad mask.\n");
		return -1;
	}

	rv = ec_command(EC_LPC_COMMAND_HOST_EVENT_SET_SMI_MASK,
			&p, sizeof(p), NULL, 0);
	if (rv)
		return rv;

	printf("Mask set.\n");
	return 0;
}


int cmd_host_event_set_sci_mask(int argc, char *argv[])
{
	struct lpc_params_host_event_set_sci_mask p;
	char *e;
	int rv;

	if (argc != 1) {
		fprintf(stderr,
			"Usage: eventscimask <mask>\n");
		return -1;
	}
	p.mask = strtol(argv[0], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad mask.\n");
		return -1;
	}

	rv = ec_command(EC_LPC_COMMAND_HOST_EVENT_SET_SCI_MASK,
			&p, sizeof(p), NULL, 0);
	if (rv)
		return rv;

	printf("Mask set.\n");
	return 0;
}


int cmd_host_event_clear(int argc, char *argv[])
{
	struct lpc_params_host_event_clear p;
	char *e;
	int rv;

	if (argc != 1) {
		fprintf(stderr,
			"Usage: eventclear <mask>\n");
		return -1;
	}
	p.mask = strtol(argv[0], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad mask.\n");
		return -1;
	}

	rv = ec_command(EC_LPC_COMMAND_HOST_EVENT_CLEAR,
			&p, sizeof(p), NULL, 0);
	if (rv)
		return rv;

	printf("Host events cleared.\n");
	return 0;
}


int cmd_switches(int argc, char *argv[])
{
	uint8_t s = read_mapped_mem8(EC_LPC_MEMMAP_SWITCHES);
	printf("Current switches:   0x%02x\n", s);
	printf("Lid switch:         %s\n",
	       (s & EC_LPC_SWITCH_LID_OPEN ? "OPEN" : "CLOSED"));
	printf("Power button:       %s\n",
	       (s & EC_LPC_SWITCH_POWER_BUTTON_PRESSED ? "DOWN" : "UP"));
	printf("Write protect:      %sABLED\n",
	       (s & EC_LPC_SWITCH_WRITE_PROTECT_DISABLED ? "DIS" : "EN"));
	printf("Keyboard recovery:  %sABLED\n",
	       (s & EC_LPC_SWITCH_KEYBOARD_RECOVERY ? "EN" : "DIS"));
	printf("Dedicated recovery: %sABLED\n",
	       (s & EC_LPC_SWITCH_DEDICATED_RECOVERY ? "EN" : "DIS"));

	return 0;
}


int cmd_battery(int argc, char *argv[])
{
	struct lpc_response_battery_info batt_info;
	struct lpc_response_battery_text batt_text;
	int rv;

	printf("Battery info:\n");

	rv = ec_command(EC_LPC_COMMAND_BATTERY_OEM,
			NULL, 0, &batt_text, sizeof(batt_text));
	if (rv)
		return rv;
	printf("  OEM name:          %s\n", batt_text.text);

	rv = ec_command(EC_LPC_COMMAND_BATTERY_MODEL_NUMBER,
			NULL, 0, &batt_text, sizeof(batt_text));
	if (rv)
		return rv;
	printf("  Model number:           %s\n", batt_text.text);

	rv = ec_command(EC_LPC_COMMAND_BATTERY_TYPE,
			NULL, 0, &batt_text, sizeof(batt_text));
	if (rv)
		return rv;
	printf("  Chemistry:              %s\n", batt_text.text);

	rv = ec_command(EC_LPC_COMMAND_BATTERY_SERIAL_NUMBER,
			NULL, 0, &batt_text, sizeof(batt_text));
	if (rv)
		return rv;
	printf("  Serial number:          %s\n", batt_text.text);

	rv = ec_command(EC_LPC_COMMAND_BATTERY_INFO,
			NULL, 0, &batt_info, sizeof(batt_info));
	if (rv)
		return rv;
	printf("  Design capacity:        %u mAh\n",
			batt_info.design_capacity);
	printf("  Last full charge:       %u mAh\n",
			batt_info.last_full_charge_capacity);
	printf("  Design output voltage   %u mV\n",
			batt_info.design_output_voltage);
	printf("  Design capacity warning %u mAh\n",
			batt_info.design_capacity_warning);
	printf("  Design capacity low     %u mAh\n",
			batt_info.design_capacity_low);
	printf("  Cycle count             %u\n",
			batt_info.cycle_count);
	return 0;
}

struct command {
	const char *name;
	int (*handler)(int argc, char *argv[]);
};

/* NULL-terminated list of commands */
const struct command commands[] = {
	{"autofanctrl", cmd_thermal_auto_fan_ctrl},
	{"battery", cmd_battery},
	{"eventclear", cmd_host_event_clear},
	{"eventget", cmd_host_event_get_raw},
	{"eventgetscimask", cmd_host_event_get_sci_mask},
	{"eventgetsmimask", cmd_host_event_get_smi_mask},
	{"eventsetscimask", cmd_host_event_set_sci_mask},
	{"eventsetsmimask", cmd_host_event_set_smi_mask},
	{"flasherase", cmd_flash_erase},
	{"flashread", cmd_flash_read},
	{"flashwrite", cmd_flash_write},
	{"flashinfo", cmd_flash_info},
	{"hello", cmd_hello},
	{"pstoreinfo", cmd_pstore_info},
	{"pstoreread", cmd_pstore_read},
	{"pstorewrite", cmd_pstore_write},
	{"pwmgetfanrpm", cmd_pwm_get_fan_rpm},
	{"pwmgetkblight", cmd_pwm_get_keyboard_backlight},
	{"pwmsetfanrpm", cmd_pwm_set_fan_rpm},
	{"pwmsetkblight", cmd_pwm_set_keyboard_backlight},
	{"queryec", cmd_acpi_query_ec},
	{"readtest", cmd_read_test},
	{"sertest", cmd_serial_test},
	{"switches", cmd_switches},
	{"temps", cmd_temperature},
	{"thermalget", cmd_thermal_get_threshold},
	{"thermalset", cmd_thermal_set_threshold},
	{"usbchargemode", cmd_usb_charge_set_mode},
	{"version", cmd_version},
	{NULL, NULL}
};


int main(int argc, char *argv[])
{
	const struct command *cmd;

	if (argc < 2 || !strcasecmp(argv[1], "-?") ||
	    !strcasecmp(argv[1], "help")) {
		print_help(argv[0]);
		return -2;
	}

	/* Request I/O privilege */
	if (iopl(3) < 0) {
		perror("Error getting I/O privilege");
		return -3;
	}

	/* Handle commands */
	for (cmd = commands; cmd; cmd++) {
		if (!strcasecmp(argv[1], cmd->name))
			return cmd->handler(argc - 2, argv + 2);
	}

	/* If we're still here, command was unknown */
	fprintf(stderr, "Unknown command '%s'\n\n", argv[1]);
	print_help(argv[0]);
	return -2;
}
