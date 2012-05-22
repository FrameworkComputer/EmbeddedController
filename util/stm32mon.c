/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * STM32L SoC system monitor interface tool
 */

/* use cfmakeraw() */
#define _BSD_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Monitor command set */
#define CMD_INIT 0x7f    /* Starts the monitor */

#define CMD_GETCMD   0x00 /* Gets the allowed commands */
#define CMD_GETVER   0x01 /* Gets the bootloader version */
#define CMD_GETID    0x02 /* Gets the Chip ID */
#define CMD_READMEM  0x11 /* Reads memory */
#define CMD_GO       0x21 /* Jumps to user code */
#define CMD_WRITEMEM 0x31 /* Writes memory (SRAM or Flash) */
#define CMD_ERASE    0x43 /* Erases n pages of Flash memory */
#define CMD_EXTERASE 0x44 /* Erases n pages of Flash memory */
#define CMD_WP       0x63 /* Enables write protect */
#define CMD_WU       0x73 /* Disables write protect */
#define CMD_RP       0x82 /* Enables the read protection */
#define CMD_RU       0x92 /* Disables the read protection */

#define RESP_NACK    0x1f
#define RESP_ACK     0x79

/* Extended erase special parameters */
#define ERASE_ALL    0xffff
#define ERASE_BANK1  0xfffe
#define ERASE_BANK2  0xfffd

/* known STM32 SoC parameters */
struct stm32_def {
	uint16_t   id;
	const char *name;
	uint32_t flash_start;
	uint32_t flash_size;
	uint32_t page_size;
} chip_defs[] = {
	{0x416, "STM32L15xx", 0x08000000, 0x20000, 256},
	{0x420, "STM32F100xx", 0x08000000, 0x10000, 1024},
	{ 0 }
};

#define DEFAULT_TIMEOUT 2 /* seconds */
#define DEFAULT_BAUDRATE B38400
#define PAGE_SIZE 256

/* store custom parameters */
speed_t baudrate = DEFAULT_BAUDRATE;
const char *serial_port = "/dev/ttyUSB1";
const char *input_filename;
const char *output_filename;

/* optional command flags */
enum {
	FLAG_UNPROTECT = 0x01,
	FLAG_ERASE     = 0x02,
	FLAG_GO        = 0x04,
};

typedef struct {
	int size;
	uint8_t *data;
} payload_t;

static int has_exterase;

#define MIN(a, b) ((a) < (b) ? (a) : (b))

int open_serial(const char *port)
{
	int fd, res;
	struct termios cfg;

	fd = open(port, O_RDWR | O_NOCTTY);
	if (fd == -1) {
		perror("Unable to open serial port");
		return -1;
	}

	/* put the tty in "raw" mode at the defined baudrate */
	res = tcgetattr(fd, &cfg);
	if (res == -1) {
		perror("Cannot read tty attributes");
		close(fd);
		return -1;
	}
	cfmakeraw(&cfg);
	cfsetspeed(&cfg, baudrate);
	/* serial mode is 8e1 */
	cfg.c_cflag |= PARENB;
	/* 200 ms timeout */
	cfg.c_cc[VTIME] = 2;
	cfg.c_cc[VMIN] = 0;
	res = tcsetattr(fd, TCSANOW, &cfg);
	if (res == -1) {
		perror("Cannot set tty attributes");
		close(fd);
		return -1;
	}

	return fd;
}

int wait_for_ack(int fd)
{
	uint8_t resp;
	int res;
	time_t deadline = time(NULL) + DEFAULT_TIMEOUT;

	while (time(NULL) < deadline) {
		res = read(fd, &resp, 1);
		if ((res < 0) && (errno != EAGAIN)) {
			perror("Failed to read answer");
			return -EIO;
		}
		if (res == 1) {
			if (resp == RESP_ACK)
				return 0;
			else if (resp == RESP_NACK) {
				fprintf(stderr, "NACK\n");
				return -EINVAL;
			} else {
				fprintf(stderr, "Receive junk: %02x\n", resp);
			}
		}
	}
	return -ETIMEDOUT;
}

int send_command(int fd, uint8_t cmd, payload_t *loads, int cnt,
		 uint8_t *resp, int resp_size)
{
	int res, i, c;
	payload_t *p;
	int readcnt = 0;
	int size;
	uint8_t *data;
	uint8_t crc = 0xff ^ cmd; /* XOR checksum */

	/* Send the command index */
	res = write(fd, &cmd, 1);
	if (res <= 0) {
		perror("Failed to write command");
		return -1;
	}
	/* Send the checksum */
	res = write(fd, &crc, 1);
	if (res <= 0) {
		perror("Failed to write checksum");
		return -1;
	}
	/* Wait for the ACK */
	if (wait_for_ack(fd) < 0) {
		fprintf(stderr, "Failed to get command %02x ACK\n", cmd);
		return -1;
	}

	/* Send the command payloads */
	for (p = loads, c = 0; c < cnt ; c++, p++) {
		crc = 0;
		size = p->size;
		data = p->data;
		for (i = 0; i < size ; i++)
			crc ^= data[i];
		if (size == 1)
			crc = 0xff ^ crc;

		while (size) {
			res = write(fd, data, size);
			if (res < 0) {
				perror("Failed to write command payload");
				return -1;
			}
			size -= res;
			data += res;
		}

		/* Send the checksum */
		res = write(fd, &crc, 1);
		if (res <= 0) {
			perror("Failed to write checksum");
			return -1;
		}

		/* Wait for the ACK */
		if (wait_for_ack(fd) < 0) {
			fprintf(stderr, "Failed to get payload %d ACK\n", c);
			return -1;
		}

	}

	/* Read the answer payload */
	if (resp) {
		while ((res = read(fd, resp, resp_size))) {
			if (res < 0) {
				perror("Failed to read payload");
				return -1;
			}
			readcnt += res;
			resp += res;
			resp_size -= res;
		}
	}

	return readcnt;
}

struct stm32_def *command_get_id(int fd)
{
	int res;
	uint8_t id[4];
	uint16_t chipid;
	struct stm32_def *def;

	res = send_command(fd, CMD_GETID, NULL, 0, id, sizeof(id));
	if (res > 0) {
		if (id[0] != 1 || id[3] != RESP_ACK) {
			fprintf(stderr, "unknown ID : %02x %02x %02x %02x\n",
				id[0], id[1], id[2], id[3]);
			return NULL;
		}
		chipid = (id[1] << 8) | id[2];
		for (def = chip_defs ; def->id ; def++)
			if (def->id == chipid)
				break;
		if (def->id == 0)
			def = NULL;
		printf("ChipID 0x%03x : %s\n", chipid, def ? def->name : "???");
		return def;
	}

	return NULL;
}

int init_monitor(int fd)
{
	int res, i;
	uint8_t init = CMD_INIT;
	uint8_t buffer[64];

	printf("Waiting for the monitor startup ...");
	fflush(stdout);

	while (1) {
		/* Send the command index */
		res = write(fd, &init, 1);
		if (res <= 0) {
			perror("Failed to write command");
			return -1;
		}
		/* Wait for the ACK */
		res = wait_for_ack(fd);
		if (res == 0)
			break;
		if (res == -EINVAL) {
			/* we got NACK'ed, the loader might be already started
			 * let's ping it to check
			 */
			if (command_get_id(fd)) {
				printf("Monitor already started.\n");
				return 0;
			}
		}
		if (res < 0 && res != -ETIMEDOUT)
			return -1;
		printf(".");
		fflush(stdout);
	}
	printf("Done.\n");

	/* read trailing chars */
	res = read(fd, buffer, sizeof(buffer));
	if (res > 0) {
		printf("Recv[%d]:", res);
		for (i = 0; i < res; i++)
			printf("%02x ", buffer[i]);
		printf("\n");
	}

	return 0;
}

int command_get_commands(int fd)
{
	int res, i;
	uint8_t cmds[64];

	res = send_command(fd, CMD_GETCMD, NULL, 0, cmds, sizeof(cmds));
	if (res > 0) {
		if ((cmds[0] > sizeof(cmds) - 2) ||
		    (cmds[cmds[0] + 2] != RESP_ACK)) {
			fprintf(stderr, "invalid GET answer (%02x...)\n",
				cmds[0]);
			return -1;
		}
		printf("Bootloader v%d.%d, commands : ",
		       cmds[1] >> 4, cmds[1] & 0xf);
		for (i = 2; i < 2 + cmds[0]; i++) {
			if (cmds[i] == CMD_EXTERASE)
				has_exterase = 1;
			printf("%02x ", cmds[i]);
		}
		printf("\n");
		return 0;
	}

	return -1;
}

int command_read_mem(int fd, uint32_t address, uint32_t size, uint8_t *buffer)
{
	int res;
	uint32_t remaining = size;
	uint32_t addr_be;
	uint8_t cnt;
	payload_t loads[2] = {
		{4, (uint8_t *)&addr_be},
		{1, &cnt}
	};

	while (remaining) {
		cnt = (remaining > PAGE_SIZE) ? PAGE_SIZE - 1 : remaining - 1;
		addr_be = htonl(address);

		printf(".");
		fflush(stdout);
		res = send_command(fd, CMD_READMEM, loads, 2, buffer, cnt + 1);
		if (res < 0)
			return -EIO;

		buffer += cnt + 1;
		address += cnt + 1;
		remaining -= cnt + 1;
	}

	return size;
}

int command_write_mem(int fd, uint32_t address, uint32_t size, uint8_t *buffer)
{
	int res;
	uint32_t remaining = size;
	uint32_t addr_be;
	uint32_t cnt;
	uint8_t outbuf[257];
	payload_t loads[2] = {
		{4, (uint8_t *)&addr_be},
		{sizeof(outbuf), outbuf}
	};

	while (remaining) {
		cnt = (remaining > PAGE_SIZE) ? PAGE_SIZE : remaining;
		addr_be = htonl(address);
		outbuf[0] = cnt - 1;
		loads[1].size = cnt + 1;
		memcpy(outbuf + 1, buffer, cnt);

		printf(".");
		fflush(stdout);
		res = send_command(fd, CMD_WRITEMEM, loads, 2, NULL, 0);
		if (res < 0)
			return -EIO;

		buffer += cnt;
		address += cnt;
		remaining -= cnt;
	}

	return size;
}

int command_ext_erase(int fd, uint16_t count, uint16_t start)
{
	int res;
	uint16_t count_be = htons(count);
	payload_t load = { 2, (uint8_t *)&count_be };
	uint16_t *pages = NULL;

	if (count < 0xfff0) {
		int i;
		/* not a special value : build a list of pages */
		load.size = 2 * (count + 1);
		pages = malloc(load.size);
		if (!pages)
			return -ENOMEM;
		load.data = (uint8_t *)pages;
		pages[0] = htons(count - 1);
		for (i = 0; i < count; i++)
			pages[i+1] = htons(start + i);
	}

	res = send_command(fd, CMD_EXTERASE, &load, 1, NULL, 0);
	if (res >= 0)
		printf("Flash erased.\n");

	if (pages)
		free(pages);
	return res;
}

int command_erase(int fd, uint8_t count, uint8_t start)
{
	int res;
	payload_t load = { 1, (uint8_t *)&count };
	uint8_t *pages = NULL;

	if (count < 0xff) {
		int i;
		/* not a special value : build a list of pages */
		load.size = count + 1;
		pages = malloc(load.size);
		if (!pages)
			return -ENOMEM;
		load.data = (uint8_t *)pages;
		pages[0] = count - 1;
		for (i = 0; i < count; i++)
			pages[i+1] = start + i;
	}

	res = send_command(fd, CMD_ERASE, &load, 1, NULL, 0);
	if (res >= 0)
		printf("Flash erased.\n");

	if (pages)
		free(pages);
	return res;
}

int command_write_unprotect(int fd)
{
	int res;

	res = send_command(fd, CMD_WU, NULL, 0, NULL, 0);
	if (res < 0)
		return -EIO;

	/* Wait for the ACK */
	if (wait_for_ack(fd) < 0) {
		fprintf(stderr, "Failed to get write-protect ACK\n");
		return -EINVAL;
	}
	printf("Flash write unprotected.\n");

	/* This commands triggers a reset */
	if (init_monitor(fd) < 0) {
		fprintf(stderr, "Cannot recover after WP reset\n");
		return -EIO;
	}


	return 0;
}

int command_go(int fd, uint32_t address)
{
	int res;
	uint32_t addr_be = htonl(address);
	payload_t load = { 4, (uint8_t *)&addr_be };

	res = send_command(fd, CMD_GO, &load, 1, NULL, 0);
	if (res < 0)
		return -EIO;

#if 0 /* this ACK should exist according to the documentation ... */
	/* Wait for the ACK */
	if (wait_for_ack(fd) < 0) {
		fprintf(stderr, "Failed to get GO ACK\n");
		return -EINVAL;
	}
#endif

	printf("Program started at 0x%08x.\n", address);
	return 0;
}

int read_flash(int fd, struct stm32_def *chip, const char *filename,
	       uint32_t offset, uint32_t size)
{
	int res;
	FILE *hnd;
	uint8_t *buffer = malloc(size);

	if (!buffer) {
		fprintf(stderr, "Cannot allocate %d bytes\n", size);
		return -ENOMEM;
	}

	hnd = fopen(filename, "w");
	if (!hnd) {
		fprintf(stderr, "Cannot open file %s for writing\n", filename);
		free(buffer);
		return -EIO;
	}

	if (!size)
		size = chip->flash_size;
	offset += chip->flash_start;
	printf("Reading %d bytes at 0x%08x ", size, offset);
	res = command_read_mem(fd, offset, size, buffer);
	if (res > 0) {
		if (fwrite(buffer, res, 1, hnd) != 1)
			fprintf(stderr, "Cannot write %s\n", filename);
	}
	printf("   %d bytes read.\n", res);

	fclose(hnd);
	free(buffer);
	return res;
}

int write_flash(int fd, struct stm32_def *chip, const char *filename,
		uint32_t offset)
{
	int res, written;
	FILE *hnd;
	int size = chip->flash_size;
	uint8_t *buffer = malloc(size);

	if (!buffer) {
		fprintf(stderr, "Cannot allocate %d bytes\n", size);
		return -ENOMEM;
	}

	hnd = fopen(filename, "r");
	if (!hnd) {
		fprintf(stderr, "Cannot open file %s for reading\n", filename);
		free(buffer);
		return -EIO;
	}
	if ((res = fread(buffer, 1, size, hnd)) <= 0) {
		fprintf(stderr, "Cannot read %s\n", filename);
		return -EIO;
	}
	fclose(hnd);

	offset += chip->flash_start;
	printf("Writing %d bytes at 0x%08x ", res, offset);
	written = command_write_mem(fd, offset, res, buffer);
	if (written != res)
		fprintf(stderr, "Error writing to flash\n");
	printf("Done.\n");

	free(buffer);
	return written;
}

static const struct option longopts[] = {
	{"device", 1, 0, 'd'},
	{"read", 1, 0, 'r'},
	{"write", 1, 0, 'w'},
	{"erase", 0, 0, 'e'},
	{"go", 0, 0, 'g'},
	{"help", 0, 0, 'h'},
	{"unprotect", 0, 0, 'u'},
	{"baudrate", 1, 0, 'b'},
	{NULL, 0, 0, 0}
};

void display_usage(char *program)
{
	fprintf(stderr, "Usage: %s [-d <tty>] [-b <baudrate>] [-u] [-e]"
		" [-r <file>] [-w <file>] [-g]\n", program);
	fprintf(stderr, "--d[evice] <tty> : use <tty> as the serial port\n");
	fprintf(stderr, "--b[audrate] <baudrate> : set serial port speed "
			"to <baudrate> bauds\n");
	fprintf(stderr, "--u[nprotect] : remove flash write protect\n");
	fprintf(stderr, "--e[rase] : erase all the flash content\n");
	fprintf(stderr, "--r[ead] <file> : read the flash content and "
			"write it into <file>\n");
	fprintf(stderr, "--w[rite] <file> : read <file> and "
			"write it to flash\n");
	fprintf(stderr, "--g[o] : jump to execute flash entrypoint\n");

	exit(2);
}

speed_t parse_baudrate(const char *value)
{
	int rate = atoi(value);

	switch (rate) {
	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 38400:
		return B38400;
	case 57600:
		return B57600;
	case 115200:
		return B115200;
	default:
		fprintf(stderr, "Invalid baudrate %s, using %d\n",
			value, DEFAULT_BAUDRATE);
		return DEFAULT_BAUDRATE;
	}
}

int parse_parameters(int argc, char **argv)
{
	int opt, idx;
	int flags = 0;

	while ((opt = getopt_long(argc, argv, "b:d:eghr:w:u?",
				  longopts, &idx)) != -1) {
		switch (opt) {
		case 'b':
			baudrate = parse_baudrate(optarg);
			break;
		case 'd':
			serial_port = optarg;
			break;
		case 'e':
			flags |= FLAG_ERASE;
			break;
		case 'g':
			flags |= FLAG_GO;
			break;
		case 'h':
		case '?':
			display_usage(argv[0]);
			break;
		case 'r':
			input_filename = optarg;
			break;
		case 'w':
			output_filename = optarg;
			break;
		case 'u':
			flags |= FLAG_UNPROTECT;
			break;
		}
	}
	return flags;
}

int main(int argc, char **argv)
{
	int ser;
	struct stm32_def *chip;
	int ret = 1;
	int flags;

	/* Parse command line options */
	flags = parse_parameters(argc, argv);

	/* Open the serial port tty */
	ser = open_serial(serial_port);
	if (ser < 0)
		return 1;

	/* Trigger embedded monitor detection */
	if (init_monitor(ser) < 0)
		goto terminate;

	chip = command_get_id(ser);
	if (!chip)
		goto terminate;

	command_get_commands(ser);

	if (flags & FLAG_UNPROTECT)
		command_write_unprotect(ser);

	if (flags & FLAG_ERASE || output_filename) {
		/* Mass erase is not supported on STM32L15xx */
		/* command_ext_erase(ser, ERASE_ALL, 0); */
		int i, page_count = chip->flash_size / chip->page_size;
		for (i = 0; i < page_count; i += 128) {
			int count = MIN(128, page_count - i);
			if (has_exterase)
				command_ext_erase(ser, count, i);
			else
				command_erase(ser, count, i);
		}
	}

	if (input_filename)
		read_flash(ser, chip, input_filename, 0, chip->flash_size);

	if (output_filename)
		write_flash(ser, chip, output_filename, 0);

	/* Run the program from flash */
	if (flags & FLAG_GO)
		command_go(ser, chip->flash_start);

	/* Normal exit */
	ret = 0;
terminate:
	/* Close serial port */
	close(ser);
	return ret;
}
