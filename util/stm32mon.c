/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * STM32 SoC system monitor interface tool
 * For Serial, implement protocol v2.0 as defined in:
 * http://www.st.com/st-web-ui/static/active/en/resource/technical/\
 * document/application_note/CD00264342.pdf
 *
 * For i2C, implement protocol v1.0 as defined in:
 * http://www.st.com/st-web-ui/static/active/en/resource/technical/\
 * document/application_note/DM00072315.pdf
 *
 * For SPI, implement protocol v1.1 as defined in:
 * https://www.st.com/resource/en/application_note/dm00081379.pdf
 */

/* use cfmakeraw() */
#define _DEFAULT_SOURCE /* Newer glibc */
#define _BSD_SOURCE     /* Older glibc */

#include <arpa/inet.h>
#include <compile_time_macros.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/i2c-dev.h>
#include <linux/spi/spidev.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "ec_version.h"

#define KBYTES_TO_BYTES		1024

/*
 * Some Ubuntu versions do not export SPI_IOC_WR_MODE32 even though
 * the kernel shipped on those supports it.
 */
#ifndef SPI_IOC_WR_MODE32
#define SPI_IOC_WR_MODE32		_IOW(SPI_IOC_MAGIC, 5, __u32)
#endif

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
#define CMD_NO_STRETCH_ERASE 0x45 /* Erases while sending busy frame */
#define CMD_WP       0x63 /* Enables write protect */
#define CMD_WU       0x73 /* Disables write protect */
#define CMD_RP       0x82 /* Enables the read protection */
#define CMD_RU       0x92 /* Disables the read protection */

#define RESP_NACK        0x1f
#define RESP_ACK         0x79 /* 0b 0111 1001 */
#define RESP_BUSY        0x76
#define RESP_DAMAGED_ACK 0xBC /* 0b 1011 1100, 1 bit shifted REST_ACK */

/* SPI Start of Frame */
#define SOF          0x5A

/* Extended erase special parameters */
#define ERASE_ALL    0xffff
#define ERASE_BANK1  0xfffe
#define ERASE_BANK2  0xfffd

/* Upper bound of rebooting the monitor */
#define MAX_DELAY_REBOOT 100000 /* us */

/* Standard addresses common across various ST chips */
#define STM32_MAIN_MEMORY_ADDR		0x08000000
#define STM32_SYSTEM_MEMORY_ADDR	0x1FFF0000

#define STM32_UNIQUE_ID_SIZE_BYTES	12

/*
 * Device electronic signature contains factory-programmed identification
 * and calibration data to automatically match the characteristics of the
 * microcontroller.
 */
struct stm32_device_signature {
	/*
	 * Address of the Unique Device ID register. This register contains a
	 * 96-bit value that is unique across all chips.
	 * Zero means ignore/unknown.
	 */
	uint32_t unique_device_id_addr;
	/*
	 * Address of the Flash Size register. This 16-bit register contains the
	 * flash size in KB.
	 * Zero means ignore/unknown.
	 */
	uint32_t flash_size_addr;
	/*
	 * Address of the Package Data register. This 16-bit register contains a
	 * value that differentiates between package types of a given chip.
	 * Zero means ignore/unknown.
	 */
	uint32_t package_data_addr;
};

struct memory_info {
	/* Zero means ignore/unknown/not-applicable */
	uint32_t addr;
	/* If addr is non-zero
	 * - zero here means value is dynamic and will be read from bootloader.
	 * If addr is zero,
	 * - zero here means ignore/unknown/not-applicable.
	 */
	uint32_t size_bytes;
};

struct memory_layout {
	struct memory_info main_memory;
	struct memory_info system_memory;
	struct memory_info otp_area;
	struct memory_info option_bytes;
};

/* known STM32 SoC parameters */
struct stm32_def {
	uint16_t   id;
	const char *name;
	uint32_t flash_size;
	uint32_t page_size;
	uint32_t cmds_len[2];
	const struct memory_layout memory_layout;
	const struct stm32_device_signature device_signature;
} chip_defs[] = {
	{0x416, "STM32L15xxB",   0x20000,   256, {13, 13}, { { 0 } }, { 0 } },
	{0x429, "STM32L15xxB-A", 0x20000,   256, {13, 13}, { { 0 } }, { 0 } },
	{0x427, "STM32L15xxC",   0x40000,   256, {13, 13}, { { 0 } }, { 0 } },
	{0x435, "STM32L44xx",    0x40000,  2048, {13, 13}, { { 0 } }, { 0 } },
	{0x420, "STM32F100xx",   0x20000,  1024, {13, 13}, { { 0 } }, { 0 } },
	{0x410, "STM32F102R8",   0x10000,  1024, {13, 13}, { { 0 } }, { 0 } },
	{0x440, "STM32F05x",     0x10000,  1024, {13, 13}, { { 0 } }, { 0 } },
	{0x444, "STM32F03x",     0x08000,  1024, {13, 13}, { { 0 } }, { 0 } },
	{0x448, "STM32F07xB",    0x20000,  2048, {13, 13}, { { 0 } }, { 0 } },
	{0x432, "STM32F37xx",    0x40000,  2048, {13, 13}, { { 0 } }, { 0 } },
	{0x442, "STM32F09x",     0x40000,  2048, {13, 13}, { { 0 } }, { 0 } },
	{0x431, "STM32F411",     0x80000, 16384, {13, 19}, { { 0 } }, { 0 } },
	{
		.id =		0x441,
		.name =		"STM32F412",
		.flash_size =	0x100000,
		.page_size =	16384,
		.cmds_len =	{13, 19},
		/*
		 * STM32F412:
		 * See https://www.st.com/resource/en/reference_manual/dm00180369.pdf
		 * Section 3.3 Table 5 Flash module organization
		 */
		.memory_layout = {
			.main_memory = {
				.addr = STM32_MAIN_MEMORY_ADDR,
				.size_bytes = 0, /* set by flash reg read */
			},
			.system_memory = {
				.addr = STM32_SYSTEM_MEMORY_ADDR,
				.size_bytes = 30 * KBYTES_TO_BYTES,
			},
			.otp_area = {
				.addr = 0x1FFF7800,
				.size_bytes = 528,
			},
			.option_bytes = {
				.addr = 0x1FFFC000,
				.size_bytes = 16,
			}
		},
		/*
		 * STM32F412:
		 * See https://www.st.com/resource/en/reference_manual/dm00180369.pdf
		 * Section 31 Device electronic signature
		 */
		.device_signature = {
			.unique_device_id_addr =	0x1FFF7A10,
			.flash_size_addr =		0x1FFF7A22,
			/*
			 * Out of range for bootloader on this chip, so we don't
			 * attempt to read.
			 */
			.package_data_addr =		0, /* 0x1FFF7BF0 */
		}
	},
	{0x450, "STM32H74x",    0x200000, 131768, {13, 19}, { { 0 } }, { 0 } },
	{0x451, "STM32F76x",    0x200000, 32768, {13, 19}, { { 0 } }, { 0 } },
	{
		.id =		0x460,
		.name =		"STM32G071xx",
		.flash_size =	0x20000,
		.page_size =	2048,
		.cmds_len =	{13, 13},
		/*
		 * STM32G0x1:
		 * See https://www.st.com/resource/en/reference_manual/dm00371828.pdf
		 * Section 3.3.1 Table 6 Flash module organization
		 */
		.memory_layout = {
			.main_memory = {
				.addr = STM32_MAIN_MEMORY_ADDR,
				.size_bytes = 0, /* set by flash reg read */
			},
			.system_memory = {
				.addr = STM32_SYSTEM_MEMORY_ADDR,
				.size_bytes = 28 * KBYTES_TO_BYTES,
			},
			.otp_area = {
				.addr = 0x1FFF7000,
				.size_bytes = 1024,
			},
			.option_bytes = {
				.addr = 0x1FFF7800,
				.size_bytes = 128,
			}
		},
		/*
		 * STM32G0x1:
		 * See https://www.st.com/resource/en/reference_manual/dm00371828.pdf
		 * Section 38 Device electronic signature
		 */
		.device_signature = {
			.unique_device_id_addr =	0x1FFF7590,
			.flash_size_addr =		0x1FFF75E0,
			/*
			 * Datasheet litst as same address as e.g. STM32F412,
			 * hence declaring as zero as for that other chip.
			 */
			.package_data_addr =		0, /* 0x1FFF7500 */
		}
	},
	{ 0 }
};

#define DEFAULT_CONNECT_RETRIES 5
#define DEFAULT_TIMEOUT 4 /* seconds */
#define EXT_ERASE_TIMEOUT 20 /* seconds */
#define DEFAULT_BAUDRATE B38400
#define PAGE_SIZE 256
#define INVALID_I2C_ADAPTER -1
#define MAX_ACK_RETRY_COUNT	(EXT_ERASE_TIMEOUT / DEFAULT_TIMEOUT)
#define MAX_RETRY_COUNT		3

enum interface_mode {
	MODE_SERIAL,
	MODE_I2C,
	MODE_SPI,
} mode = MODE_SERIAL;

/* I2c address the EC is listening depends on the device:
 * stm32f07xxx: 0x76
 * stm32f411xx: 0x72
 */
#define DEFAULT_I2C_SLAVE_ADDRESS 0x76

/* store custom parameters */
speed_t baudrate = DEFAULT_BAUDRATE;
int connect_retries = DEFAULT_CONNECT_RETRIES;
int i2c_adapter = INVALID_I2C_ADAPTER;
const char *spi_adapter;
int i2c_slave_address = DEFAULT_I2C_SLAVE_ADDRESS;
uint8_t boot_loader_version;
const char *serial_port = "/dev/ttyUSB1";
const char *input_filename;
const char *output_filename;
uint32_t offset = 0x08000000, length = 0;
int retry_on_damaged_ack;

/* STM32MON function return values */
enum {
	STM32_SUCCESS   = 0,
	STM32_EIO       = -1,	/* IO error */
	STM32_EINVAL    = -2,	/* Got a faulty response from device */
	STM32_ETIMEDOUT = -3,	/* Device didn't respond in a time window. */
	STM32_ENOMEM    = -4,	/* Failed to allocate memory. */
	STM32_ENACK     = -5,	/* Got NACK. */
	STM32_EDACK     = -6,	/* Got a damanged ACK. */
};
BUILD_ASSERT(STM32_SUCCESS == 0);
#define IS_STM32_ERROR(res)		((res) < STM32_SUCCESS)

/* optional command flags */
enum {
	FLAG_UNPROTECT      = 0x01,
	FLAG_ERASE          = 0x02,
	FLAG_GO             = 0x04,
	FLAG_READ_UNPROTECT = 0x08,
	FLAG_CR50_MODE	    = 0x10,
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

/* On user request save all data exchange with the target in this log file. */
static FILE *log_file;

/* Statistic data structure for response kind. */
struct {
	const char * const event_name;
	uint32_t event_count;
} stat_resp[] = {
	{ "RESP_ACK",	0 },
	{ "RESP_NACK",	0 },
	{ "RESP_BUSY",	0 },
	{ "RESP_DAMAGED_ACK", 0 },
	{ "JUNK",	0 },
};

enum {
	RESP_ACK_IDX = 0,
	RESP_NACK_IDX,
	RESP_BUSY_IDX,
	RESP_DAMAGED_ACK_IDX,
	JUNK_IDX,
	MAX_EVENT_IDX
};

BUILD_ASSERT(ARRAY_SIZE(stat_resp) == MAX_EVENT_IDX);

/*
 * Print data into the log file, in hex, 16 bytes per line, prefix the first
 * line with the value supplied by the caller (usually 'r' or 'w' for
 * read/write).
 */
static void dump_log(const char *prefix, const void *data, size_t count)
{
	size_t i;

	fprintf(log_file, "%s: ", prefix);
	for (i = 0; i < count; i++) {
		if (i && !(i  % 16))
			fprintf(log_file, "\n   ");
		fprintf(log_file, " %02x", ((uint8_t *)data)[i]);
	}

	if (count % 16)
		fprintf(log_file, "\n");

	/* Make sure all data is there even in case of aborts/crashes. */
	fflush(log_file);
}

/*
 * Wrappers for standard library read() and write() functions. Add transferred
 * data to the log if log file is opened.
 */
static ssize_t read_wrapper(int fd, void *buf, size_t count)
{
	ssize_t rv = read(fd, buf, count);

	if (log_file && (rv > 0))
		dump_log("r", buf, rv);

	return rv;
}

static ssize_t write_wrapper(int fd, const void *buf, size_t count)
{
	ssize_t rv;

	rv = write(fd, buf, count);

	if (log_file && (rv > 0))
		dump_log("w", buf, rv);

	return rv;
}

int open_serial(const char *port, int cr50_mode)
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

	/* Don't bother setting speed and parity when programming over Cr50. */
	if (!cr50_mode) {
		cfsetspeed(&cfg, baudrate);
		/* serial mode should be 8e1 */
		cfg.c_cflag |= PARENB;
	}

	/* 200 ms timeout */
	cfg.c_cc[VTIME] = 2;
	cfg.c_cc[VMIN] = 0;
	memcpy(&cfg_copy, &cfg, sizeof(cfg_copy));

	/*
	 * tcsetattr() returns success if any of the modifications succeed, so
	 * its return value of zero is not an indication of success, one needs
	 * to check the result explicitly.
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
	if (ioctl(fd, I2C_SLAVE, i2c_slave_address >> 1) < 0) {
		perror("Unable to select proper address");
		close(fd);
		return -1;
	}

	return fd;
}

int open_spi(const char *port)
{
	int fd;
	int res;
	uint32_t mode = SPI_MODE_0;
	uint8_t bits = 8;

	fd = open(port, O_RDWR);
	if (fd == -1) {
		perror("Unable to open SPI controller");
		return -1;
	}

	res = ioctl(fd, SPI_IOC_WR_MODE32, &mode);
	if (res == -1) {
		perror("Cannot set SPI mode");
		close(fd);
		return -1;
	}

	res = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	if (res == -1) {
		perror("Cannot set SPI bits per word");
		close(fd);
		return -1;
	}

	return fd;
}

static void discard_input(int fd)
{
	uint8_t buffer[64];
	int res, i;
	int count_of_zeros;

	/* Skip in i2c and spi modes */
	if (mode != MODE_SERIAL)
		return;

	/* eat trailing garbage */
	count_of_zeros = 0;
	do {
		res = read_wrapper(fd, buffer, sizeof(buffer));
		if (res > 0) {

			/* Discard zeros in the beginning of the buffer. */
			for (i = 0; i < res; i++)
				if (buffer[i])
					break;

			count_of_zeros += i;
			if (i == res) {
				/* Only zeros, nothing to print out. */
				continue;
			}

			/* Discard zeros in the end of the buffer. */
			while (!buffer[res - 1]) {
				count_of_zeros++;
				res--;
			}

			printf("Recv[%d]:", res - i);
			for (; i < res; i++)
				printf("%02x ", buffer[i]);
			printf("\n");
		}
	} while (res > 0);

	if (count_of_zeros)
		printf("%d zeros ignored\n", count_of_zeros);
}

int wait_for_ack(int fd)
{
	uint8_t resp;
	int res;
	time_t deadline = time(NULL) + DEFAULT_TIMEOUT;
	const uint8_t ack = RESP_ACK;

	while (time(NULL) < deadline) {
		res = read_wrapper(fd, &resp, 1);
		if ((res < 0) && (errno != EAGAIN)) {
			perror("Failed to read answer");
			return STM32_EIO;
		}

		if (res != 1)
			continue;

		switch (resp) {
		case RESP_ACK:
			stat_resp[RESP_ACK_IDX].event_count++;
			if (mode == MODE_SPI) /* Ack the ACK */
				if (write_wrapper(fd, &ack, 1) != 1)
					return STM32_EIO;
			return STM32_SUCCESS;

		case RESP_NACK:
			stat_resp[RESP_NACK_IDX].event_count++;
			fprintf(stderr, "NACK\n");
			if (mode == MODE_SPI) /* Ack the NACK */
				if (write_wrapper(fd, &ack, 1) != 1)
					return STM32_EIO;
			discard_input(fd);
			return STM32_ENACK;

		case RESP_BUSY:
			stat_resp[RESP_BUSY_IDX].event_count++;
			/* I2C Boot protocol 1.1 */
			deadline = time(NULL) + DEFAULT_TIMEOUT;
			break;

		case RESP_DAMAGED_ACK:
			if (retry_on_damaged_ack) {
				/* It is a damaged ACK. However, device is
				 * likely to believe it sent ACK, so let's not
				 * treat it as junk.
				 */
				stat_resp[RESP_DAMAGED_ACK_IDX].event_count++;
				fprintf(stderr, "DAMAGED_ACK\n");
				return STM32_EDACK;
			}

			/* Do not break so that it can be handled as junk */
		default:
			stat_resp[JUNK_IDX].event_count++;
			if (mode == MODE_SERIAL)
				fprintf(stderr, "Receive junk: %02x\n", resp);
			break;
		}
	}
	fprintf(stderr, "Timeout\n");
	return STM32_ETIMEDOUT;
}

int send_command(int fd, uint8_t cmd, payload_t *loads, int cnt,
		 uint8_t *resp, int resp_size, int ack_requested)
{
	int res, i, c;
	payload_t *p;
	int readcnt = 0;
	uint8_t cmd_frame[] = { SOF, cmd, 0xff ^ cmd }; /* XOR checksum */
	/* only the SPI mode needs the Start Of Frame byte */
	int cmd_off = mode == MODE_SPI ? 0 : 1;
	int count_damaged_ack = 0;

	/* Send the command index */
	res = write_wrapper(fd, cmd_frame + cmd_off,
			    sizeof(cmd_frame) - cmd_off);
	if (res <= 0) {
		perror("Failed to write command frame");
		return STM32_EIO;
	}

	/* Wait for the ACK */
	res = wait_for_ack(fd);
	if (res == STM32_EDACK) {
		++count_damaged_ack;
	} else if (IS_STM32_ERROR(res)) {
		fprintf(stderr, "Failed to get command 0x%02x ACK\n", cmd);
		return res;
	}

	/* Send the command payloads */
	for (p = loads, c = 0; c < cnt; c++, p++) {
		uint8_t crc = 0;
		int size = p->size;
		uint8_t *data = malloc(size + 1), *data_ptr;

		if (data == NULL) {
			fprintf(stderr,
				"Failed to allocate memory for load %d\n", c);
			return STM32_ENOMEM;
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
			res = write_wrapper(fd, data_ptr, size);
			if (res < 0) {
				perror("Failed to write command payload");
				free(data);
				return STM32_EIO;
			}
			size -= res;
			data_ptr += res;
		}
		free(data);

		/* Wait for the ACK */
		res = wait_for_ack(fd);
		if (res == STM32_EDACK) {
			++count_damaged_ack;
		} else if (IS_STM32_ERROR(res)) {
			if (res != STM32_ETIMEDOUT)
				fprintf(stderr,
					"payload %d ACK failed for CMD%02x\n",
					c, cmd);
			return res;
		}
	}

	/* Read the answer payload */
	if (resp) {
		if (mode == MODE_SPI) /* ignore extra byte */
			if (read_wrapper(fd, resp, 1) < 0)
				return STM32_EIO;
		while ((resp_size > 0) &&
		       (res = read_wrapper(fd, resp, resp_size))) {
			if (res < 0) {
				perror("Failed to read payload");
				return STM32_EIO;
			}
			readcnt += res;
			resp += res;
			resp_size -= res;
		}

		/* Wait for the ACK */
		if (ack_requested) {
			res = wait_for_ack(fd);
			if (res == STM32_EDACK) {
				++count_damaged_ack;
			} else if (IS_STM32_ERROR(res)) {
				fprintf(stderr,
					"Failed to get response to command"
					" 0x%02x ACK\n", cmd);
				return res;
			}
		}
	}

	if (count_damaged_ack)
		return STM32_EDACK;

	return readcnt;
}

int send_command_retry(int fd, uint8_t cmd, payload_t *loads,
		int cnt, uint8_t *resp, int resp_size, int ack_requested)
{
	int res;
	int retries = MAX_RETRY_COUNT;

	do {
		int ack_tries = MAX_ACK_RETRY_COUNT;

		res = send_command(fd, cmd, loads, cnt, resp, resp_size,
			ack_requested);

		while (res == STM32_ETIMEDOUT && ack_tries--) {
			if (cmd == CMD_WRITEMEM) {
				/* send garbage byte */
				res = write_wrapper(fd, loads->data, 1);
				/* Don't care much since it is a  garbage
				 * transfer to let the device not wait for
				 * any missing data, if any.
				 */
				if (res < 0)
					fprintf(stderr, "warn: write failed\n");
			}
			res = wait_for_ack(fd);
		}
	} while ((res == STM32_ENACK || res == STM32_EDACK) && retries--);

	return res;
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
	int res = 0;
	int attempts = connect_retries + 1;
	uint8_t init = mode == MODE_SPI ? SOF : CMD_INIT;

	/* Skip in i2c mode */
	if (mode == MODE_I2C)
		return STM32_SUCCESS;

	printf("Waiting for the monitor startup ...");
	fflush(stdout);

	while (connect_retries < 0 || attempts--) {
		/* Send the command index */
		res = write_wrapper(fd, &init, 1);
		if (res <= 0) {
			perror("Failed to write command");
			return STM32_EIO;
		}
		/* Wait for the ACK */
		res = wait_for_ack(fd);
		if (res == STM32_SUCCESS)
			break;
		if (res == STM32_ENACK) {
			/* we got NACK'ed, the loader might be already started
			 * let's ping it to check
			 */
			if (command_get_id(fd)) {
				printf("Monitor already started.\n");
				return STM32_SUCCESS;
			}
		}
		if (IS_STM32_ERROR(res) && res != STM32_ETIMEDOUT)
			return res;
		fflush(stdout);
	}

	if (IS_STM32_ERROR(res)) {
		printf("Giving up after %d attempts.\n", connect_retries + 1);
		return res;
	}

	printf("Done.\n");

	/* read trailing chars */
	discard_input(fd);

	return STM32_SUCCESS;
}

int command_get_commands(int fd, struct stm32_def *chip)
{
	int res, i;
	uint8_t cmds[64];

	/*
	 * For i2c, we have to request the exact amount of bytes we expect.
	 */
	res = send_command(fd, CMD_GETCMD, NULL, 0, cmds,
			   chip->cmds_len[(mode == MODE_I2C ? 1 : 0)], 1);
	if (res > 0) {
		if (cmds[0] > sizeof(cmds) - 2) {
			fprintf(stderr, "invalid GET answer (%02x...)\n",
				cmds[0]);
			return STM32_EINVAL;
		}
		printf("Bootloader v%d.%d, commands : ",
		       cmds[1] >> 4, cmds[1] & 0xf);
		boot_loader_version = cmds[1];

		erase = command_erase;
		for (i = 2; i < 2 + cmds[0]; i++) {
			if (cmds[i] == CMD_EXTERASE)
				erase = command_ext_erase;
			printf("%02x ", cmds[i]);
		}

		if (mode == MODE_I2C)
			erase = command_erase_i2c;
		printf("\n");

		return STM32_SUCCESS;
	}

	fprintf(stderr, "Cannot get bootloader command list.\n");
	return STM32_EINVAL;
}

static int use_progressbar;
static int windex;
static const char wheel[] = {'|', '/', '-', '\\' };
static void draw_spinner(uint32_t remaining, uint32_t size)
{
	int percent = (size - remaining)*100/size;
	if (use_progressbar) {
		int dots = percent / 4;

		while (dots > windex) {
			putchar('#');
			windex++;
		}
	} else {
		printf("\r%c%3d%%", wheel[windex++], percent);
		windex %= sizeof(wheel);
	}
	fflush(stdout);
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
		uint32_t bytes = MIN(remaining, PAGE_SIZE);

		cnt = (uint8_t) (bytes - 1);
		addr_be = htonl(address);

		draw_spinner(remaining, size);

		res = send_command_retry(fd, CMD_READMEM, loads, 2, buffer,
					 bytes, 0);
		if (IS_STM32_ERROR(res))
			return STM32_EIO;

		buffer += bytes;
		address += bytes;
		remaining -= bytes;
	}

	return size;
}

int command_write_mem(int fd, uint32_t address, uint32_t size, uint8_t *buffer)
{
	int res = 0;
	int i;
	uint32_t remaining = size;
	uint32_t addr_be;
	uint32_t cnt;
	uint8_t outbuf[257];
	payload_t loads[2] = {
		{4, (uint8_t *)&addr_be},
		{sizeof(outbuf), outbuf}
	};

	while (remaining) {
		cnt = MIN(remaining, PAGE_SIZE);
		/* skip empty blocks to save time */
		for (i = 0; i < cnt && buffer[i] == 0xff; i++)
			;
		if (i != cnt) {
			addr_be = htonl(address);
			outbuf[0] = cnt - 1;
			loads[1].size = cnt + 1;
			memcpy(outbuf + 1, buffer, cnt);

			draw_spinner(remaining, size);

			res = send_command_retry(fd, CMD_WRITEMEM, loads, 2,
					   NULL, 0, 1);
			if (IS_STM32_ERROR(res))
				return STM32_EIO;
		}
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
			return STM32_ENOMEM;
		load.data = (uint8_t *)pages;
		pages[0] = htons(count - 1);
		for (i = 0; i < count; i++)
			pages[i+1] = htons(start + i);
	}

	printf("Erasing...\n");
	res = send_command_retry(fd, CMD_EXTERASE, &load, 1, NULL, 0, 1);
	if (!IS_STM32_ERROR(res))
		printf("Flash erased.\n");

	if (pages)
		free(pages);
	return res;
}

int command_erase_i2c(int fd, uint16_t count, uint16_t start)
{
	int res;
	uint8_t erase_cmd;
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
			return STM32_ENOMEM;
		load[1].data = (uint8_t *)pages;
		count_be = htons(count - 1);
		for (i = 0; i < count; i++)
			pages[i] = htons(start + i);
	}

	erase_cmd = (boot_loader_version == 0x10) ? CMD_EXTERASE :
		     CMD_NO_STRETCH_ERASE;

	printf("Erasing...\n");
	res = send_command(fd, erase_cmd, load, load_cnt, NULL, 0, 1);
	if (!IS_STM32_ERROR(res))
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
			return STM32_ENOMEM;
		load.data = (uint8_t *)pages;
		pages[0] = count - 1;
		for (i = 0; i < count; i++)
			pages[i+1] = start + i;
	}

	printf("Erasing...\n");
	res = send_command(fd, CMD_ERASE, &load, 1, NULL, 0, 1);
	if (!IS_STM32_ERROR(res))
		printf("Flash erased.\n");

	if (pages)
		free(pages);
	return res;
}

int command_read_unprotect(int fd)
{
	int res;
	int retries = MAX_RETRY_COUNT;

	printf("Unprotecting flash read...\n");

	res = send_command(fd, CMD_RU, NULL, 0, NULL, 0, 1);
	/*
	 * Read unprotect can trigger a mass erase, which can take long time
	 * (e.g. 13s+ on STM32H7)
	 */
	do {
		res = wait_for_ack(fd);
	} while ((res == STM32_ETIMEDOUT) && --retries);

	if (IS_STM32_ERROR(res)) {
		fprintf(stderr, "Failed to get read-protect ACK\n");
		return res;
	}
	printf("Flash read unprotected.\n");

	/*
	 * This command triggers a reset.
	 *
	 * Wait at least the reboot delay, else we could reconnect
	 * before the actual reset depending on the bootloader.
	 */
	usleep(MAX_DELAY_REBOOT);
	if (IS_STM32_ERROR(init_monitor(fd))) {
		fprintf(stderr, "Cannot recover after RU reset\n");
		return STM32_EIO;
	}

	return STM32_SUCCESS;
}

int command_write_unprotect(int fd)
{
	int res;

	res = send_command(fd, CMD_WU, NULL, 0, NULL, 0, 1);
	if (IS_STM32_ERROR(res))
		return STM32_EIO;

	/* Wait for the ACK */
	if (wait_for_ack(fd) < 0) {
		fprintf(stderr, "Failed to get write-protect ACK\n");
		return STM32_EINVAL;
	}
	printf("Flash write unprotected.\n");

	/*
	 * This command triggers a reset.
	 *
	 * Wait at least the reboot delay, else we could reconnect
	 * before the actual reset depending on the bootloader.
	 */
	usleep(MAX_DELAY_REBOOT);
	if (IS_STM32_ERROR(init_monitor(fd))) {
		fprintf(stderr, "Cannot recover after WP reset\n");
		return STM32_EIO;
	}

	return STM32_SUCCESS;
}

int command_go(int fd, uint32_t address)
{
	int res;
	uint32_t addr_be = htonl(address);
	payload_t load = { 4, (uint8_t *)&addr_be };

	res = send_command(fd, CMD_GO, &load, 1, NULL, 0, 1);
	if (IS_STM32_ERROR(res))
		return STM32_EIO;

#if 0 /* this ACK should exist according to the documentation ... */
	/* Wait for the ACK */
	if (wait_for_ack(fd) < 0) {
		fprintf(stderr, "Failed to get GO ACK\n");
		return -EINVAL;
	}
#endif

	printf("Program started at 0x%08x.\n", address);
	return STM32_SUCCESS;
}

/*
 * The bootloader does not allow reading directly from the "device signature"
 * registers. However, it does allow reading the OTP region, so this function
 * starts a read from the last byte in that region and reads an additional
 * number of bytes to read the requested register.
 *
 * Example:
 *
 * Given a chip with OTP region starting at address 0x1FFF7800 with a size of
 * 528 bytes and a register that we want to read at address 0x1FFF7A10 with a
 * size of 12 bytes:
 *
 * We start the read at the last byte in the OTP region:
 *
 * 0x1FFF7800 + 528 - 1 = 0x1FFF7A0F
 *
 * From 0x1FFF7A0F we perform a read of (12 + 1) = 13 bytes in order to read the
 * 12 bytes starting at 0x1FFF7A10 (the actual register we care about).
 *
 * Returns zero on success, negative on failure.
 */
int read_device_signature_register(int fd, const struct stm32_def *chip,
				   uint32_t addr, uint32_t size_bytes,
				   uint8_t *out_buffer)
{
	int res;
	uint8_t *buffer;
	struct memory_info otp = chip->memory_layout.otp_area;
	uint32_t otp_end_addr = otp.addr + otp.size_bytes - 1;
	uint32_t offset = addr - otp_end_addr;
	uint32_t read_size_bytes = offset + size_bytes;

	if (!otp.addr) {
		fprintf(stderr, "No otp_area.addr specified for given chip.\n");
		return STM32_EINVAL;
	}

	if (addr <= otp_end_addr) {
		fprintf(stderr, "Attempting to read from invalid address: "
				"%08X\n", addr);
		return STM32_EINVAL;
	}

	/*
	 * The USART/SPI/I2C bootloader can only read at most 256 bytes in a
	 * single read command (see AN4286 section 2.5 or AN3155 section 3.4).
	 *
	 * command_read_mem will correctly chunk larger requests, but the
	 * subsequent reads will fail because the bootloader won't allow reads
	 * from a starting address that is beyond the OTP region.
	 */
	if (read_size_bytes > PAGE_SIZE) {
		fprintf(stderr,
			"Requested register 0x%08X is outside read range.\n",
			addr);
		return STM32_EINVAL;
	}

	buffer = malloc(read_size_bytes);
	if (!buffer) {
		fprintf(stderr, "Cannot allocate %" PRIu32 " bytes\n",
			read_size_bytes);
		return STM32_ENOMEM;
	}

	res = command_read_mem(fd, otp_end_addr, read_size_bytes, buffer);
	if (res == read_size_bytes)
		memcpy(out_buffer, buffer + offset, size_bytes);
	else
		fprintf(stderr,
			"Cannot read %" PRIu32 " bytes from address 0x%08X",
			read_size_bytes, otp_end_addr);

	free(buffer);
	return IS_STM32_ERROR(res) ? res : STM32_SUCCESS;
}

/* Return zero on success, a negative error value on failures. */
int read_flash_size_register(int fd, struct stm32_def *chip,
			     uint16_t *flash_size_kbytes)
{
	int res;
	uint32_t flash_size_addr = chip->device_signature.flash_size_addr;

	if (!flash_size_addr)
		return STM32_EINVAL;

	res = read_device_signature_register(fd, chip,
		flash_size_addr, sizeof(*flash_size_kbytes),
		(uint8_t *)flash_size_kbytes);

	if (!IS_STM32_ERROR(res))
		printf("Flash size: %" PRIu16 " KB\n", *flash_size_kbytes);
	else
		fprintf(stderr,
			"Unable to read flash size register (0x%08X).\n",
			flash_size_addr);

	return res;
}

/* Return zero on success, a negative error value on failures. */
int read_unique_device_id_register(int fd, struct stm32_def *chip,
	uint8_t device_id[STM32_UNIQUE_ID_SIZE_BYTES])
{
	int i;
	int res;
	uint32_t unique_device_id_addr =
		chip->device_signature.unique_device_id_addr;

	if (!unique_device_id_addr)
		return STM32_EINVAL;

	res = read_device_signature_register(fd, chip, unique_device_id_addr,
		STM32_UNIQUE_ID_SIZE_BYTES, device_id);

	if (!IS_STM32_ERROR(res)) {
		printf("Unique Device ID: 0x");
		for (i = STM32_UNIQUE_ID_SIZE_BYTES - 1; i >= 0; i--)
			printf("%02X", device_id[i]);
		printf("\n");
	} else {
		fprintf(stderr,
			"Unable to read unique device ID register (0x%08X). "
			"Ignoring non-critical failure.\n",
			unique_device_id_addr);
	}

	return res;
}

/* Return zero on success, a negative error value on failures. */
int read_package_data_register(int fd, struct stm32_def *chip,
			       uint16_t *package_data)
{
	int res;
	uint32_t package_data_addr = chip->device_signature.package_data_addr;

	if (!package_data_addr)
		return STM32_EINVAL;

	res = read_device_signature_register(fd, chip, package_data_addr,
					     sizeof(*package_data),
					     (uint8_t *)package_data);

	if (!IS_STM32_ERROR(res))
		printf("Package data register: %04X\n", *package_data);
	else
		fprintf(stderr,
			"Failed to read package data register (0x%08X). "
			"Ignoring non-critical failure.\n", package_data_addr);

	return res;
}

/* Return zero on success, a negative error value on failures. */
int read_flash(int fd, struct stm32_def *chip, const char *filename,
	       uint32_t offset, uint32_t size)
{
	int res;
	FILE *hnd;
	uint8_t *buffer;

	if (!size)
		size = chip->flash_size;
	buffer = malloc(size);
	if (!buffer) {
		fprintf(stderr, "Cannot allocate %d bytes\n", size);
		return STM32_ENOMEM;
	}

	hnd = fopen(filename, "w");
	if (!hnd) {
		fprintf(stderr, "Cannot open file %s for writing\n", filename);
		free(buffer);
		return STM32_EIO;
	}

	printf("Reading %d bytes at 0x%08x\n", size, offset);
	res = command_read_mem(fd, offset, size, buffer);
	if (res > 0) {
		if (fwrite(buffer, res, 1, hnd) != 1)
			fprintf(stderr, "Cannot write %s\n", filename);
	}
	printf("\r   %d bytes read.\n", res);

	fclose(hnd);
	free(buffer);
	return IS_STM32_ERROR(res) ? res : STM32_SUCCESS;
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
		return STM32_ENOMEM;
	}

	if (!strncmp(filename, "-", sizeof("-")))
		hnd = fdopen(STDIN_FILENO, "r");
	else
		hnd = fopen(filename, "r");
	if (!hnd) {
		fprintf(stderr, "Cannot open file %s for reading\n", filename);
		free(buffer);
		return STM32_EIO;
	}
	res = fread(buffer, 1, size, hnd);
	fclose(hnd);
	if (res <= 0) {
		fprintf(stderr, "Cannot read %s\n", filename);
		free(buffer);
		return STM32_EIO;
	}

	/* faster write: skip empty trailing space */
	while (res && buffer[res - 1] == 0xff)
		res--;
	/* ensure 'res' is multiple of 4 given 'size' is and res <= size */
	res = (res + 3) & ~3;

	printf("Writing %d bytes at 0x%08x\n", res, offset);
	written = command_write_mem(fd, offset, res, buffer);
	if (written != res) {
		fprintf(stderr, "Error writing to flash\n");
		free(buffer);
		return STM32_EIO;
	}
	printf("\r   %d bytes written.\n", written);

	free(buffer);
	return STM32_SUCCESS;
}

static const struct option longopts[] = {
	{"adapter", 1, 0, 'a'},
	{"baudrate", 1, 0, 'b'},
	{"cr50", 0, 0, 'c'},
	{"device", 1, 0, 'd'},
	{"erase", 0, 0, 'e'},
	{"go", 0, 0, 'g'},
	{"help", 0, 0, 'h'},
	{"length", 1, 0, 'n'},
	{"location", 1, 0, 'l'},
	{"logfile", 1, 0, 'L'},
	{"offset", 1, 0, 'o'},
	{"progressbar", 0, 0, 'p'},
	{"read", 1, 0, 'r'},
	{"retries", 1, 0, 'R'},
	{"spi", 1, 0, 's'},
	{"unprotect", 0, 0, 'u'},
	{"version", 0, 0, 'v'},
	{"write", 1, 0, 'w'},
	{NULL, 0, 0, 0}
};

void display_usage(char *program)
{
	fprintf(stderr,
		"Usage: %s [-a <i2c_adapter> [-l address ]] | [-s]"
		" [-d <tty>] [-b <baudrate>]] [-u] [-e] [-U]"
		" [-r <file>] [-w <file>] [-o offset] [-n length] [-g] [-p]"
		" [-L <log_file>] [-c] [-v]\n",
		program);
	fprintf(stderr, "Can access the controller via serial port or i2c\n");
	fprintf(stderr, "Serial port mode:\n");
	fprintf(stderr, "--d[evice] <tty> : use <tty> as the serial port\n");
	fprintf(stderr, "--b[audrate] <baudrate> : set serial port speed "
			"to <baudrate> bauds\n");
	fprintf(stderr, "i2c mode:\n");
	fprintf(stderr, "--a[dapter] <id> : use i2c adapter <id>.\n");
	fprintf(stderr, "--l[ocation]  <address> : use address <address>.\n");
	fprintf(stderr, "--s[pi]: use spi mode.\n");
	fprintf(stderr, "--u[nprotect] : remove flash write protect\n");
	fprintf(stderr, "--U[nprotect] : remove flash read protect\n");
	fprintf(stderr, "--e[rase] : erase all the flash content\n");
	fprintf(stderr, "--r[ead] <file> : read the flash content and "
			"write it into <file>\n");
	fprintf(stderr, "--s[pi] </dev/spi> : use SPI adapter on </dev>.\n");
	fprintf(stderr, "--w[rite] <file|-> : read <file> or\n\t"
			"standard input and write it to flash\n");
	fprintf(stderr, "--o[ffset] : offset to read/write/start from/to\n");
	fprintf(stderr, "--n[length] : amount to read/write\n");
	fprintf(stderr, "--g[o] : jump to execute flash entrypoint\n");
	fprintf(stderr, "--p[rogressbar] : use a progress bar instead of "
			"the spinner\n");
	fprintf(stderr, "--R[etries] <num> : limit connect retries to num\n");
	fprintf(stderr, "-L[ogfile] <file> : save all communications exchange "
		"in a log file\n");
	fprintf(stderr, "-c[r50_mode] : consider device to be a Cr50 interface,"
		" no need to set UART port attributes\n");
	fprintf(stderr, "--v[ersion] : print version and exit\n");

	exit(2);
}

void display_version(const char *exe_name)
{
	printf("%s version: %s %s %s\n", exe_name, CROS_STM32MON_VERSION, DATE,
		BUILDER);
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
	const char *log_file_name = NULL;

	while ((opt = getopt_long(argc, argv, "a:l:b:cd:eghL:n:o:pr:R:s:w:uUv?",
				  longopts, &idx)) != -1) {
		switch (opt) {
		case 'a':
			i2c_adapter = atoi(optarg);
			mode = MODE_I2C;
			break;
		case 'l':
			i2c_slave_address = strtol(optarg, NULL, 0);
			break;
		case 'b':
			baudrate = parse_baudrate(optarg);
			break;
		case 'c':
			flags |= FLAG_CR50_MODE;
			break;
		case 'd':
			serial_port = optarg;
			mode = MODE_SERIAL;
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
		case 'L':
			log_file_name = optarg;
			break;
		case 'n':
			length = strtol(optarg, NULL, 0);
			break;
		case 'o':
			offset = strtol(optarg, NULL, 0);
			break;
		case 'p':
			use_progressbar = 1;
			break;
		case 'r':
			input_filename = optarg;
			break;
		case 'R':
			connect_retries = atoi(optarg);
			break;
		case 's':
			spi_adapter = optarg;
			mode = MODE_SPI;
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
		case 'v':
			display_version(argv[0]);
			exit(0);
		}
	}

	if (log_file_name) {
		log_file = fopen(log_file_name, "w");
		if (!log_file) {
			fprintf(stderr, "failed to open %s for writing\n",
				log_file_name);
			exit(2);
		}
	}
	return flags;
}

static void display_stat_response(void)
{
	uint32_t total_events = MAX_EVENT_IDX;
	uint32_t idx;

	printf("--\n");
	for (idx = 0; idx < total_events; ++idx) {
		printf("%-18s %d\n", stat_resp[idx].event_name,
				stat_resp[idx].event_count);
	}
	printf("--\n");
}

int main(int argc, char **argv)
{
	int ser;
	struct stm32_def *chip;
	int ret = STM32_EIO;
	int res;
	int flags;
	uint16_t flash_size_kbytes = 0;
	uint8_t unique_device_id[STM32_UNIQUE_ID_SIZE_BYTES] = { 0 };
	uint16_t package_data_reg = 0;

	/* Parse command line options */
	flags = parse_parameters(argc, argv);

	display_version(argv[0]);

	retry_on_damaged_ack = !!(flags & FLAG_CR50_MODE);

	switch (mode) {
	case MODE_SPI:
		ser = open_spi(spi_adapter);
		break;
	case MODE_I2C:
		ser = open_i2c(i2c_adapter);
		break;
	case MODE_SERIAL:
	default:
		/* Open the serial port tty */
		ser = open_serial(serial_port, !!(flags & FLAG_CR50_MODE));
	}
	if (ser < 0)
		return 1;
	/* Trigger embedded monitor detection */
	res = init_monitor(ser);
	if (IS_STM32_ERROR(res))
		goto terminate;

	chip = command_get_id(ser);
	if (!chip)
		goto terminate;

	/*
	 * Use the actual size if we were able to read it since some chips
	 * have the same chip ID, but different flash sizes based on the
	 * package.
	 */
	res = read_flash_size_register(ser, chip, &flash_size_kbytes);
	if (!IS_STM32_ERROR(res))
		chip->flash_size = flash_size_kbytes * KBYTES_TO_BYTES;

	/*
	 * This is simply informative at the moment, so we don't care about the
	 * return value.
	 */
	(void)read_unique_device_id_register(ser, chip, unique_device_id);

	/*
	 * This is simply informative at the moment, so we don't care about the
	 * return value.
	 */
	(void)read_package_data_register(ser, chip, &package_data_reg);

	if (command_get_commands(ser, chip) < 0)
		goto terminate;

	if (flags & FLAG_READ_UNPROTECT)
		command_read_unprotect(ser);
	if (flags & FLAG_UNPROTECT)
		command_write_unprotect(ser);

	if (flags & FLAG_ERASE || output_filename) {
		if ((!strncmp("STM32L15", chip->name, 8)) ||
		    (!strncmp("STM32F411", chip->name, 9))) {
			/* Mass erase is not supported on these chips*/
			int i, page_count = chip->flash_size / chip->page_size;
			for (i = 0; i < page_count; i += 128) {
				int count = MIN(128, page_count - i);
				ret = erase(ser, count, i);
				if (IS_STM32_ERROR(ret))
					goto terminate;
			}
		} else {
			ret = erase(ser, 0xFFFF, 0);
			if (IS_STM32_ERROR(ret))
				goto terminate;
		}
	}

	if (input_filename) {
		ret = read_flash(ser, chip, input_filename, offset, length);
		if (IS_STM32_ERROR(ret))
			goto terminate;
	}

	if (output_filename) {
		ret = write_flash(ser, chip, output_filename, offset);
		if (IS_STM32_ERROR(ret))
			goto terminate;
	}

	/* Run the program from flash */
	if (flags & FLAG_GO)
		command_go(ser, offset);

	/* Normal exit */
	ret = STM32_SUCCESS;
terminate:
	if (log_file)
		fclose(log_file);

	/* Close serial port */
	close(ser);

	if (retry_on_damaged_ack)
		display_stat_response();

	if (IS_STM32_ERROR(ret)) {
		fprintf(stderr, "Failed: %d\n", ret);
		return 1;
	}

	printf("Done.\n");
	return 0;
}
