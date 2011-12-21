/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <unistd.h>

#include "lpc_commands.h"
#include "temp_sensor.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
/* Don't use a macro where an inline will do... */
static inline int MIN(int a, int b) { return a < b ? a : b; }


const char help_str[] =
	"Commands:\n"
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
	"  readtest <patternoffset> <size>\n"
	"      Reads a pattern from the EC via LPC\n"
	"  sertest\n"
	"      Serial output test for COM2\n"
	"  version\n"
	"      Prints EC version\n"
	"  temps\n"
	"      Print temperature\n"
	"\n"
	"Not working for you?  Make sure LPC I/O is configured:\n"
	"  pci_write32 0 0x1f 0 0x88 0x007c0801\n"
	"  pci_write32 0 0x1f 0 0x8c 0x007c0901\n"
	"  pci_write16 0 0x1f 0 0x80 0x0010\n"
	"  pci_write16 0 0x1f 0 0x82 0x3f02\n"
	"";


/* Waits for the EC to be unbusy.  Returns 0 if unbusy, non-zero if
 * timeout. */
int wait_for_ec(int status_addr, int timeout_usec)
{
	int i;
	for (i = 0; i < timeout_usec; i += 10) {
		usleep(10);  /* Delay first, in case we just sent a command */
		if (!(inb(status_addr) & EC_LPC_BUSY_MASK))
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

	/* Check status */
	i = inb(cmd_addr);
	i = EC_LPC_GET_STATUS(i);
	if (i) {
		fprintf(stderr, "EC returned error status %d\n", i);
		return i;
	}

	/* Read data, if any */
	for (i = 0, d = (uint8_t *)outdata; i < outsize; i++, d++)
		*d = inb(param_addr + i);

	return 0;
}


void print_help(const char *prog)
{
	printf("Usage: %s <command> [params]\n\n", prog);
	puts(help_str);
}


int cmd_hello(void)
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


int cmd_version(void)
{
  static const char * const fw_copies[] = {"unknown", "RO", "A", "B"};
	struct lpc_response_get_version r;
	int rv;

	rv = ec_command(EC_LPC_COMMAND_GET_VERSION, NULL, 0, &r, sizeof(r));
	if (rv)
		return rv;

	/* Ensure versions are null-terminated before we print them */
	r.version_string_ro[sizeof(r.version_string_ro) - 1] = '\0';
	r.version_string_rw_a[sizeof(r.version_string_rw_a) - 1] = '\0';
	r.version_string_rw_b[sizeof(r.version_string_rw_b) - 1] = '\0';

        /* Print versions */
	printf("RO version:    %s\n", r.version_string_ro);
	printf("RW-A version:  %s\n", r.version_string_rw_a);
	printf("RW-B version:  %s\n", r.version_string_rw_b);
	printf("Firmware copy: %s\n",
	       (r.current_image < ARRAY_SIZE(fw_copies) ?
		fw_copies[r.current_image] : "?"));
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


int cmd_flash_info(void)
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
	FILE *f;

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

	/* Write to file */
	f = fopen(argv[2], "wb");
	if (!f) {
		perror("Error opening output file");
		free(buf);
		return -1;
	}
	i = fwrite(buf, 1, size, f);
	fclose(f);
	free(buf);
	if (i != size) {
		perror("Error writing to file");
		return -1;
	}
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
	FILE *f;

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
	f = fopen(argv[1], "rb");
	if (!f) {
		perror("Error opening input file");
		return -1;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	rewind(f);
	if (size > 0x100000) {
		fprintf(stderr, "File seems unreasonably large\n");
		fclose(f);
		return -1;
	}

	buf = (char *)malloc(size);
	if (!buf) {
		fprintf(stderr, "Unable to allocate buffer.\n");
		fclose(f);
		return -1;
	}

	printf("Reading %d bytes from %s...\n", size, argv[1]);
	i = fread(buf, 1, size, f);
	if (i != size) {
		perror("Error reading file");
		free(buf);
		return -1;
	}

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

int get_temperature(int sensor_id, const char* name)
{
	struct lpc_params_temp_sensor_get_readings p;
	struct lpc_response_temp_sensor_get_readings r;
	int rv;

	p.temp_sensor_id = sensor_id;
	printf("Reading %s...", name);
	rv = ec_command(EC_LPC_COMMAND_TEMP_SENSOR_GET_READINGS, &p, sizeof(p), &r, sizeof(r));
	if (rv)
		printf("Error\n");
	else
		printf("%d\n", r.value);
	return rv;
}

int cmd_temperature(void)
{
	int rv1, rv2, rv3;
	rv1 = get_temperature(TEMP_SENSOR_CASE, "TEMP_SENSOR_CASE");
	rv2 = get_temperature(TEMP_SENSOR_CASE_DIE, "TEMP_SENSOR_CASE_DIE");
	rv3 = get_temperature(TEMP_SENSOR_EC_INTERNAL, "TEMP_SENSOR_EC_INTERNAL");
	if (rv1 || rv2 || rv3)
		return -1;
	return 0;
}

int main(int argc, char *argv[])
{
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
	if (!strcasecmp(argv[1], "flashinfo"))
		return cmd_flash_info();
	if (!strcasecmp(argv[1], "flashread"))
		return cmd_flash_read(argc - 2, argv + 2);
	if (!strcasecmp(argv[1], "flashwrite"))
		return cmd_flash_write(argc - 2, argv + 2);
	if (!strcasecmp(argv[1], "flasherase"))
		return cmd_flash_erase(argc - 2, argv + 2);
	if (!strcasecmp(argv[1], "hello"))
		return cmd_hello();
	if (!strcasecmp(argv[1], "readtest"))
		return cmd_read_test(argc - 2, argv + 2);
	if (!strcasecmp(argv[1], "sertest"))
		return cmd_serial_test(argc - 2, argv + 2);
	if (!strcasecmp(argv[1], "version"))
		return cmd_version();
	if (!strcasecmp(argv[1], "temps"))
		return cmd_temperature();

	/* If we're still here, command was unknown */
	fprintf(stderr, "Unknown command '%s'\n\n", argv[1]);
	print_help(argv[0]);
	return -2;
}
