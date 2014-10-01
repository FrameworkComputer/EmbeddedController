/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * STM32 SoC system monitor interface tool
 * For Serial, implement proctol v2.0 as defined in:
 * http://www.st.com/st-web-ui/static/active/en/resource/technical/\
 * document/application_note/CD00264342.pdf
 *
 * For i2C, implement protocol v1.0 as defined in:
 * http://www.st.com/st-web-ui/static/active/en/resource/technical/\
 * document/application_note/DM00072315.pdf
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
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/i2c-dev.h>
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
	uint32_t cmds_len;
} chip_defs[] = {
	{0x416, "STM32L15xxB",   0x08000000, 0x20000, 256, 13},
	{0x429, "STM32L15xxB-A", 0x08000000, 0x20000, 256, 13},
	{0x427, "STM32L15xxC",   0x08000000, 0x40000, 256, 13},
	{0x420, "STM32F100xx",   0x08000000, 0x20000, 1024, 13},
	{0x410, "STM32F102R8",   0x08000000, 0x10000, 1024, 13},
	{0x440, "STM32F05x",     0x08000000, 0x10000, 1024, 13},
	{0x444, "STM32F03x",     0x08000000, 0x08000, 1024, 13},
	{0x448, "STM32F07xB",    0x08000000, 0x20000, 2048, 13},
	{ 0 }
};

#define DEFAULT_TIMEOUT 4 /* seconds */
#define DEFAULT_BAUDRATE B38400
#define PAGE_SIZE 256
#define INVALID_I2C_ADAPTER -1

/* store custom parameters */
speed_t baudrate = DEFAULT_BAUDRATE;
int i2c_adapter = INVALID_I2C_ADAPTER;
const char *serial_port = "/dev/ttyUSB1";
const char *input_filename;
const char *output_filename;

/* optional command flags */
enum {
	FLAG_UNPROTECT      = 0x01,
	FLAG_ERASE          = 0x02,
	FLAG_GO             = 0x04,
	FLAG_READ_UNPROTECT = 0x08,
};

typedef struct {
	int size;
	uint8_t *data;
} payload_t;

/* List all possible flash erase functions */
typedef int command_erase_t(int fd, uint16_t count, uint16_t start);
command_erase_t command_erase;
command_erase_t command_ext_erase;
command_erase_t command_erase_i2c;

command_erase_t *erase;

static void discard_input(int);

#define MIN(a, b) ((a) < (b) ? (a) : (b))

int open_serial(const char *port)
{
	int fd, res;
	struct termios cfg, cfg_copy;

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
	/* serial mode should be 8e1 */
	cfg.c_cflag |= PARENB;
	/* 200 ms timeout */
	cfg.c_cc[VTIME] = 2;
	cfg.c_cc[VMIN] = 0;
	memcpy(&cfg_copy, &cfg, sizeof(cfg_copy));

	/*
	 * tcsetattr() returns success if any of the modifications succeed, so
	 * its return value of zero is not an indication of success, one needs
	 * to check the result explicitely.
	 */
	tcsetattr(fd, TCSANOW, &cfg);
	if (tcgetattr(fd, &cfg)) {
		perror("Failed to re-read tty attributes");
		close(fd);
		return -1;
	}

	if (memcmp(&cfg, &cfg_copy, sizeof(cfg))) {
		/*
		 * On some systems the setting which does not come through is
		 * the parity. We can try continuing without it when using
		 * certain interfaces, let's try.
		 */
		cfg_copy.c_cflag &= ~PARENB;
		if (memcmp(&cfg, &cfg_copy, sizeof(cfg))) {
			/*
			 * Something other than parity failed to get set, this
			 * is an error.
			 */
			perror("Cannot set tty attributes");
			close(fd);
			return -1;
		} else {
			fprintf(stderr, "Failed to enable parity\n");
		}
	}

	discard_input(fd); /* in case were were invoked soon after reset */
	return fd;
}

int open_i2c(const int port)
{
	int fd;
	char filename[20];

	snprintf(filename, 19, "/dev/i2c-%d", port);
	fd = open(filename, O_RDWR);
	if (fd < 0) {
		perror("Unable to open i2c adapter");
		return -1;
	}
	/*
	 * When in I2C mode, the bootloader is listening at address 0x76 (10 bit
	 * mode), 0x3B (7 bit mode)
	 */
	if (ioctl(fd, I2C_SLAVE, 0x3B) < 0) {
		perror("Unable to select proper address");
		close(fd);
		return -1;
	}

	return fd;
}


static void discard_input(int fd)
{
	uint8_t buffer[64];
	int res, i;

	/* Skip in i2c mode */
	if (i2c_adapter != INVALID_I2C_ADAPTER)
		return;

	/* eat trailing garbage */
	do {
		res = read(fd, buffer, sizeof(buffer));
		if (res > 0) {
			printf("Recv[%d]:", res);
			for (i = 0; i < res; i++)
				printf("%02x ", buffer[i]);
			printf("\n");
		}
	} while (res > 0);
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
				discard_input(fd);
				return -EINVAL;
			} else {
				fprintf(stderr, "Receive junk: %02x\n", resp);
			}
		}
	}
	fprintf(stderr, "Timeout\n");
	return -ETIMEDOUT;
}

int send_command(int fd, uint8_t cmd, payload_t *loads, int cnt,
		 uint8_t *resp, int resp_size, int ack_requested)
{
	int res, i, c;
	payload_t *p;
	int readcnt = 0;
	uint8_t cmd_frame[] = { cmd, 0xff ^ cmd }; /* XOR checksum */

	/* Send the command index */
	res = write(fd, cmd_frame, 2);
	if (res <= 0) {
		perror("Failed to write command frame");
		return -1;
	}

	/* Wait for the ACK */
	if (wait_for_ack(fd) < 0) {
		fprintf(stderr, "Failed to get command 0x%02x ACK\n", cmd);
		return -1;
	}

	/* Send the command payloads */
	for (p = loads, c = 0; c < cnt; c++, p++) {
		uint8_t crc = 0;
		int size = p->size;
		uint8_t *data = malloc(size + 1), *data_ptr;

		if (data == NULL) {
			fprintf(stderr,
				"Failed to allocate memory for load %d\n", c);
			return -ENOMEM;
		}
		memcpy(data, p->data, size);
		for (i = 0; i < size; i++)
			crc ^= data[i];
		if (size == 1)
			crc = 0xff ^ crc;
		data[size] = crc;
		size++;
		data_ptr = data;
		while (size) {
			res = write(fd, data_ptr, size);
			if (res < 0) {
				perror("Failed to write command payload");
				free(data);
				return -1;
			}
			size -= res;
			data_ptr += res;
		}

		/* Wait for the ACK */
		if (wait_for_ack(fd) < 0) {
			fprintf(stderr, "payload %d ACK failed for CMD%02x\n",
					c, cmd);
			free(data);
			return -1;
		}
		free(data);
	}

	/* Read the answer payload */
	if (resp) {
		while ((resp_size > 0) && (res = read(fd, resp, resp_size))) {
			if (res < 0) {
				perror("Failed to read payload");
				return -1;
			}
			readcnt += res;
			resp += res;
			resp_size -= res;
		}

		/* Wait for the ACK */
		if (ack_requested) {
			if (wait_for_ack(fd) < 0) {
				fprintf(stderr,
					"Failed to get response to command 0x%02x ACK\n",
					cmd);
				return -1;
			}
		}
	}
	return readcnt;
}

struct stm32_def *command_get_id(int fd)
{
	int res;
	uint8_t id[3];
	uint16_t chipid;
	struct stm32_def *def;

	res = send_command(fd, CMD_GETID, NULL, 0, id, sizeof(id), 1);
	if (res > 0) {
		if (id[0] != 1) {
			fprintf(stderr, "unknown ID : %02x %02x %02x\n",
				id[0], id[1], id[2]);
			return NULL;
		}
		chipid = (id[1] << 8) | id[2];
		for (def = chip_defs; def->id; def++)
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
	int res;
	uint8_t init = CMD_INIT;

	/* Skip in i2c mode */
	if (i2c_adapter != INVALID_I2C_ADAPTER)
		return 0;

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
		fflush(stdout);
	}
	printf("Done.\n");

	/* read trailing chars */
	discard_input(fd);

	return 0;
}

int command_get_commands(int fd, struct stm32_def *chip)
{
	int res, i;
	uint8_t cmds[64];

	/*
	 * For i2c, we have to request the exact amount of bytes we expect.
	 * TODO(gwendal): Broken on device with Bootloader version 1.1
	 */
	res = send_command(fd, CMD_GETCMD, NULL, 0, cmds, chip->cmds_len, 1);
	if (res > 0) {
		if (cmds[0] > sizeof(cmds) - 2) {
			fprintf(stderr, "invalid GET answer (%02x...)\n",
				cmds[0]);
			return -1;
		}
		printf("Bootloader v%d.%d, commands : ",
		       cmds[1] >> 4, cmds[1] & 0xf);

		erase = command_erase;
		for (i = 2; i < 2 + cmds[0]; i++) {
			if (cmds[i] == CMD_EXTERASE)
				erase = command_ext_erase;
			printf("%02x ", cmds[i]);
		}

		if (i2c_adapter != INVALID_I2C_ADAPTER)
			erase = command_erase_i2c;
		printf("\n");

		return 0;
	}

	return -1;
}

static int windex;
static const char wheel[] = {'|', '/', '-', '\\' };
static void draw_spinner(uint32_t remaining, uint32_t size)
{
	int percent = (size - remaining)*100/size;
	printf("\r%c%3d%%", wheel[windex++], percent);
	windex %= sizeof(wheel);
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

		draw_spinner(remaining, size);
		fflush(stdout);
		res = send_command(fd, CMD_READMEM, loads, 2, buffer, cnt + 1,
				   0);
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

		draw_spinner(remaining, size);
		fflush(stdout);
		res = send_command(fd, CMD_WRITEMEM, loads, 2, NULL, 0, 1);
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

	res = send_command(fd, CMD_EXTERASE, &load, 1, NULL, 0, 1);
	if (res >= 0)
		printf("Flash erased.\n");

	if (pages)
		free(pages);
	return res;
}

int command_erase_i2c(int fd, uint16_t count, uint16_t start)
{
	int res;
	uint16_t count_be = htons(count);
	payload_t load[2] = {
		{ 2, (uint8_t *)&count_be},
		{ 0, NULL},
	};
	int load_cnt = 1;
	uint16_t *pages = NULL;

	if (count < 0xfff) {
		int i;
		/* not a special value : build a list of pages */
		/*
		 * I2c protocol requires 2 messages, the count has to be acked
		 * before the addresses can be sent.
		 * TODO(gwendal): Still broken on i2c.
		 */
		load_cnt = 2;
		load[1].size = 2 * count;
		pages = malloc(load[1].size);
		if (!pages)
			return -ENOMEM;
		load[1].data = (uint8_t *)pages;
		count_be = htons(count - 1);
		for (i = 0; i < count; i++)
			pages[i] = htons(start + i);
	} else {
		load_cnt = 1;
	}

	res = send_command(fd, CMD_EXTERASE, load, load_cnt,
			   NULL, 0, 1);
	if (res >= 0)
		printf("Flash erased.\n");

	if (pages)
		free(pages);
	return res;
}


int command_erase(int fd, uint16_t count, uint16_t start)
{
	int res;
	uint8_t count_8bit = count;
	payload_t load = { 1, &count_8bit };
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

	res = send_command(fd, CMD_ERASE, &load, 1, NULL, 0, 1);
	if (res >= 0)
		printf("Flash erased.\n");

	if (pages)
		free(pages);
	return res;
}

int command_read_unprotect(int fd)
{
	int res;

	res = send_command(fd, CMD_RU, NULL, 0, NULL, 0, 1);
	if (res < 0)
		return -EIO;

	/* Wait for the ACK */
	if (wait_for_ack(fd) < 0) {
		fprintf(stderr, "Failed to get read-protect ACK\n");
		return -EINVAL;
	}
	printf("Flash read unprotected.\n");

	/* This commands triggers a reset */
	if (init_monitor(fd) < 0) {
		fprintf(stderr, "Cannot recover after RP reset\n");
		return -EIO;
	}

	return 0;
}

int command_write_unprotect(int fd)
{
	int res;

	res = send_command(fd, CMD_WU, NULL, 0, NULL, 0, 1);
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

	res = send_command(fd, CMD_GO, &load, 1, NULL, 0, 1);
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

/* Return zero on success, a negative error value on failures. */
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
	printf("Reading %d bytes at 0x%08x\n", size, offset);
	res = command_read_mem(fd, offset, size, buffer);
	if (res > 0) {
		if (fwrite(buffer, res, 1, hnd) != 1)
			fprintf(stderr, "Cannot write %s\n", filename);
	}
	printf("\r   %d bytes read.\n", res);

	fclose(hnd);
	free(buffer);
	return (res < 0) ? res : 0;
}

/* Return zero on success, a negative error value on failures. */
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

	if (!strncmp(filename, "-", sizeof("-")))
		hnd = fdopen(STDIN_FILENO, "r");
	else
		hnd = fopen(filename, "r");
	if (!hnd) {
		fprintf(stderr, "Cannot open file %s for reading\n", filename);
		free(buffer);
		return -EIO;
	}
	res = fread(buffer, 1, size, hnd);
	if (res <= 0) {
		fprintf(stderr, "Cannot read %s\n", filename);
		free(buffer);
		return -EIO;
	}
	fclose(hnd);

	offset += chip->flash_start;
	printf("Writing %d bytes at 0x%08x\n", res, offset);
	written = command_write_mem(fd, offset, res, buffer);
	if (written != res) {
		fprintf(stderr, "Error writing to flash\n");
		free(buffer);
		return -EIO;
	}
	printf("\rDone.\n");

	free(buffer);
	return 0;
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
	{"adapter", 1, 0, 'a'},
	{NULL, 0, 0, 0}
};

void display_usage(char *program)
{
	fprintf(stderr,
		"Usage: %s [-a <i2c_adapter> | [-d <tty>] [-b <baudrate>]]"
		" [-u] [-e] [-U] [-r <file>] [-w <file>] [-g]\n", program);
	fprintf(stderr, "Can access the controller via serial port or i2c\n");
	fprintf(stderr, "Serial port mode:\n");
	fprintf(stderr, "--d[evice] <tty> : use <tty> as the serial port\n");
	fprintf(stderr, "--b[audrate] <baudrate> : set serial port speed "
			"to <baudrate> bauds\n");
	fprintf(stderr, "i2c mode:\n");
	fprintf(stderr, "--a[dapter] <id> : use i2c adapter <id>.\n\n");
	fprintf(stderr, "--u[nprotect] : remove flash write protect\n");
	fprintf(stderr, "--U[nprotect] : remove flash read protect\n");
	fprintf(stderr, "--e[rase] : erase all the flash content\n");
	fprintf(stderr, "--r[ead] <file> : read the flash content and "
			"write it into <file>\n");
	fprintf(stderr, "--w[rite] <file|-> : read <file> or\n\t"
			"standard input and write it to flash\n");
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

	while ((opt = getopt_long(argc, argv, "a:b:d:eghr:w:uU?",
				  longopts, &idx)) != -1) {
		switch (opt) {
		case 'a':
			i2c_adapter = atoi(optarg);
			break;
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
		case 'U':
			flags |= FLAG_READ_UNPROTECT;
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

	if (i2c_adapter == INVALID_I2C_ADAPTER) {
		/* Open the serial port tty */
		ser = open_serial(serial_port);
	} else {
		ser = open_i2c(i2c_adapter);
	}
	if (ser < 0)
		return 1;
	/* Trigger embedded monitor detection */
	if (init_monitor(ser) < 0)
		goto terminate;

	chip = command_get_id(ser);
	if (!chip)
		goto terminate;

	command_get_commands(ser, chip);

	if (flags & FLAG_READ_UNPROTECT)
		command_read_unprotect(ser);
	if (flags & FLAG_UNPROTECT)
		command_write_unprotect(ser);

	if (flags & FLAG_ERASE || output_filename) {
		if (!strcmp("STM32L15", chip->name)) {
			/* Mass erase is not supported on STM32L15xx */
			/* command_ext_erase(ser, ERASE_ALL, 0); */
			int i, page_count = chip->flash_size / chip->page_size;
			for (i = 0; i < page_count; i += 128) {
				int count = MIN(128, page_count - i);
				ret = erase(ser, count, i);
				if (ret)
					goto terminate;
			}
		} else {
			ret = erase(ser, 0xFFFF, 0);
			if (ret)
				goto terminate;
		}
	}

	if (input_filename) {
		ret = read_flash(ser, chip, input_filename,
				 0, chip->flash_size);
		if (ret)
			goto terminate;
	}

	if (output_filename) {
		ret = write_flash(ser, chip, output_filename, 0);
		if (ret)
			goto terminate;
	}

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
