/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ITE83xx SoC in-system programming tool
 */

/* remove when ftdi_usb_purge_buffers has been replaced to follow libftdi */
#include <cstdint>
#include <memory>
#define _FTDI_DISABLE_DEPRECATED

#include "compile_time_macros.h"
#include "usb_if.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <ftdi.h>
#include <getopt.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

/* Default FTDI device : Servo v2. */
#define SERVO_USB_VID 0x18d1
#define SERVO_USB_PID 0x5002
#define SERVO_INTERFACE INTERFACE_B

/* Default CCD device: Cr50. */
#define CR50_USB_VID 0x18d1
#define CR50_USB_PID 0x5014

/* Cr50 exposed properties of the USB I2C endpoint. */
#define CR50_I2C_SUBCLASS 82
#define CR50_I2C_PROTOCOL 1

#define CROS_CMD_ADDR 0x78 /* USB_I2C_CMD_ADDR 0xF0 >> 1 */
#define CROS_CMD_ITE_SYNC 0

/* DBGR I2C addresses */
#define I2C_CMD_ADDR 0x5A
#define I2C_DATA_ADDR 0x35
#define I2C_BLOCK_ADDR 0x79

#define FTDI_I2C_FREQ 400000

/* I2C pins on the FTDI interface */
#define SCL_BIT BIT(0)
#define SDA_BIT BIT(1)

/* Chip ID register value */
#define CHIP_ID 0x8380

/* Embedded flash page size */
#define PAGE_SIZE (1 << 8)

/* Embedded flash block write size for different programming modes. */
#define FTDI_BLOCK_WRITE_SIZE (1 << 16)

/* JEDEC SPI Flash commands */
#define SPI_CMD_PAGE_PROGRAM 0x02
#define SPI_CMD_WRITE_DISABLE 0x04
#define SPI_CMD_READ_STATUS 0x05
#define SPI_CMD_WRITE_ENABLE 0x06
#define SPI_CMD_FAST_READ 0x0B
#define SPI_CMD_CHIP_ERASE 0x60
#define SPI_CMD_SECTOR_ERASE_1K 0xD7
#define SPI_CMD_SECTOR_ERASE_4K 0x20
#define SPI_CMD_WORD_PROGRAM 0xAD
#define SPI_CMD_EWSR 0x50 /* Enable Write Status Register */
#define SPI_CMD_WRSR 0x01 /* Write Status Register */
#define SPI_CMD_RDID 0x9F /* Read Flash ID */

/* Size for FTDI outgoing buffer */
#define FTDI_CMD_BUF_SIZE (1 << 12)

/* Reset Status */
#define RSTS_VCCDO_PW_ON 0x40
#define RSTS_VFSPIPG 0x20
#define RSTS_HGRST 0x08
#define RSTS_GRST 0x04

/* I2C MUX Configuration: TCA9543 or PCA9546 */
#define I2C_MUX_CMD_ADDR 0x70
#define I2C_MUX_CMD_NONE 0x00
#define I2C_MUX_CMD_INAS 0x01
#define I2C_MUX_CMD_EC 0x02

/* Eflash Type*/
#define EFLASH_TYPE_8315 0x01
#define EFLASH_TYPE_KGD 0x02
#define EFLASH_TYPE_NONE 0xFF

uint8_t eflash_type;
uint8_t spi_cmd_sector_erase;

/* Embedded flash number of pages in a sector erase */
uint8_t sector_erase_pages;

static volatile sig_atomic_t exit_requested;

struct i2c_interface;

/* Config mostly comes from the command line.  Defaults are set in main(). */
struct iteflash_config {
	char *input_filename;
	char *output_filename;
	int send_waveform; /* boolean */
	int erase; /* boolean */
	int i2c_mux; /* boolean */
	int debug; /* boolean */
	int disable_watchdog; /* boolean */
	int disable_protect_path; /* boolean */
	int block_write_size;
	int usb_interface;
	int usb_vid;
	int usb_pid;
	int verify; /* boolean */
	char *usb_serial;
	char *i2c_dev_path;
	const struct i2c_interface *i2c_if;
	size_t range_base;
	size_t range_size;
};

struct common_hnd {
	struct iteflash_config conf;
	int flash_size;
	int flash_cmd_v2; /* boolean */
	int dbgr_addr_3bytes; /* boolean */
	union {
		int i2c_dev_fd;
		struct usb_endpoint uep;
		struct ftdi_context *ftdi_hnd;
	};
};

struct cmds {
	uint8_t addr;
	uint8_t cmd;
};

/* For all callback return values, zero indicates success, non-zero failure. */
struct i2c_interface {
	/* Optional, may be NULL. */
	int (*interface_init)(struct common_hnd *chnd);
	/* Always called if non-NULL, even if special waveform is skipped! */
	/* Optional, may be NULL. */
	int (*interface_post_waveform)(struct common_hnd *chnd);
	/* Called exactly once if and only if interface_init() succeeded. */
	/* Optional, may be NULL. */
	int (*interface_shutdown)(struct common_hnd *chnd);
	/* Optional, may be NULL (unsupported for this I2C interface type). */
	int (*send_special_waveform)(struct common_hnd *chnd);
	/* Required, must not be NULL. */
	int (*byte_transfer)(struct common_hnd *chnd, uint8_t addr,
			     uint8_t *data, int write, int numbytes);
	/* Required, must be positive. */
	int default_block_write_size;
};

static int spi_flash_command_short(struct common_hnd *chnd, uint8_t cmd,
				   const char *desc);

static void null_and_free(void **ptr)
{
	void *holder;

	if (*ptr) {
		holder = *ptr;
		*ptr = NULL;
		free(holder);
	}
}

/* This releases any memory owned by *conf.  This does NOT free conf itself! */
/* Not all pointers in conf necessarily point to memory owned by it. */
static void config_release(struct iteflash_config *conf)
{
	null_and_free((void **)&conf->input_filename);
	null_and_free((void **)&conf->output_filename);
	null_and_free((void **)&conf->usb_serial);
	null_and_free((void **)&conf->i2c_dev_path);
}

/* number of bytes to send consecutively before checking for ACKs */
#define FTDI_TX_BUFFER_LIMIT 32

static inline int i2c_byte_transfer(struct common_hnd *chnd, uint8_t addr,
				    uint8_t *data, int write, int numbytes)
{
	/* If we got a termination signal, stop sending data */
	if (exit_requested)
		return -1;

	return chnd->conf.i2c_if->byte_transfer(chnd, addr, data, write,
						numbytes);
}

static int linux_i2c_byte_transfer(struct common_hnd *chnd, uint8_t addr,
				   uint8_t *data, int write, int numbytes)
{
	static const int nmsgs = 1;
	int ret, extra_int;
	struct i2c_msg i2cmsg = {};
	struct i2c_rdwr_ioctl_data msgset = {};

	i2cmsg.addr = addr;
	if (!write)
		i2cmsg.flags |= I2C_M_RD;
	i2cmsg.buf = data;
	i2cmsg.len = numbytes;

	msgset.msgs = &i2cmsg;
	msgset.nmsgs = nmsgs;

	ret = ioctl(chnd->i2c_dev_fd, I2C_RDWR, &msgset);
	if (ret < 0) {
		extra_int = errno;
		fprintf(stderr,
			"%s: ioctl() failed with return value %d and "
			"errno %d\n",
			__func__, ret, extra_int);
		if (ret == -1 && extra_int)
			ret = -abs(extra_int);
	} else if (ret < nmsgs) {
		fprintf(stderr, "%s: failed to send %d of %d I2C messages\n",
			__func__, (nmsgs - ret), nmsgs);
		ret = -1;
	} else {
		ret = 0;
	}
	return ret;
}

static int i2c_add_send_byte(struct ftdi_context *ftdi, uint8_t *buf,
			     uint8_t *ptr, uint8_t *tbuf, int tcnt, int debug)
{
	int ret, i, j, remaining_data, ack_idx;
	int tx_buffered = 0;
	static uint8_t ack[FTDI_TX_BUFFER_LIMIT];
	uint8_t *b = ptr;
	uint8_t failed_ack = 0;

	for (i = 0; i < tcnt; i++) {
		/* WORKAROUND: force SDA before sending the next byte */
		*b++ = SET_BITS_LOW;
		*b++ = SDA_BIT;
		*b++ = SCL_BIT | SDA_BIT;
		/* write byte */
		*b++ = MPSSE_DO_WRITE | MPSSE_BITMODE | MPSSE_WRITE_NEG;
		*b++ = 0x07;
		*b++ = *tbuf++;
		/* prepare for ACK */
		*b++ = SET_BITS_LOW;
		*b++ = 0;
		*b++ = SCL_BIT;
		/* read ACK */
		*b++ = MPSSE_DO_READ | MPSSE_BITMODE | MPSSE_LSB;
		*b++ = 0;
		*b++ = SEND_IMMEDIATE;

		tx_buffered++;

		/*
		 * On the last byte, or every FTDI_TX_BUFFER_LIMIT bytes, read
		 * the ACK bits.
		 */
		if (i == tcnt - 1 || (tx_buffered == FTDI_TX_BUFFER_LIMIT)) {
			/* write data */
			ret = ftdi_write_data(ftdi, buf, b - buf);
			if (ret < 0) {
				fprintf(stderr, "failed to write byte\n");
				return ret;
			}

			/* read ACK bits */
			remaining_data = tx_buffered;
			ack_idx = 0;
			do {
				ret = ftdi_read_data(ftdi, &ack[ack_idx],
						     remaining_data);
				if (ret < 0) {
					fprintf(stderr, "read ACK failed\n");
					return ret;
				}
				remaining_data -= ret;
				ack_idx += ret;
			} while (remaining_data);
			for (j = 0; j < tx_buffered; j++) {
				if ((ack[j] & 0x80) != 0)
					failed_ack = ack[j];
			}

			/* check ACK bits */
			if (ret < 0 || failed_ack) {
				if (debug)
					fprintf(stderr,
						"write ACK fail: %d, 0x%02x\n",
						ret, failed_ack);
				return -ENXIO;
			}

			/* reset for next set of transactions */
			b = ptr;
			tx_buffered = 0;
		}
	}
	return 0;
}

static int i2c_add_recv_bytes(struct ftdi_context *ftdi, uint8_t *buf,
			      uint8_t *ptr, uint8_t *rbuf, int rcnt)
{
	int ret, i, rbuf_idx;
	uint8_t *b = ptr;

	for (i = 0; i < rcnt; i++) {
		/* set SCL low */
		*b++ = SET_BITS_LOW;
		*b++ = 0;
		*b++ = SCL_BIT;
		/* read the byte on the wire */
		*b++ = MPSSE_DO_READ;
		*b++ = 0;
		*b++ = 0;

		if (i == rcnt - 1) {
			/* NACK last byte */
			*b++ = SET_BITS_LOW;
			*b++ = 0;
			*b++ = SCL_BIT;
			*b++ = MPSSE_DO_WRITE | MPSSE_BITMODE | MPSSE_WRITE_NEG;
			*b++ = 0;
			*b++ = 0xff;
			*b++ = SEND_IMMEDIATE;
		} else {
			/* ACK all other bytes */
			*b++ = SET_BITS_LOW;
			*b++ = 0;
			*b++ = SCL_BIT | SDA_BIT;
			*b++ = MPSSE_DO_WRITE | MPSSE_BITMODE | MPSSE_WRITE_NEG;
			*b++ = 0;
			*b++ = 0;
			*b++ = SEND_IMMEDIATE;
		}
	}

	ret = ftdi_write_data(ftdi, buf, b - buf);
	if (ret < 0) {
		fprintf(stderr, "failed to prepare read\n");
		return ret;
	}

	rbuf_idx = 0;
	do {
		ret = ftdi_read_data(ftdi, &rbuf[rbuf_idx], rcnt);
		if (ret < 0) {
			fprintf(stderr, "read byte failed\n");
			break;
		}
		rcnt -= ret;
		rbuf_idx += ret;
	} while (rcnt);

	return ret;
}

#define USB_I2C_HEADER_SIZE 4
static int ccd_i2c_byte_transfer(struct common_hnd *chnd, uint8_t addr,
				 uint8_t *data, int write, int numbytes)
{
	size_t usb_buffer_size = USB_I2C_HEADER_SIZE + numbytes +
				 (((!write * numbytes) > 0x7f) ? 2 : 0);
	std::unique_ptr<uint8_t[]> usb_buffer_ptr =
		std::make_unique<uint8_t[]>(usb_buffer_size);
	uint8_t *usb_buffer = usb_buffer_ptr.get();
	size_t response_size;
	size_t extra = 0;

	/*
	 * Build a message following format described in ./include/usb_i2c.h.
	 *
	 * Hardcode port, the lowest 4 bits of the first byte, to 0; may need
	 * to make this a command line option.
	 */
	usb_buffer[0] = 0;

	usb_buffer[1] = addr;
	if (write) {
		/*
		 * Write count might spill over into the top 4 bits of the
		 * first byte. We trust the caller not to pass numbytes
		 * exceeding (2^12 - 1).
		 */
		if (numbytes > 255)
			usb_buffer[0] |= (numbytes >> 4) & 0xf0;
		usb_buffer[2] = numbytes & 0xff;
		usb_buffer[3] = 0;
		memcpy(usb_buffer + USB_I2C_HEADER_SIZE, data, numbytes);
	} else {
		usb_buffer[2] = 0;
		if (numbytes < 0x80) {
			usb_buffer[3] = numbytes;
		} else {
			usb_buffer[3] = (numbytes & 0x7f) | 0x80;
			usb_buffer[4] = numbytes >> 7;
			usb_buffer[5] = 0;
			extra = 2;
		}
	}

	response_size = 0;
	usb_trx(&chnd->uep, usb_buffer,
		write ? sizeof(usb_buffer) : USB_I2C_HEADER_SIZE + extra,
		usb_buffer, sizeof(usb_buffer), 1, &response_size);

	if (response_size < (USB_I2C_HEADER_SIZE + (write ? 0 : numbytes))) {
		fprintf(stderr, "%s: got too few bytes (%zd) in response\n",
			__func__, response_size);
		return -1;
	}

	if (usb_buffer[0]) {
		uint32_t rv;

		/*
		 * Error is reported as a 16 bit value in little endian byte
		 * order.
		 */
		rv = usb_buffer[1];
		rv = (rv << 8) + usb_buffer[0];

		fprintf(stderr, "%s: usb i2c error %d\n", __func__,
			(((uint16_t)usb_buffer[1]) << 8) + usb_buffer[0]);

		return -rv;
	}

	if (!write)
		memcpy(data, usb_buffer + USB_I2C_HEADER_SIZE, numbytes);

	return 0;
}

static int ftdi_i2c_byte_transfer(struct common_hnd *chnd, uint8_t addr,
				  uint8_t *data, int write, int numbytes)
{
	int ret, rets;
	static uint8_t buf[FTDI_CMD_BUF_SIZE];
	uint8_t *b;
	uint8_t slave_addr;
	struct ftdi_context *ftdi;

	ret = 0;
	b = buf;
	ftdi = chnd->ftdi_hnd;

	/* START condition */
	/* SCL & SDA high */
	*b++ = SET_BITS_LOW;
	*b++ = 0;
	*b++ = 0;
	*b++ = SET_BITS_LOW;
	*b++ = 0;
	*b++ = 0;
	/* SCL high, SDA low */
	*b++ = SET_BITS_LOW;
	*b++ = 0;
	*b++ = SDA_BIT;
	*b++ = SET_BITS_LOW;
	*b++ = 0;
	*b++ = SDA_BIT;
	/* SCL low, SDA low */
	*b++ = SET_BITS_LOW;
	*b++ = 0;
	*b++ = SCL_BIT | SDA_BIT;
	*b++ = SET_BITS_LOW;
	*b++ = 0;
	*b++ = SCL_BIT | SDA_BIT;

	/* send address */
	slave_addr = (addr << 1) | (write ? 0 : 1);
	ret = i2c_add_send_byte(ftdi, buf, b, &slave_addr, 1, chnd->conf.debug);
	if (ret < 0) {
		if (chnd->conf.debug)
			fprintf(stderr, "address %02x failed\n", addr);
		ret = -ENXIO;
		goto exit_xfer;
	}

	b = buf;
	if (write) /* write data */
		ret = i2c_add_send_byte(ftdi, buf, b, data, numbytes,
					chnd->conf.debug);
	else /* read data */
		ret = i2c_add_recv_bytes(ftdi, buf, b, data, numbytes);

exit_xfer:
	b = buf;
	/* STOP condition */
	/* SCL high, SDA low */
	*b++ = SET_BITS_LOW;
	*b++ = 0;
	*b++ = SDA_BIT;
	*b++ = SET_BITS_LOW;
	*b++ = 0;
	*b++ = SDA_BIT;
	/* SCL high, SDA high */
	*b++ = SET_BITS_LOW;
	*b++ = 0;
	*b++ = 0;
	*b++ = SET_BITS_LOW;
	*b++ = 0;
	*b++ = 0;

	rets = ftdi_write_data(ftdi, buf, b - buf);
	if (rets < 0)
		fprintf(stderr, "failed to send STOP\n");
	return ret;
}

static int i2c_write_byte(struct common_hnd *chnd, uint8_t cmd, uint8_t data)
{
	int ret;

	ret = i2c_byte_transfer(chnd, I2C_CMD_ADDR, &cmd, 1, 1);
	if (ret < 0)
		return -EIO;
	ret = i2c_byte_transfer(chnd, I2C_DATA_ADDR, &data, 1, 1);
	if (ret < 0)
		return -EIO;

	return 0;
}

static int i2c_read_byte(struct common_hnd *chnd, uint8_t cmd, uint8_t *data)
{
	int ret;

	ret = i2c_byte_transfer(chnd, I2C_CMD_ADDR, &cmd, 1, 1);
	if (ret < 0)
		return -EIO;
	ret = i2c_byte_transfer(chnd, I2C_DATA_ADDR, data, 0, 1);
	if (ret < 0)
		return -EIO;

	return 0;
}

/* Configure I2C MUX to choose EC Prog channel */
static int config_i2c_mux(struct common_hnd *chnd, uint8_t cmd)
{
	int ret;

	ret = i2c_byte_transfer(chnd, I2C_MUX_CMD_ADDR, &cmd, 1, 1);
	if (ret < 0) {
		fprintf(stderr, "Failed to configure I2C MUX.");
		return -EIO;
	}

	return 0;
}

/* Get 3rd Byte Chip ID */
static int get_3rd_chip_id_byte(struct common_hnd *chnd, uint8_t *chip_id)
{
	int ret = 0;

	ret = i2c_write_byte(chnd, 0x80, 0xf0);
	ret |= i2c_write_byte(chnd, 0x2f, 0x20);
	ret |= i2c_write_byte(chnd, 0x2e, 0x85);
	ret |= i2c_read_byte(chnd, 0x30, chip_id);

	if (ret < 0)
		fprintf(stderr, "Failed to get id of 3rd byte.");

	return ret;
}

static int check_flashid(struct common_hnd *chnd)
{
	int ret = 0;
	uint8_t id[16], i;
	struct cmds commands[] = { { 0x07, 0x7f }, { 0x06, 0xff },
				   { 0x04, 0x00 }, { 0x05, 0xfe },
				   { 0x08, 0x00 }, { 0x05, 0xfd },
				   { 0x08, 0x9f } };

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		ret = i2c_write_byte(chnd, commands[i].addr, commands[i].cmd);
		if (ret) {
			fprintf(stderr, "Flash ID Failed : cmd %x ,data %x\n",
				commands[i].addr, commands[i].cmd);
			return ret;
		}
	}

	ret = i2c_byte_transfer(chnd, I2C_DATA_ADDR, id, 0, 16);

	if (ret < 0)
		fprintf(stderr, "Check Flash ID FAILED\n");

	if ((id[0] == 0xFF) && (id[1] == 0xFF) && (id[2] == 0xFE)) {
		printf("EFLASH TYPE = 8315\n\r");
		eflash_type = EFLASH_TYPE_8315;
	} else if ((id[0] == 0xC8) || (id[0] == 0xEF)) {
		printf("EFLASH TYPE = KGD\n\r");
		eflash_type = EFLASH_TYPE_KGD;
	} else {
		printf("Invalid EFLASH TYPE : ");
		printf("FLASH ID = %02x %02x %02x\n\r", id[0], id[1], id[2]);
		eflash_type = EFLASH_TYPE_NONE;
		ret = -EINVAL;
	}

	return ret;
}

/* Fills in chnd->flash_size */
static int check_chipid(struct common_hnd *chnd)
{
	int ret;
	uint8_t ver = 0xff;
	uint32_t id = 0xffff;
	uint16_t v2[7] = { 128, 192, 256, 384, 512, 0, 1024 };
	/*
	 * Chip Version is mapping from bit 3-0
	 * Flash size is mapping from bit 7-4
	 *
	 * Chip Version (bit 3-0)
	 * 0: AX
	 * 1: BX
	 * 2: CX
	 * 3: DX
	 *
	 * CX before flash size (bit 7-4)
	 * 0:128KB
	 * 4:192KB
	 * 8:256KB
	 *
	 * DX flash size(bit 7-4)
	 * 0:128KB
	 * 2:192KB
	 * 4:256KB
	 * 6:384KB
	 * 8:512KB
	 *
	 * flash size(bit 7-4) of it8xxx1 or it8xxx2 series
	 * 0:128KB
	 * 4:256KB
	 * 8:512KB
	 * C:1024KB
	 */

	ret = i2c_read_byte(chnd, 0x00, (uint8_t *)&id + 1);
	if (ret < 0)
		return ret;
	ret = i2c_read_byte(chnd, 0x01, (uint8_t *)&id);
	if (ret < 0)
		return ret;
	ret = i2c_read_byte(chnd, 0x02, &ver);
	if (ret < 0)
		return ret;

	if ((id & 0xff00) != (CHIP_ID & 0xff00)) {
		id |= 0xff0000;
		ret = get_3rd_chip_id_byte(chnd, (uint8_t *)&id + 2);
		if (ret < 0)
			return ret;

		if ((id & 0xf000f) == 0x80001 || (id & 0xf000f) == 0x80002) {
			chnd->flash_cmd_v2 = 1;
			chnd->dbgr_addr_3bytes = 1;
		} else {
			fprintf(stderr, "Invalid chip id: %05x\n", id);
			return -EINVAL;
		}
	} else {
		chnd->dbgr_addr_3bytes = 0;
		if ((ver & 0x0f) >= 0x03)
			chnd->flash_cmd_v2 = 1;
		else
			chnd->flash_cmd_v2 = 0;
	}
	/* compute embedded flash size from CHIPVER field */
	if (chnd->flash_cmd_v2)
		chnd->flash_size = v2[(ver & 0xF0) >> 5] * 1024;
	else
		chnd->flash_size = (128 + (ver & 0xF0)) * 1024;

	if (chnd->flash_size == 0) {
		fprintf(stderr, "Invalid Flash Size");
		return -EINVAL;
	}

	printf("CHIPID %05x, CHIPVER %02x, Flash size %d kB\n", id, ver,
	       chnd->flash_size / 1024);

	return 0;
}

/* Exit DBGR mode */
static int exit_dbgr_mode(struct common_hnd *chnd)
{
	int ret = 0;

	printf("Exit DBGR mode...\n");
	if (chnd->dbgr_addr_3bytes)
		ret |= i2c_write_byte(chnd, 0x80, 0xf0);
	ret |= i2c_write_byte(chnd, 0x2f, 0x1c);
	ret |= i2c_write_byte(chnd, 0x2e, 0x08);
	ret |= i2c_write_byte(chnd, 0x30, BIT(4));

	if (ret < 0)
		fprintf(stderr, "EXIT DBGR MODE FAILED\n");

	return 0;
}

/* DBGR reset GPIOs to default */
static int dbgr_reset_gpio(struct common_hnd *chnd)
{
	int ret = 0;

	printf("Reset GPIOs to default.\n");
	if (chnd->dbgr_addr_3bytes)
		ret |= i2c_write_byte(chnd, 0x80, 0xf0);
	ret |= i2c_write_byte(chnd, 0x2f, 0x20);
	ret |= i2c_write_byte(chnd, 0x2e, 0x07);
	ret |= i2c_write_byte(chnd, 0x30, BIT(1));

	if (ret < 0)
		fprintf(stderr, "DBGR RESET GPIO FAILED\n");

	return 0;
}

/* disable watchdog */
static int dbgr_disable_watchdog(struct common_hnd *chnd)
{
	int ret = 0;

	printf("Disabling watchdog...\n");
	if (chnd->dbgr_addr_3bytes)
		ret |= i2c_write_byte(chnd, 0x80, 0xf0);

	ret |= i2c_write_byte(chnd, 0x2f, 0x1f);
	ret |= i2c_write_byte(chnd, 0x2e, 0x05);
	ret |= i2c_write_byte(chnd, 0x30, 0x30);

	if (ret < 0)
		fprintf(stderr, "DBGR DISABLE WATCHDOG FAILED!\n");

	return ret;
}

/* disable protect path from DBGR */
static int dbgr_disable_protect_path(struct common_hnd *chnd)
{
	int ret = 0, i;

	printf("Disabling protect path...\n");

	if (chnd->dbgr_addr_3bytes)
		ret |= i2c_write_byte(chnd, 0x80, 0xf0);

	ret |= i2c_write_byte(chnd, 0x2f, 0x20);
	for (i = 0; i < 32; i++) {
		ret |= i2c_write_byte(chnd, 0x2e, 0xa0 + i);
		ret |= i2c_write_byte(chnd, 0x30, 0);
	}

	if (ret < 0)
		fprintf(stderr, "DISABLE PROTECT PATH FROM DBGR FAILED!\n");

	return ret;
}

/* Enter follow mode and FSCE# high level */
static int spi_flash_follow_mode(struct common_hnd *chnd, const char *desc)
{
	int ret = 0;

	ret |= i2c_write_byte(chnd, 0x07, 0x7f);
	ret |= i2c_write_byte(chnd, 0x06, 0xff);
	ret |= i2c_write_byte(chnd, 0x05, 0xfe);
	ret |= i2c_write_byte(chnd, 0x04, 0x00);
	ret |= i2c_write_byte(chnd, 0x08, 0x00);

	ret = (ret ? -EIO : 0);
	if (ret < 0)
		fprintf(stderr, "Flash %s enter follow mode FAILED (%d)\n",
			desc, ret);

	return ret;
}

/* Exit follow mode */
static int spi_flash_follow_mode_exit(struct common_hnd *chnd, const char *desc)
{
	int ret = 0;

	ret |= i2c_write_byte(chnd, 0x07, 0x00);
	ret |= i2c_write_byte(chnd, 0x06, 0x00);

	ret = (ret ? -EIO : 0);
	if (ret < 0)
		fprintf(stderr, "Flash %s exit follow mode FAILED (%d)\n", desc,
			ret);

	return ret;
}

/* Stop EC by sending follow mode command */
static int dbgr_stop_ec(struct common_hnd *chnd)
{
	int ret = 0;

	ret |= spi_flash_follow_mode(chnd, "enter follow mode");
	ret |= spi_flash_follow_mode_exit(chnd, "exit follow mode");

	if (ret < 0)
		fprintf(stderr, "DBGR STOP EC FAILED!\n");

	return ret;
}

/* SPI Flash generic command, short version */
static int spi_flash_command_short(struct common_hnd *chnd, uint8_t cmd,
				   const char *desc)
{
	int ret = 0;

	ret |= i2c_write_byte(chnd, 0x05, 0xfe);
	ret |= i2c_write_byte(chnd, 0x08, 0x00);
	ret |= i2c_write_byte(chnd, 0x05, 0xfd);
	ret |= i2c_write_byte(chnd, 0x08, cmd);

	ret = (ret ? -EIO : 0);
	if (ret < 0)
		fprintf(stderr, "Flash CMD %s FAILED (%d)\n", desc, ret);

	return ret;
}

/* SPI Flash set erase page */
static int spi_flash_set_erase_page(struct common_hnd *chnd, int page,
				    const char *desc)
{
	int ret = 0;

	ret |= i2c_write_byte(chnd, 0x08, page >> 8);
	ret |= i2c_write_byte(chnd, 0x08, page & 0xff);
	ret |= i2c_write_byte(chnd, 0x08, 0);

	ret = (ret ? -EIO : 0);
	if (ret < 0)
		fprintf(stderr, "Flash %s set page FAILED (%d)\n", desc, ret);

	return ret;
}

/* Poll SPI Flash Read Status register until BUSY is reset */
static int spi_poll_busy(struct common_hnd *chnd, const char *desc)
{
	uint8_t reg = 0xff;
	int ret = -EIO;

	if (spi_flash_command_short(chnd, SPI_CMD_READ_STATUS,
				    "read status for busy bit") < 0) {
		fprintf(stderr, "Flash %s wait busy cleared FAILED\n", desc);
		goto failed_read_status;
	}

	while (1) {
		if (i2c_byte_transfer(chnd, I2C_DATA_ADDR, &reg, 0, 1) < 0) {
			fprintf(stderr, "Flash polling busy cleared FAILED\n");
			break;
		}

		if ((reg & 0x01) == 0) {
			/* busy bit cleared */
			ret = 0;
			break;
		}
	}
failed_read_status:
	return ret;
}

static int spi_check_write_enable(struct common_hnd *chnd, const char *desc)
{
	uint8_t reg = 0xff;
	int ret = -EIO;

	if (spi_flash_command_short(chnd, SPI_CMD_READ_STATUS,
				    "read status for write enable bit") < 0) {
		fprintf(stderr, "Flash %s wait WE FAILED\n", desc);
		goto failed_read_status;
	}

	while (1) {
		if (i2c_byte_transfer(chnd, I2C_DATA_ADDR, &reg, 0, 1) < 0) {
			fprintf(stderr, "Flash polling WE FAILED\n");
			break;
		}

		if ((reg & 0x03) == 2) {
			/* busy bit cleared and WE bit set */
			ret = 0;
			break;
		}
	}
failed_read_status:
	return ret;
}

static int ftdi_config_i2c(struct ftdi_context *ftdi)
{
	int ret;
	static const uint16_t divisor =
		60000000 / (2 * FTDI_I2C_FREQ * 3 / 2 /* 3-phase CLK */) - 1;
	uint8_t clock_buf[] = { EN_3_PHASE, DIS_DIV_5, TCK_DIVISOR,
				divisor & 0xff, divisor >> 8 };

	ret = ftdi_set_latency_timer(ftdi, 16 /* ms */);
	if (ret < 0)
		fprintf(stderr, "Cannot set latency\n");

	ret = ftdi_set_bitmode(ftdi, 0, BITMODE_RESET);
	if (ret < 0) {
		fprintf(stderr, "Cannot reset MPSSE\n");
		return -EIO;
	}
	ret = ftdi_set_bitmode(ftdi, 0, BITMODE_MPSSE);
	if (ret < 0) {
		fprintf(stderr, "Cannot enable MPSSE\n");
		return -EIO;
	}

	ret = ftdi_usb_purge_buffers(ftdi);
	if (ret < 0)
		fprintf(stderr, "Cannot purge buffers\n");

	/* configure the clock */
	ret = ftdi_write_data(ftdi, clock_buf, sizeof(clock_buf));
	if (ret < 0)
		return ret;

	return 0;
}

/* Special waveform definition */
#define SPECIAL_LEN_USEC 50000ULL /* us */
#define SPECIAL_FREQ 400000ULL

#define SPECIAL_PATTERN 0x0000020301010302ULL
#define SPECIAL_PATTERN_SDA_L_SCL_L 0x0000000000000000ULL
#define SPECIAL_PATTERN_SDA_H_SCL_L 0x0202020202020202ULL
#define SPECIAL_PATTERN_SDA_L_SCL_H 0x0101010101010101ULL
#define SPECIAL_PATTERN_SDA_H_SCL_H 0x0303030303030303ULL
#define TICK_COUNT 24

#define MSEC 1000
#define USEC 1000000

#define SPECIAL_BUFFER_SIZE \
	(((SPECIAL_LEN_USEC * SPECIAL_FREQ * 2 / USEC) + 7) & ~7)

static int connect_to_ccd_i2c_bridge(struct common_hnd *chnd)
{
	int rv;

	rv = usb_findit(chnd->conf.usb_serial, chnd->conf.usb_vid,
			chnd->conf.usb_pid, CR50_I2C_SUBCLASS,
			CR50_I2C_PROTOCOL, &chnd->uep);

	if (rv) {
		fprintf(stderr, "%s: usb_findit returned error %d\n", __func__,
			rv);
	}

	return rv;
}

static int ccd_trigger_special_waveform(struct common_hnd *chnd)
{
	uint8_t response[20];
	size_t rsize;
	uint8_t req[] = { 0, /* Port 0. Might be necessary to modify. */
			  CROS_CMD_ADDR, /* Chrome OS dedicated address. */
			  1, /* Will send a single byte command. */
			  0, /* No need to read back anything. */
			  CROS_CMD_ITE_SYNC };

	usb_trx(&chnd->uep, req, sizeof(req), response, sizeof(response), 1,
		&rsize);

	if (rsize < USB_I2C_HEADER_SIZE)
		return -1;

	if (response[0])
		return -response[0];
	/*
	 * The target is about to get reset, let's shut down the USB
	 * connection.
	 */
	usb_shut_down(&chnd->uep);

	sleep(3);

	return connect_to_ccd_i2c_bridge(chnd);
}

static int ftdi_send_special_waveform(struct common_hnd *chnd)
{
	int ret;
	int i;
	uint64_t *wave;
	struct ftdi_context *ftdi = chnd->ftdi_hnd;
	uint8_t release_lines[] = { SET_BITS_LOW, 0, 0 };

	wave = (uint64_t *)malloc(SPECIAL_BUFFER_SIZE);
	if (!wave) {
		fprintf(stderr, "malloc(%zu) failed\n",
			(size_t)SPECIAL_BUFFER_SIZE);
		return -1;
	}

	/* Reset the FTDI into a known state */
	ret = ftdi_set_bitmode(ftdi, 0xFF, BITMODE_RESET);
	if (ret) {
		fprintf(stderr, "failed to reset FTDI\n");
		goto free_and_return;
	}

	/*
	 * set the clock divider, so we output a new bitbang value every
	 * 2.5us.
	 */
	ret = ftdi_set_baudrate(ftdi, 160000);
	if (ret) {
		fprintf(stderr, "failed to set bitbang clock\n");
		goto free_and_return;
	}

	/* Enable asynchronous bit-bang mode */
	ret = ftdi_set_bitmode(ftdi, 0xFF, BITMODE_BITBANG);
	if (ret) {
		fprintf(stderr, "failed to set bitbang mode\n");
		goto free_and_return;
	}

	/* do usb special waveform */
	wave[0] = 0x0;
	ftdi_write_data(ftdi, (uint8_t *)wave, 1);
	usleep(5000);

	/* program each special tick */
	for (i = 0; i < TICK_COUNT;) {
		wave[i++] = SPECIAL_PATTERN_SDA_L_SCL_L;
		wave[i++] = SPECIAL_PATTERN_SDA_H_SCL_L;
		wave[i++] = SPECIAL_PATTERN_SDA_L_SCL_L;
	}
	wave[19] = SPECIAL_PATTERN_SDA_H_SCL_H;

	/* fill the buffer with the waveform pattern */
	for (i = TICK_COUNT; i < SPECIAL_BUFFER_SIZE / sizeof(uint64_t); i++)
		wave[i] = SPECIAL_PATTERN;

	ret = ftdi_write_data(ftdi, (uint8_t *)wave, SPECIAL_BUFFER_SIZE);
	if (ret < 0)
		fprintf(stderr, "Cannot output special waveform\n");
	else
		/* no error */
		ret = 0;

	/* clean everything to go back to regular I2C communication */
	ftdi_usb_purge_buffers(ftdi);
	ftdi_set_bitmode(ftdi, 0xff, BITMODE_RESET);
	ftdi_config_i2c(ftdi);
	ftdi_write_data(ftdi, release_lines, sizeof(release_lines));

free_and_return:
	free(wave);
	return ret;
}

static int send_special_waveform(struct common_hnd *chnd)
{
	const int max_iterations = 10;
	int ret;
	int iterations;

	if (!chnd->conf.i2c_if->send_special_waveform) {
		fprintf(stderr,
			"This binary does not support sending the ITE "
			"special waveform with the chosen I2C interface.\n");
		return -1;
	}

	iterations = 0;

	do {
		ret = chnd->conf.i2c_if->send_special_waveform(chnd);
		if (ret)
			break;

		/* wait for PLL stable for 5ms (plus remaining USB transfers) */
		usleep(10 * MSEC);

		/* Stop EC ASAP after sending special waveform. */
		if (dbgr_stop_ec(chnd) >= 0) {
			/*
			 * If we can talk to chip, then we can break the retry
			 * loop.
			 */
			ret = check_chipid(chnd);
		} else {
			ret = -1;
			if (!(iterations % max_iterations))
				fprintf(stderr, "!please reset EC if flashing"
						" sequence is not starting!\n");
		}
	} while (ret && (iterations++ < max_iterations));

	if (ret)
		fprintf(stderr, "Failed to send special waveform!\n");
	else
		printf("Done with sending special waveform.\n");

	return ret;
}

static int windex;
static const char wheel[] = { '|', '/', '-', '\\' };
static void draw_spinner(uint32_t remaining, uint32_t size)
{
	int percent = (size - remaining) * 100 / size;
	fprintf(stderr, "\r%c%3d%%", wheel[windex++], percent);
	windex %= sizeof(wheel);
}

/* Note: this function must be called in follow mode */
static int spi_send_cmd_fast_read(struct common_hnd *chnd, uint32_t addr)
{
	int ret = 0;
	uint8_t cmd = 0x9;

	/* Fast Read command */
	ret = spi_flash_command_short(chnd, SPI_CMD_FAST_READ, "fast read");
	/* Send address */
	ret |= i2c_write_byte(chnd, 0x08, ((addr >> 16) & 0xff)); /* addr_h */
	ret |= i2c_write_byte(chnd, 0x08, ((addr >> 8) & 0xff)); /* addr_m */
	ret |= i2c_write_byte(chnd, 0x08, (addr & 0xff)); /* addr_l */
	/* fake byte */
	ret |= i2c_write_byte(chnd, 0x08, 0x00);
	/* use i2c block read command */
	ret |= i2c_byte_transfer(chnd, I2C_CMD_ADDR, &cmd, 1, 1);
	if (ret < 0)
		fprintf(stderr, "Send fast read command failed\n");

	return ret;
}

static int command_read_pages(struct common_hnd *chnd, uint32_t address,
			      uint32_t size, uint8_t *buffer)
{
	int res = -EIO;
	uint32_t remaining = size;
	int cnt;

	if (address & 0xFF) {
		fprintf(stderr,
			"page read requested at non-page boundary: "
			"0x%X\n",
			address);
		return -EINVAL;
	}

	if (spi_flash_follow_mode(chnd, "fast read") < 0)
		goto failed_read;

	if (spi_send_cmd_fast_read(chnd, address) < 0)
		goto failed_read;

	while (remaining) {
		cnt = (remaining > PAGE_SIZE) ? PAGE_SIZE : remaining;
		draw_spinner(remaining, size);

		/* read page data */
		res = i2c_byte_transfer(chnd, I2C_BLOCK_ADDR, buffer, 0, cnt);
		if (res < 0) {
			fprintf(stderr, "page data read failed\n");
			goto failed_read;
		}

		address += cnt;
		remaining -= cnt;
		buffer += cnt;

		/* We need to resend fast read command at 256KB boundary. */
		if (!(address % 0x40000) && remaining) {
			if (spi_send_cmd_fast_read(chnd, address) < 0)
				goto failed_read;
		}
	}
	/* No error so far */
	res = size;
failed_read:
	if (spi_flash_follow_mode_exit(chnd, "fast read") < 0)
		res = -EIO;

	return res;
}

static bool is_empty_page(uint8_t *buffer, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (buffer[i] != 0xFF)
			return false;
	}

	return true;
}

static int command_write_pages(struct common_hnd *chnd, uint32_t address,
			       uint32_t size, uint8_t *buffer)
{
	int res = -EIO;
	int block_write_size = chnd->conf.block_write_size;
	uint32_t remaining = size;
	int cnt;
	uint8_t addr_H, addr_M, addr_L;
	uint8_t data;

	if (spi_flash_follow_mode(chnd, "AAI write") < 0)
		goto failed_write;

	while (remaining) {
		cnt = (remaining > block_write_size) ? block_write_size :
						       remaining;
		addr_H = (address >> 16) & 0xFF;
		addr_M = (address >> 8) & 0xFF;
		addr_L = address & 0xFF;

		draw_spinner(remaining, size);

		/* Write enable */
		if (spi_flash_command_short(chnd, SPI_CMD_WRITE_ENABLE,
					    "write enable for AAI write") < 0)
			goto failed_write;

		/* Check write enable bit */
		if (spi_check_write_enable(chnd, "AAI write") < 0)
			goto failed_write;

		/* Setup write */
		if (spi_flash_command_short(chnd, SPI_CMD_WORD_PROGRAM,
					    "AAI write") < 0)
			goto failed_write;

		/* Set eflash page address */
		res = i2c_byte_transfer(chnd, I2C_DATA_ADDR, &addr_H, 1, 1);
		res |= i2c_byte_transfer(chnd, I2C_DATA_ADDR, &addr_M, 1, 1);
		res |= i2c_byte_transfer(chnd, I2C_DATA_ADDR, &addr_L, 1, 1);
		if (res < 0) {
			fprintf(stderr, "Flash write set page FAILED (%d)\n",
				res);
			goto failed_write;
		}

		/* Wait until not busy */
		if (spi_poll_busy(chnd, "AAI write") < 0)
			goto failed_write;

		/* Write up to block_write_size data */
		res = i2c_write_byte(chnd, 0x10, 0x20);
		res = i2c_byte_transfer(chnd, I2C_BLOCK_ADDR, buffer, 1, cnt);
		buffer += cnt;

		if (res < 0) {
			fprintf(stderr, "Flash data write failed\n");
			goto failed_write;
		}

		data = 0xFF;
		res = i2c_byte_transfer(chnd, I2C_DATA_ADDR, &data, 1, 1);
		res |= i2c_write_byte(chnd, 0x10, 0x00);
		if (res < 0) {
			fprintf(stderr, "Flash end data write FAILED (%d)\n",
				res);
			goto failed_write;
		}

		/* Write disable */
		if (spi_flash_command_short(chnd, SPI_CMD_WRITE_DISABLE,
					    "write disable for AAI write") < 0)
			goto failed_write;

		/* Wait until available */
		if (spi_poll_busy(chnd, "write disable for AAI write") < 0)
			goto failed_write;

		address += cnt;
		remaining -= cnt;
	}
	draw_spinner(remaining, size);
	/* No error so far */
	res = size;
failed_write:
	if (spi_flash_command_short(chnd, SPI_CMD_WRITE_DISABLE,
				    "write disable exit AAI write") < 0)
		res = -EIO;

	if (spi_flash_follow_mode_exit(chnd, "AAI write") < 0)
		res = -EIO;

	return res;
}

/*
 * Test for spi page program command
 */
static int command_write_pages3(struct common_hnd *chnd, uint32_t address,
				uint32_t size, uint8_t *buffer)
{
	int ret = 0;
	uint8_t addr_H, addr_M, addr_L;

	/* SMB_SPI_Flash_Write_Enable */
	if (spi_flash_command_short(chnd, SPI_CMD_WRITE_ENABLE,
				    "SPI Command Write Enable") < 0) {
		ret = -EIO;
		goto failed_write;
	}

	if (spi_flash_command_short(chnd, SPI_CMD_PAGE_PROGRAM,
				    "SPI_CMD_PAGE_PROGRAM") < 0) {
		ret = -EIO;
		goto failed_write;
	}

	addr_H = (address >> 16) & 0xFF;
	addr_M = (address >> 8) & 0xFF;
	addr_L = address & 0xFF;

	ret = i2c_byte_transfer(chnd, I2C_DATA_ADDR, &addr_H, 1, 1);
	ret |= i2c_byte_transfer(chnd, I2C_DATA_ADDR, &addr_M, 1, 1);
	ret |= i2c_byte_transfer(chnd, I2C_DATA_ADDR, &addr_L, 1, 1);
	ret |= i2c_byte_transfer(chnd, I2C_BLOCK_ADDR, buffer, 1, size);
	if (ret < 0)
		goto failed_write;

	/* Wait until not busy */
	if (spi_poll_busy(chnd, "Page Program") < 0)
		ret = -EIO;

	/* No error so far */
failed_write:
	return ret;
}

static int command_erase(struct common_hnd *chnd, uint32_t len, uint32_t off)
{
	int res = -EIO;
	int page = 0;
	uint32_t remaining = len;

	printf("Erasing chip...\n");

	if (off != 0 || len != chnd->flash_size) {
		fprintf(stderr, "Only full chip erase is supported\n");
		return -EINVAL;
	}

	if (spi_flash_follow_mode(chnd, "erase") < 0)
		goto failed_erase;

	while (remaining) {
		draw_spinner(remaining, len);

		if (spi_flash_command_short(chnd, SPI_CMD_WRITE_ENABLE,
					    "write enable for erase") < 0)
			goto failed_erase;

		if (spi_check_write_enable(chnd, "erase") < 0)
			goto failed_erase;

		/* do chip erase */
		if (remaining == chnd->flash_size) {
			if (spi_flash_command_short(chnd, SPI_CMD_CHIP_ERASE,
						    "chip erase") < 0)
				goto failed_erase;
			goto wait_busy_cleared;
		}

		/* do sector erase */
		if (spi_flash_command_short(chnd, spi_cmd_sector_erase,
					    "sector erase") < 0)
			goto failed_erase;

		if (spi_flash_set_erase_page(chnd, page, "sector erase") < 0)
			goto failed_erase;

	wait_busy_cleared:
		if (spi_poll_busy(chnd, "erase") < 0)
			goto failed_erase;

		if (spi_flash_command_short(chnd, SPI_CMD_WRITE_DISABLE,
					    "write disable for erase") < 0)
			goto failed_erase;

		if (remaining == chnd->flash_size) {
			remaining = 0;
			draw_spinner(remaining, len);
		} else {
			page += sector_erase_pages;
			remaining -= sector_erase_pages * PAGE_SIZE;
		}
	}

	/* No error so far */
	printf("\n\rErasing Done.\n");
	res = 0;

failed_erase:
	if (spi_flash_command_short(chnd, SPI_CMD_WRITE_DISABLE,
				    "write disable exit erase") < 0)
		res = -EIO;

	if (spi_flash_follow_mode_exit(chnd, "erase") < 0)
		res = -EIO;

	return res;
}

/*
 * This function can Erase First Sector or Erase All Sector by reset value
 * Some F/W will produce the H/W watchdog reset and it will happen
 * reset issue while flash.
 * Add such function to prevent the reset issue.
 */
static int command_erase2(struct common_hnd *chnd, uint32_t len, uint32_t off,
			  uint32_t reset)
{
	int res = -EIO;
	int page = 0;
	uint32_t remaining = len;

	/*
	 * TODOD(b/<>):
	 * Using sector erase instead of chip erase
	 * For some new chip , the chip erase may not work
	 * well on the original flow
	 */

	printf("Erasing flash...erase size=%d\n", len);

	if (off != 0 || len != chnd->flash_size) {
		fprintf(stderr, "Only full chip erase is supported\n");
		return -EINVAL;
	}

	if (spi_flash_follow_mode(chnd, "erase") < 0)
		goto failed_erase;

	while (remaining) {
		draw_spinner(remaining, len);

		if (spi_flash_command_short(chnd, SPI_CMD_WRITE_ENABLE,
					    "write enable for erase") < 0)
			goto failed_erase;

		if (spi_check_write_enable(chnd, "erase") < 0)
			goto failed_erase;

		/* do sector erase */
		if (spi_flash_command_short(chnd, spi_cmd_sector_erase,
					    "sector erase") < 0)
			goto failed_erase;

		if (spi_flash_set_erase_page(chnd, page, "sector erase") < 0)
			goto failed_erase;

		if (spi_poll_busy(chnd, "erase") < 0)
			goto failed_erase;

		if (spi_flash_command_short(chnd, SPI_CMD_WRITE_DISABLE,
					    "write disable for erase") < 0)
			goto failed_erase;

		if (reset) {
			printf("\n\rreset to prevent the watchdog reset...\n");
			break;
		}

		page += sector_erase_pages;
		remaining -= sector_erase_pages * PAGE_SIZE;
		draw_spinner(remaining, len);
	}

	/* No error so far */
	printf("\n\rErasing Done.\n");
	res = 0;

failed_erase:
	if (spi_flash_command_short(chnd, SPI_CMD_WRITE_DISABLE,
				    "write disable exit erase") < 0)
		res = -EIO;

	if (spi_flash_follow_mode_exit(chnd, "erase") < 0)
		res = -EIO;

	return res;
}

/* Return zero on success, a negative error value on failures. */
static int read_flash(struct common_hnd *chnd)
{
	int res;
	FILE *hnd;
	uint8_t *buffer;
	const char *filename = chnd->conf.input_filename;
	size_t offset = chnd->conf.range_base;
	size_t size;

	if (!offset && !chnd->conf.range_size) {
		size = chnd->flash_size;
	} else {
		/*
		 * Zero conf.range_size means the user did not enter range
		 * size in the command line.
		 */
		if (chnd->conf.range_size)
			size = chnd->conf.range_size;
		else
			size = chnd->flash_size - offset;

		if (!size) {
			fprintf(stderr,
				"Error: not reading a zero sized range!\n");
			return -EINVAL;
		}

		if ((size + offset) > chnd->flash_size) {
			fprintf(stderr,
				"Error: Read range exceeds flash size!\n");
			return -EINVAL;
		}
	}

	buffer = (uint8_t *)malloc(size);
	if (!buffer) {
		fprintf(stderr, "Cannot allocate %zd bytes\n", size);
		return -ENOMEM;
	}

	hnd = fopen(filename, "w");
	if (!hnd) {
		fprintf(stderr, "Cannot open file %s for writing\n", filename);
		free(buffer);
		return -EIO;
	}

	printf("Reading %zd bytes at %#08zx\n", size, offset);
	res = command_read_pages(chnd, offset, size, buffer);
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
static int write_flash(struct common_hnd *chnd, const char *filename,
		       uint32_t offset)
{
	int res, written;
	FILE *hnd;
	int size = chnd->flash_size;
	uint8_t *buffer = (uint8_t *)malloc(size);

	if (!buffer) {
		fprintf(stderr, "%s: Cannot allocate %d bytes\n", __func__,
			size);
		return -ENOMEM;
	}

	hnd = fopen(filename, "r");
	if (!hnd) {
		fprintf(stderr, "%s: Cannot open file %s for reading\n",
			__func__, filename);
		free(buffer);
		return -EIO;
	}
	res = fread(buffer, 1, size, hnd);
	if (res <= 0) {
		fprintf(stderr,
			"%s: Failed to read %d bytes from %s with "
			"ferror() %d\n",
			__func__, size, filename, ferror(hnd));
		free(buffer);
		fclose(hnd);
		return -EIO;
	}
	fclose(hnd);

	printf("Writing %d bytes at 0x%08x\n", res, offset);
	written = command_write_pages(chnd, offset, res, buffer);
	if (written != res) {
		fprintf(stderr, "%s: Error writing to flash\n", __func__);
		free(buffer);
		return -EIO;
	}
	printf("\n\rWriting Done.\n");

	free(buffer);
	return 0;
}

/*
 * Return zero on success, a negative error value on failures.
 *
 * Change the program command to match the ITE Download
 * The original flow may not work on the DX chip.
 *
 */
static int write_flash2(struct common_hnd *chnd, const char *filename,
			uint32_t offset)
{
	int res;
	int block_write_size = chnd->conf.block_write_size;
	FILE *hnd;
	int size = chnd->flash_size;
	int cnt, two_bytes_sent, ret;
	uint8_t addr_h, addr_m, addr_l, data_ff = 0xff;
	uint8_t *buffer = (uint8_t *)malloc(size);

	if (!buffer) {
		fprintf(stderr, "%s: Cannot allocate %d bytes\n", __func__,
			size);
		return -ENOMEM;
	}

	hnd = fopen(filename, "r");
	if (!hnd) {
		fprintf(stderr, "%s: Cannot open file %s for reading\n",
			__func__, filename);
		free(buffer);
		return -EIO;
	}
	res = fread(buffer, 1, size, hnd);
	if (res <= 0) {
		fprintf(stderr,
			"%s: Failed to read %d bytes from %s with "
			"ferror() %d\n",
			__func__, size, filename, ferror(hnd));
		fclose(hnd);
		free(buffer);
		return -EIO;
	}
	fclose(hnd);

	/* Enter follow mode */
	if (spi_flash_follow_mode(chnd, "AAI write") < 0) {
		ret = -EIO;
		goto failed_enter_mode;
	}

	printf("Writing %d bytes at 0x%08x.......\n", res, offset);

__send_aai_cmd:
	addr_h = (offset >> 16) & 0xff;
	addr_m = (offset >> 8) & 0xff;
	addr_l = offset & 0xff;

	/* write enable command */
	ret = spi_flash_command_short(chnd, SPI_CMD_WRITE_ENABLE, "SPI WE");
	/* AAI command */
	ret |= spi_flash_command_short(chnd, SPI_CMD_WORD_PROGRAM, "SPI AAI");
	/* address of AAI command */
	ret |= i2c_byte_transfer(chnd, I2C_DATA_ADDR, &addr_h, 1, 1);
	ret |= i2c_byte_transfer(chnd, I2C_DATA_ADDR, &addr_m, 1, 1);
	ret |= i2c_byte_transfer(chnd, I2C_DATA_ADDR, &addr_l, 1, 1);
	/* Send first two bytes of buffe */
	ret |= i2c_byte_transfer(chnd, I2C_DATA_ADDR, &buffer[offset], 1, 1);
	ret |= i2c_byte_transfer(chnd, I2C_DATA_ADDR, &buffer[offset + 1], 1,
				 1);
	/* we had sent two bytes */
	offset += 2;
	res -= 2;
	two_bytes_sent = 1;
	/* Wait until not busy */
	if (spi_poll_busy(chnd, "wait busy bit cleared at AAI write ") < 0) {
		ret = -EIO;
		goto failed_write;
	}
	/* enable quick AAI mode */
	ret |= i2c_write_byte(chnd, 0x10, 0x20);
	if (ret < 0)
		goto failed_write;

	while (res) {
		cnt = (res > block_write_size) ? block_write_size : res;
		/* we had sent two bytes */
		if (two_bytes_sent) {
			two_bytes_sent = 0;
			cnt -= 2;
		}
		if (i2c_byte_transfer(chnd, I2C_BLOCK_ADDR, &buffer[offset], 1,
				      cnt) < 0) {
			ret = -EIO;
			goto failed_write;
		}

		res -= cnt;
		offset += cnt;
		draw_spinner(res, res + offset);

		/* We need to resend aai write command at 256KB boundary. */
		if (!(offset % 0x40000) && res) {
			/* disable quick AAI mode */
			i2c_byte_transfer(chnd, I2C_DATA_ADDR, &data_ff, 1, 1);
			i2c_write_byte(chnd, 0x10, 0x00);
			/* write disable command */
			spi_flash_command_short(chnd, SPI_CMD_WRITE_DISABLE,
						"SPI write disable");
			goto __send_aai_cmd;
		}
	}

failed_write:
	/* disable quick AAI mode */
	i2c_byte_transfer(chnd, I2C_DATA_ADDR, &data_ff, 1, 1);
	i2c_write_byte(chnd, 0x10, 0x00);
	/* write disable command */
	spi_flash_command_short(chnd, SPI_CMD_WRITE_DISABLE,
				"SPI write disable");
failed_enter_mode:
	/* exit follow mode */
	spi_flash_follow_mode_exit(chnd, "AAI write");

	if (ret < 0)
		printf("\n\rWriting Failed.\n");
	else
		printf("\n\rWriting Done.\n");

	free(buffer);

	return ret;
}

/*
 * Return zero on success, a negative error value on failures.
 *
 * Change the program command to match the ITE Download
 * The original flow may not work on the DX chip.
 *
 */
static int write_flash3(struct common_hnd *chnd, const char *filename,
			uint32_t offset)
{
	int res, ret = 0;
	int block_write_size = chnd->conf.block_write_size;
	FILE *hnd;
	int size = chnd->flash_size;
	int cnt;
	uint8_t *buf = (uint8_t *)malloc(size);

	if (!buf) {
		fprintf(stderr, "%s: Cannot allocate %d bytes\n", __func__,
			size);
		return -ENOMEM;
	}

	hnd = fopen(filename, "r");
	if (!hnd) {
		fprintf(stderr, "%s: Cannot open file %s for reading\n",
			__func__, filename);
		free(buf);
		return -EIO;
	}
	res = fread(buf, 1, size, hnd);
	if (res <= 0) {
		fprintf(stderr,
			"%s: Failed to read %d bytes from %s with "
			"ferror() %d\n",
			__func__, size, filename, ferror(hnd));
		fclose(hnd);
		free(buf);
		return -EIO;
	}
	fclose(hnd);

	printf("Writing %d bytes at 0x%08x.......\n", res, offset);

	/* Enter follow mode */
	ret = spi_flash_follow_mode(chnd, "Page program");
	if (ret < 0)
		goto failed_write;

	/* Page program instruction allows up to 256 bytes */
	if (block_write_size > 256)
		block_write_size = 256;

	while (res) {
		cnt = (res > block_write_size) ? block_write_size : res;
		if (chnd->conf.erase && is_empty_page(&buf[offset], cnt)) {
			/* do nothing */
		} else if (command_write_pages3(chnd, offset, cnt,
						&buf[offset]) < 0) {
			ret = -EIO;
			goto failed_write;
		}

		res -= cnt;
		offset += cnt;
		draw_spinner(res, res + offset);
	}

failed_write:
	free(buf);
	spi_flash_command_short(chnd, SPI_CMD_WRITE_DISABLE,
				"SPI write disable");
	spi_flash_follow_mode_exit(chnd, "Page program");
	if (ret < 0)
		fprintf(stderr, "%s: Error writing to flash\n", __func__);
	else
		printf("\n\rWriting Done.\n");

	return ret;
}

/* Return zero on success, a non-zero value on failures. */
static int verify_flash(struct common_hnd *chnd, const char *filename,
			uint32_t offset)
{
	int res;
	int file_size;
	FILE *hnd;
	uint8_t *buffer = (uint8_t *)malloc(chnd->flash_size);
	uint8_t *buffer2 = (uint8_t *)malloc(chnd->flash_size);

	if (!buffer || !buffer2) {
		fprintf(stderr, "%s: Cannot allocate %d bytes\n", __func__,
			chnd->flash_size);
		free(buffer);
		free(buffer2);
		return -ENOMEM;
	}

	hnd = fopen(filename, "r");
	if (!hnd) {
		fprintf(stderr, "%s: Cannot open file %s for reading\n",
			__func__, filename);
		res = -EIO;
		goto exit;
	}

	file_size = fread(buffer, 1, chnd->flash_size, hnd);
	if (file_size <= 0) {
		fprintf(stderr,
			"%s: Failed to read %d bytes from %s with "
			"ferror() %d\n",
			__func__, chnd->flash_size, filename, ferror(hnd));
		fclose(hnd);
		res = -EIO;
		goto exit;
	}
	fclose(hnd);

	printf("Verify %d bytes at 0x%08x\n", file_size, offset);
	res = command_read_pages(chnd, offset, chnd->flash_size, buffer2);
	if (res > 0)
		res = memcmp(buffer, buffer2, file_size);

	printf("\n\rVerify %s\n", res ? "Failed!" : "Done.");

exit:
	free(buffer);
	free(buffer2);
	return res;
}

static struct ftdi_context *open_ftdi_device(int vid, int pid, int interface,
					     const char *serial)
{
	struct ftdi_context *ftdi;
	int ret;

	ftdi = ftdi_new();
	if (!ftdi) {
		fprintf(stderr, "Cannot allocate context memory\n");
		return NULL;
	}

	ret = ftdi_set_interface(ftdi, (enum ftdi_interface)interface);
	if (ret < 0) {
		fprintf(stderr, "cannot set ftdi interface %d: %s(%d)\n",
			interface, ftdi_get_error_string(ftdi), ret);
		goto open_failed;
	}
	ret = ftdi_usb_open_desc(ftdi, vid, pid, NULL, serial);
	if (ret < 0) {
		fprintf(stderr, "unable to open ftdi device: %s(%d)\n",
			ftdi_get_error_string(ftdi), ret);
		goto open_failed;
	}
	return ftdi;

open_failed:
	ftdi_free(ftdi);
	return NULL;
}

static int linux_i2c_interface_init(struct common_hnd *chnd)
{
	int err;

	if (!chnd->conf.i2c_dev_path) {
		fprintf(stderr, "Must set --i2c_dev_path when using "
				"Linux i2c-dev interface.\n");
		return -1;
	}
	printf("Attempting to open Linux i2c-dev path %s\n",
	       chnd->conf.i2c_dev_path);
	chnd->i2c_dev_fd = open(chnd->conf.i2c_dev_path, O_RDWR);
	if (chnd->i2c_dev_fd < 0) {
		err = errno;
		perror("Failed to open Linux i2c-dev file path with error");
		fprintf(stderr,
			"Linux i2c-dev file path from --i2c_dev_path "
			"is: %s\n",
			chnd->conf.i2c_dev_path);
		return err ? err : -1;
	}
	printf("Successfully opened Linux i2c-dev path %s\n",
	       chnd->conf.i2c_dev_path);
	return 0;
}

static int linux_i2c_interface_shutdown(struct common_hnd *chnd)
{
	int err;

	printf("Attempting to close Linux i2c-dev file descriptor %d\n",
	       chnd->i2c_dev_fd);
	if (close(chnd->i2c_dev_fd)) {
		err = errno;
		perror("Failed to close Linux i2c-dev file descriptor with "
		       "error");
		return err ? err : -1;
	}
	printf("Successfully closed Linux i2c-dev file descriptor %d\n",
	       chnd->i2c_dev_fd);
	return 0;
}

static int ccd_i2c_interface_init(struct common_hnd *chnd)
{
	chnd->conf.usb_vid = CR50_USB_VID;
	chnd->conf.usb_pid = CR50_USB_PID;
	return connect_to_ccd_i2c_bridge(chnd);
}

static int ccd_i2c_interface_shutdown(struct common_hnd *chnd)
{
	usb_shut_down(&chnd->uep);
	return 0;
}

static int ftdi_i2c_interface_init(struct common_hnd *chnd)
{
	chnd->ftdi_hnd = open_ftdi_device(chnd->conf.usb_vid,
					  chnd->conf.usb_pid,
					  chnd->conf.usb_interface,
					  chnd->conf.usb_serial);
	if (chnd->ftdi_hnd == NULL)
		return -1;
	return 0;
}

static int ftdi_i2c_interface_post_waveform(struct common_hnd *chnd)
{
	return chnd->conf.send_waveform ? 0 : ftdi_config_i2c(chnd->ftdi_hnd);
}

/* Close the FTDI USB handle */
static int ftdi_i2c_interface_shutdown(struct common_hnd *chnd)
{
	ftdi_usb_close(chnd->ftdi_hnd);
	ftdi_free(chnd->ftdi_hnd);
	return 0;
}

static const struct i2c_interface linux_i2c_interface = {
	.interface_init = linux_i2c_interface_init,
	.interface_shutdown = linux_i2c_interface_shutdown,
	.byte_transfer = linux_i2c_byte_transfer,
	/*
	 * 254 bytes is the largest size that works with Servo Micro as of
	 * 2018-11-30. Odd numbers up to 255 result in corruption, and 256 or
	 * greater fails with a timeout from the I2C bus. Fixing that so this
	 * can be increased to match FTDI_BLOCK_WRITE_SIZE would be a useful
	 * speedup.
	 *
	 * 254 byte block sizes cause corruption with Ampton (using any kind of
	 * servo).  128 bytes is the largest block_write_size compatible with
	 * both Ampton and Servo Micro.
	 *
	 * See https://issuetracker.google.com/79684405 for background.
	 */
	.default_block_write_size = 128,
};

static const struct i2c_interface ccd_i2c_interface = {
	.interface_init = ccd_i2c_interface_init,
	.interface_shutdown = ccd_i2c_interface_shutdown,
	.send_special_waveform = ccd_trigger_special_waveform,
	.byte_transfer = ccd_i2c_byte_transfer,
	.default_block_write_size = PAGE_SIZE,
};

static const struct i2c_interface ftdi_i2c_interface = {
	.interface_init = ftdi_i2c_interface_init,
	.interface_post_waveform = ftdi_i2c_interface_post_waveform,
	.interface_shutdown = ftdi_i2c_interface_shutdown,
	.send_special_waveform = ftdi_send_special_waveform,
	.byte_transfer = ftdi_i2c_byte_transfer,
	.default_block_write_size = FTDI_BLOCK_WRITE_SIZE,
};

static int post_waveform_work(struct common_hnd *chnd)
{
	int ret;

	if (chnd->conf.i2c_if->interface_post_waveform) {
		ret = chnd->conf.i2c_if->interface_post_waveform(chnd);
		if (ret)
			return ret;
	}

	if (chnd->conf.disable_watchdog) {
		ret = dbgr_disable_watchdog(chnd);
		if (ret)
			return ret;
	}

	if (chnd->conf.disable_protect_path) {
		ret = dbgr_disable_protect_path(chnd);
		if (ret)
			return ret;
	}

	return 0;
}

static int strdup_with_errmsg(const char *source, char **dest, const char *name)
{
	int ret = 0;
	*dest = strdup(source);
	if (!(*dest)) {
		ret = errno ? errno : -1;
		fprintf(stderr, "strdup() of %zu size string from %s failed.\n",
			strlen(source), name);
	}
	return ret;
}

static const struct option longopts[] = { { "block-write-size", 1, 0, 'b' },
					  { "debug", 0, 0, 'd' },
					  { "erase", 0, 0, 'e' },
					  { "help", 0, 0, 'h' },
					  { "i2c-dev-path", 1, 0, 'D' },
					  { "i2c-interface", 1, 0, 'c' },
					  { "i2c-mux", 0, 0, 'm' },
					  { "interface", 1, 0, 'i' },
					  { "nodisable-protect-path", 0, 0,
					    'Z' },
					  { "nodisable-watchdog", 0, 0, 'z' },
					  { "noverify", 0, 0, 'n' },
					  { "product", 1, 0, 'p' },
					  { "range", 1, 0, 'R' },
					  { "read", 1, 0, 'r' },
					  { "send-waveform", 1, 0, 'W' },
					  { "serial", 1, 0, 's' },
					  { "vendor", 1, 0, 'v' },
					  { "write", 1, 0, 'w' },
					  { NULL, 0, 0, 0 } };

static void display_usage(const char *program)
{
	fprintf(stderr,
		"Usage: %s [-d] [-v <VID>] [-p <PID>] \\\n"
		"\t[-c <linux|ccd|ftdi>] [-D /dev/i2c-<N>] [-i <1|2>] [-S] \\\n"
		"\t[-s <serial>] [-e] [-r <file>] [-W <0|1|false|true>] \\\n"
		"\t[-w <file>] [-R base[:size]] [-m] [-b <size>]\n",
		program);
	fprintf(stderr, "-d, --debug : Output debug traces.\n");
	fprintf(stderr, "-e, --erase : Erase all the flash content.\n");
	fprintf(stderr, "-c, --i2c-interface <linux|ccd|ftdi> : I2C interface "
			"to use\n");
	fprintf(stderr, "-D, --i2c-dev-path /dev/i2c-<N> : Path to "
			"Linux i2c-dev file e.g. /dev/i2c-5;\n"
			"\tonly applicable with --i2c-interface=linux\n");
	fprintf(stderr, "-i, --interface <1> : FTDI interface: A=1, B=2,"
			" ...\n");
	fprintf(stderr,
		"-m, --i2c-mux : Enable i2c-mux (to EC).\n"
		"\tSpecify this flag only if the board has an I2C MUX and\n"
		"\tyou are not using servod.\n");
	fprintf(stderr, "-n, --noverify : Don't auto verify.\n");
	fprintf(stderr, "-b, --block-write-size <size> : Perform writes in\n"
			"\tblocks of this many bytes.\n");
	fprintf(stderr, "-p, --product <0x1234> : USB product ID\n");
	fprintf(stderr,
		"-R, --range base[:size] : Allow to read or write"
		" just a slice\n"
		"\tof the file, starting at <base>:<size> bytes, or til\n"
		"\tthe end of the file if <size> is not specified, expressed\n"
		"\tin hex.\n");
	fprintf(stderr, "-r, --read <file> : Read the flash content and"
			" write it into <file>.\n");
	fprintf(stderr, "-s, --serial <serialname> : USB serial string\n");
	fprintf(stderr, "-v, --vendor <0x1234> : USB vendor ID\n");
	fprintf(stderr,
		"-W, --send-waveform <0|1|false|true> : Send the"
		" special waveform.\n"
		"\tDefault is true. Set to false if ITE direct firmware\n"
		"\tupdate mode has already been enabled.\n");
	fprintf(stderr, "-w, --write <file> : Write <file> to flash.\n");
	fprintf(stderr, "-z, --nodisable-watchdog : Do *not* disable EC "
			"watchdog.\n");
	fprintf(stderr, "-Z, --nodisable-protect-path : Do *not* disable EC "
			"protect path.\n");
}

/*
 * Parses -R command line option parameter, returns zero on success and
 * -1 on errors (non hex values, missing values, etc.).
 */
static int parse_range_options(char *str, struct iteflash_config *conf)
{
	char *size;

	if (!str) {
		fprintf(stderr, "missing range base address specification\n");
		return -1;
	}

	conf->range_base = strtoull(str, &size, 16);
	if (!size || !*size)
		return 0;

	if (*size++ != ':') {
		fprintf(stderr, "wrong range base address specification\n");
		return -1;
	}

	if (!*size) {
		fprintf(stderr, "missing range size specification\n");
		return -1;
	}

	conf->range_size = strtoull(size, &size, 16);
	if ((size && *size) || !conf->range_size) {
		fprintf(stderr, "wrong range size specification\n");
		return -1;
	}

	return 0;
}

static int parse_parameters(int argc, char **argv, struct iteflash_config *conf)
{
	int opt, idx, ret = 0;

	while (!ret &&
	       (opt = getopt_long(argc, argv, "?b:c:D:dehi:mp:R:r:s:uv:W:w:Zz",
				  longopts, &idx)) != -1) {
		switch (opt) {
		case 'b':
			conf->block_write_size = strtol(optarg, NULL, 10);
			break;
		case 'c':
			if (!strcasecmp(optarg, "linux")) {
				conf->i2c_if = &linux_i2c_interface;
			} else if (!strcasecmp(optarg, "ccd")) {
				conf->i2c_if = &ccd_i2c_interface;
			} else if (!strcasecmp(optarg, "ftdi")) {
				conf->i2c_if = &ftdi_i2c_interface;
			} else {
				fprintf(stderr,
					"Unexpected -c / "
					"--i2c-interface value: %s\n",
					optarg);
				ret = -1;
			}
			break;
		case 'D':
			ret = strdup_with_errmsg(optarg, &conf->i2c_dev_path,
						 "-D / --i2c-dev-path");
			break;
		case 'd':
			conf->debug = 1;
			break;
		case 'e':
			conf->erase = 1;
			break;
		case 'h':
		case '?':
			display_usage(argv[0]);
			ret = 2;
			break;
		case 'i':
			conf->usb_interface = strtol(optarg, NULL, 10);
			break;
		case 'm':
			conf->i2c_mux = 1;
			break;
		case 'n':
			conf->verify = 0;
			break;
		case 'p':
			conf->usb_pid = strtol(optarg, NULL, 16);
			break;
		case 'R':
			ret = parse_range_options(optarg, conf);
			break;
		case 'r':
			ret = strdup_with_errmsg(optarg, &conf->input_filename,
						 "-r / --read");
			break;
		case 's':
			ret = strdup_with_errmsg(optarg, &conf->usb_serial,
						 "-s / --serial");
			break;
		case 'v':
			conf->usb_vid = strtol(optarg, NULL, 16);
			break;
		case 'W':
			if (!strcmp(optarg, "0") ||
			    !strcasecmp(optarg, "false")) {
				conf->send_waveform = 0;
				break;
			}
			if (!strcmp(optarg, "1") ||
			    !strcasecmp(optarg, "true")) {
				conf->send_waveform = 1;
				break;
			}
			fprintf(stderr,
				"Unexpected -W / --special-waveform "
				"value: %s\n",
				optarg);
			ret = -1;
			break;
		case 'w':
			ret = strdup_with_errmsg(optarg, &conf->output_filename,
						 "-w / --write");
			break;
		case 'z':
			conf->disable_watchdog = 0;
			break;
		case 'Z':
			conf->disable_protect_path = 0;
			break;
		}
	}

	if (ret)
		config_release(conf);
	return ret;
}

static void sighandler(int signum)
{
	int status;
	printf("\nCaught signal %d: %s\nExiting...\n", signum,
	       strsignal(signum));
	wait(&status);
	exit_requested = status;
}

static void register_sigaction(void)
{
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
}

int main(int argc, char **argv)
{
	int ret = 1, other_ret;
	struct common_hnd chnd = {
		/* Default flag settings. */
		.conf = {
			.send_waveform = 1,
			.disable_watchdog = 1,
			.disable_protect_path = 1,
			.usb_interface = SERVO_INTERFACE,
			.usb_vid = SERVO_USB_VID,
			.usb_pid = SERVO_USB_PID,
			.verify = 1,
			.i2c_if = &ftdi_i2c_interface,
		},
	};

	/* Parse command line options */
	other_ret = parse_parameters(argc, argv, &chnd.conf);
	if (other_ret)
		return other_ret;

	/* Fill in block_write_size if not set from command line. */
	if (!chnd.conf.block_write_size)
		chnd.conf.block_write_size =
			chnd.conf.i2c_if->default_block_write_size;

	/* Open the communications channel. */
	if (chnd.conf.i2c_if->interface_init &&
	    chnd.conf.i2c_if->interface_init(&chnd))
		goto return_after_parse;

	/* Register signal handler after opening the communications channel. */
	register_sigaction();

	if (chnd.conf.i2c_mux) {
		printf("configuring I2C MUX to EC.\n");

		if (config_i2c_mux(&chnd, I2C_MUX_CMD_EC))
			goto return_after_init;
	}

	/* Trigger embedded monitor detection */
	if (chnd.conf.send_waveform) {
		if (send_special_waveform(&chnd))
			goto return_after_init;
	} else {
		/* Stop EC ASAP after sending special waveform. */
		dbgr_stop_ec(&chnd);

		ret = check_chipid(&chnd);
		if (ret) {
			fprintf(stderr,
				"Failed to get ITE chip ID.  This "
				"could be because the ITE direct firmware "
				"update (DFU) mode is not enabled.\n");
			goto return_after_init;
		}
	}

	/* Turn off power rails by reset GPIOs to default (input). */
	dbgr_reset_gpio(&chnd);

	check_flashid(&chnd);

	ret = post_waveform_work(&chnd);
	if (ret)
		goto return_after_init;

	if (chnd.conf.input_filename) {
		ret = read_flash(&chnd);
		if (ret)
			goto return_after_init;
	}

	switch (eflash_type) {
	case EFLASH_TYPE_8315:
		sector_erase_pages = 4;
		spi_cmd_sector_erase = SPI_CMD_SECTOR_ERASE_1K;
		break;
	case EFLASH_TYPE_KGD:
		sector_erase_pages = 16;
		spi_cmd_sector_erase = SPI_CMD_SECTOR_ERASE_4K;
		break;
	default:
		printf("Invalid EFLASH TYPE!");
		ret = -EINVAL;
		break;
	}

	if (ret)
		goto return_after_init;

	if (chnd.conf.erase) {
		if (chnd.flash_cmd_v2)
			/* Do Normal Erase Function */
			command_erase2(&chnd, chnd.flash_size, 0, 0);
		else
			command_erase(&chnd, chnd.flash_size, 0);
	}

	if (chnd.conf.output_filename) {
		if (chnd.flash_cmd_v2)
			switch (eflash_type) {
			case EFLASH_TYPE_8315:
				ret = write_flash2(
					&chnd, chnd.conf.output_filename, 0);
				break;
			case EFLASH_TYPE_KGD:
				ret = write_flash3(
					&chnd, chnd.conf.output_filename, 0);
				break;
			default:
				printf("Invalid EFLASH TYPE!");
				ret = -EINVAL;
				break;
			}
		else
			ret = write_flash(&chnd, chnd.conf.output_filename, 0);
		if (ret)
			goto return_after_init;
		if (chnd.conf.verify) {
			ret = verify_flash(&chnd, chnd.conf.output_filename, 0);
			if (ret)
				goto return_after_init;
		}
	}

	/* Normal exit */
	ret = 0;

return_after_init:
	/*
	 * Exit DBGR mode. This ensures EC won't hold clock/data pins of I2C.
	 * Avoid resetting EC here because flash_ec will after iteflash exits.
	 * This avoids double reset after flash sequence.
	 */
	exit_dbgr_mode(&chnd);

	if (chnd.conf.i2c_mux) {
		printf("configuring I2C MUX to none.\n");
		config_i2c_mux(&chnd, I2C_MUX_CMD_NONE);
	}

	if (chnd.conf.i2c_if->interface_shutdown) {
		other_ret = chnd.conf.i2c_if->interface_shutdown(&chnd);
		if (!ret && other_ret)
			ret = other_ret;
	}

return_after_parse:
	config_release(&chnd.conf);
	return ret;
}
