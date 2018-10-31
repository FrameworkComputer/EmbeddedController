/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ITE83xx SoC in-system programming tool
 */

#include <errno.h>
#include <ftdi.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "usb_if.h"

/* Default FTDI device : Servo v2. */
#define SERVO_USB_VID 0x18d1
#define SERVO_USB_PID 0x5002
#define SERVO_INTERFACE INTERFACE_B

/* Default CCD device: Cr50. */
#define CR50_USB_VID 0x18d1
#define CR50_USB_PID 0x5014

/* Cr50 exposed properties of the USB I2C endpoint. */
#define CR50_I2C_SUBCLASS  82
#define CR50_I2C_PROTOCOL  1

#define CROS_CMD_ADDR	  0x78
#define CROS_CMD_ITE_SYNC    0

/* DBGR I2C addresses */
#define I2C_CMD_ADDR   0x5A
#define I2C_DATA_ADDR  0x35
#define I2C_BLOCK_ADDR 0x79

#define I2C_FREQ 400000

/* I2C pins on the FTDI interface */
#define SCL_BIT        (1 << 0)
#define SDA_BIT        (1 << 1)

/* Chip ID register value */
#define CHIP_ID 0x8380

/* Embedded flash page size */
#define PAGE_SIZE		256

/* Embedded flash block write size for different programming modes. */
#define FTDI_BLOCK_WRITE_SIZE	65536

/* Embedded flash number of pages in a sector erase */
#define SECTOR_ERASE_PAGES	4

/* JEDEC SPI Flash commands */
#define SPI_CMD_PAGE_PROGRAM	0x02
#define SPI_CMD_WRITE_DISABLE	0x04
#define SPI_CMD_READ_STATUS	0x05
#define SPI_CMD_WRITE_ENABLE	0x06
#define SPI_CMD_FAST_READ	0x0B
#define SPI_CMD_CHIP_ERASE	0x60
#define SPI_CMD_SECTOR_ERASE	0xD7
#define SPI_CMD_WORD_PROGRAM	0xAD
#define SPI_CMD_EWSR		0x50 /* Enable Write Status Register */
#define SPI_CMD_WRSR		0x01 /* Write Status Register */

/* Size for FTDI outgoing buffer */
#define FTDI_CMD_BUF_SIZE (1<<12)

/* Reset Status */
#define RSTS_VCCDO_PW_ON	0x40
#define RSTS_VFSPIPG		0x20
#define RSTS_HGRST		0x08
#define RSTS_GRST		0x04

/* store custom parameters */
const char *input_filename;
const char *output_filename;
static int usb_vid = SERVO_USB_VID;
static int usb_pid = SERVO_USB_PID;
static int usb_interface = SERVO_INTERFACE;
static char *usb_serial;
static int flash_size;
static int exit_requested;
static int is8320dx;

/* debug traces : default OFF*/
static int debug;

/* optional command flags */
enum {
	FLAG_UNPROTECT      = 0x01,
	FLAG_ERASE          = 0x02,
	FLAG_CCD_MODE       = 0x04
};

enum interface_type {
	FTDI_IF,
	CCD_IF
};

struct common_hnd {
	enum interface_type iftype;
	union {
		struct ftdi_context *ftdi_hnd;
		struct usb_endpoint uep;
	};
};

/* number of bytes to send consecutively before checking for ACKs */
#define TX_BUFFER_LIMIT	32

/* For backwards compatibility let FTDI value be the default. */
static int block_write_size_ = FTDI_BLOCK_WRITE_SIZE;
static int block_write_size(void)
{
	return block_write_size_;
}

static int i2c_add_send_byte(struct ftdi_context *ftdi, uint8_t *buf,
			     uint8_t *ptr, uint8_t *tbuf, int tcnt)
{
	int ret, i, j, remaining_data, ack_idx;
	int tx_buffered = 0;
	static uint8_t ack[TX_BUFFER_LIMIT];
	uint8_t *b = ptr;
	uint8_t failed_ack = 0;

	for (i = 0; i < tcnt; i++) {
		/* If we got a termination signal, stop sending data */
		if (exit_requested)
			return -1;

		/* WORKAROUND: force SDA before sending the next byte */
		*b++ = SET_BITS_LOW; *b++ = SDA_BIT; *b++ = SCL_BIT | SDA_BIT;
		/* write byte */
		*b++ = MPSSE_DO_WRITE | MPSSE_BITMODE | MPSSE_WRITE_NEG;
		*b++ = 0x07; *b++ = *tbuf++;
		/* prepare for ACK */
		*b++ = SET_BITS_LOW; *b++ = 0; *b++ = SCL_BIT;
		/* read ACK */
		*b++ = MPSSE_DO_READ | MPSSE_BITMODE | MPSSE_LSB;
		*b++ = 0;
		*b++ = SEND_IMMEDIATE;

		tx_buffered++;

		/*
		 * On the last byte, or every TX_BUFFER_LIMIT bytes, read the
		 * ACK bits.
		 */
		if (i == tcnt-1 || (tx_buffered == TX_BUFFER_LIMIT)) {
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
				return  -ENXIO;
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
		*b++ = SET_BITS_LOW; *b++ = 0; *b++ = SCL_BIT;
		/* read the byte on the wire */
		*b++ = MPSSE_DO_READ; *b++ = 0; *b++ = 0;

		if (i == rcnt - 1) {
			/* NACK last byte */
			*b++ = SET_BITS_LOW; *b++ = 0; *b++ = SCL_BIT;
			*b++ = MPSSE_DO_WRITE | MPSSE_BITMODE | MPSSE_WRITE_NEG;
			*b++ = 0; *b++ = 0xff; *b++ = SEND_IMMEDIATE;
		} else {
			/* ACK all other bytes */
			*b++ = SET_BITS_LOW; *b++ = 0; *b++ = SCL_BIT | SDA_BIT;
			*b++ = MPSSE_DO_WRITE | MPSSE_BITMODE | MPSSE_WRITE_NEG;
			*b++ = 0; *b++ = 0; *b++ = SEND_IMMEDIATE;
		}
	}

	ret = ftdi_write_data(ftdi, buf, b - buf);
	if (ret < 0) {
		fprintf(stderr, "failed to prepare read\n");
		return ret;
	}

	rbuf_idx = 0;
	do {
		/* If we got a termination signal, stop sending data */
		if (exit_requested)
			return -1;

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
static int ccd_i2c_byte_transfer(struct usb_endpoint *uep, uint8_t addr,
				 uint8_t *data, int write, int numbytes)
{
	uint8_t usb_buffer[USB_I2C_HEADER_SIZE + numbytes +
			   (((!write * numbytes) > 0x7f) ? 2 : 0)];
	size_t response_size;
	size_t extra = 0;

	/* Do nothing if user wants to quit. */
	if (exit_requested)
		return -1;

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
	usb_trx(uep, usb_buffer,
		write ? sizeof(usb_buffer) : USB_I2C_HEADER_SIZE + extra,
		usb_buffer, sizeof(usb_buffer), 1, &response_size);

	if (response_size < (USB_I2C_HEADER_SIZE  + (write ? 0 : numbytes))) {
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

		fprintf(stderr, "%s: usb i2c error %d\n",
			__func__,
			(((uint16_t)usb_buffer[1]) << 8) + usb_buffer[0]);

		return -rv;
	}

	if (!write)
		memcpy(data, usb_buffer + USB_I2C_HEADER_SIZE, numbytes);

	return 0;
}

static int i2c_byte_transfer(struct common_hnd *chnd, uint8_t addr,
			     uint8_t *data, int write, int numbytes)
{
	int ret, rets;
	static uint8_t buf[FTDI_CMD_BUF_SIZE];
	uint8_t *b;
	uint8_t slave_addr;
	struct ftdi_context *ftdi;

	if (chnd->iftype == CCD_IF)
		return ccd_i2c_byte_transfer(&chnd->uep, addr,
					     data, write, numbytes);

	ret = 0;
	b = buf;
	ftdi = chnd->ftdi_hnd;

	/* START condition */
	/* SCL & SDA high */
	*b++ = SET_BITS_LOW; *b++ = 0; *b++ = 0;
	*b++ = SET_BITS_LOW; *b++ = 0; *b++ = 0;
	/* SCL high, SDA low */
	*b++ = SET_BITS_LOW; *b++ = 0; *b++ = SDA_BIT;
	*b++ = SET_BITS_LOW; *b++ = 0; *b++ = SDA_BIT;
	/* SCL low, SDA low */
	*b++ = SET_BITS_LOW; *b++ = 0; *b++ = SCL_BIT | SDA_BIT;
	*b++ = SET_BITS_LOW; *b++ = 0; *b++ = SCL_BIT | SDA_BIT;

	/* send address */
	slave_addr = (addr << 1) | (write ? 0 : 1);
	ret = i2c_add_send_byte(ftdi, buf, b, &slave_addr, 1);
	if (ret < 0) {
		if (debug)
			fprintf(stderr, "address %02x failed\n", addr);
		ret = -ENXIO;
		goto exit_xfer;
	}

	b = buf;
	if (write) /* write data */
		ret = i2c_add_send_byte(ftdi, buf, b, data, numbytes);
	else /* read data */
		ret = i2c_add_recv_bytes(ftdi, buf, b, data, numbytes);

exit_xfer:
	b = buf;
	/* STOP condition */
	/* SCL high, SDA low */
	*b++ = SET_BITS_LOW; *b++ = 0; *b++ = SDA_BIT;
	*b++ = SET_BITS_LOW; *b++ = 0; *b++ = SDA_BIT;
	/* SCL high, SDA high */
	*b++ = SET_BITS_LOW; *b++ = 0; *b++ = 0;
	*b++ = SET_BITS_LOW; *b++ = 0; *b++ = 0;

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

static int check_chipid(struct common_hnd *chnd)
{
	int ret;
	uint8_t ver = 0xff;
	uint16_t id = 0xffff;
	uint16_t DX[5] = {128, 192, 256, 384, 512};

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
		fprintf(stderr, "Invalid chip id: %04x\n", id);
		return -EINVAL;
	}
	/* compute embedded flash size from CHIPVER field */
	if ((ver & 0x0f) == 0x03)  {
		flash_size = DX[(ver & 0xF0)>>5] * 1024;
		is8320dx = 1;
	} else {
		flash_size = (128 + (ver & 0xF0)) * 1024;
		is8320dx = 0;
	}
	printf("CHIPID %04x, CHIPVER %02x, Flash size %d kB\n", id, ver,
			flash_size / 1024);

	return 0;
}

/* DBGR Reset*/
static int dbgr_reset(struct common_hnd *chnd, unsigned char val)
{
	int ret = 0;

	/* Reset CPU only, and we keep power state until flashing is done. */
	ret |= i2c_write_byte(chnd, 0x2f, 0x20);
	ret |= i2c_write_byte(chnd, 0x2e, 0x06);

	/* Enable the Reset Status by val */
	ret |= i2c_write_byte(chnd, 0x30, val);

	ret |= i2c_write_byte(chnd, 0x27, 0x80);
	if (ret < 0)
		printf("DBGR RESET FAILED\n");

	return 0;
}

/* disable watchdog*/
static int dbgr_disable_watchdog(struct common_hnd *chnd)
{
	int ret = 0;

	ret |= i2c_write_byte(chnd, 0x2f, 0x1f);
	ret |= i2c_write_byte(chnd, 0x2e, 0x05);
	ret |= i2c_write_byte(chnd, 0x30, 0x30);

	if (ret < 0)
		printf("DBGR DISABLE WATCHDOG FAILED!\n");

	return 0;
}

/* disable protect path from DBGR */
static int dbgr_disable_protect_path(struct common_hnd *chnd)
{
	int ret = 0, i;

	ret |= i2c_write_byte(chnd, 0x2f, 0x20);
	for (i = 0; i < 32; i++) {
		ret |= i2c_write_byte(chnd, 0x2e, 0xa0+i);
		ret |= i2c_write_byte(chnd, 0x30, 0);
	}

	if (ret < 0)
		printf("DISABLE PROTECT PATH FROM DBGR FAILED!\n");

	return 0;
}

/* Enter follow mode and FSCE# high level */
static int spi_flash_follow_mode(struct common_hnd *chnd, char *desc)
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
static int spi_flash_follow_mode_exit(struct common_hnd *chnd, char *desc)
{
	int ret = 0;

	ret |= i2c_write_byte(chnd, 0x07, 0x00);
	ret |= i2c_write_byte(chnd, 0x06, 0x00);

	ret = (ret ? -EIO : 0);
	if (ret < 0)
		fprintf(stderr, "Flash %s exit follow mode FAILED (%d)\n",
			desc, ret);

	return ret;
}

/* SPI Flash generic command, short version */
static int spi_flash_command_short(struct common_hnd *chnd,
				   uint8_t cmd, char *desc)
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
static int spi_flash_set_erase_page(struct common_hnd *chnd,
				    int page, char *desc)
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
static int spi_poll_busy(struct common_hnd *chnd, char *desc)
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

static int spi_check_write_enable(struct common_hnd *chnd, char *desc)
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

static int config_i2c(struct ftdi_context *ftdi)
{
	int ret;
	uint8_t buf[5];
	uint16_t divisor;

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
	divisor = (60000000 / (2 * I2C_FREQ * 3 / 2 /* 3-phase CLK */) - 1);
	buf[0] = EN_3_PHASE;
	buf[1] = DIS_DIV_5;
	buf[2] = TCK_DIVISOR;
	buf[3] = divisor & 0xff;
	buf[4] = divisor >> 8;
	ret = ftdi_write_data(ftdi, buf, sizeof(buf));
	return ret;
}

/* Special waveform definition */
#define SPECIAL_LEN_USEC 50000ULL /* us */
#define SPECIAL_FREQ    400000ULL

#define SPECIAL_PATTERN 0x0000020301010302ULL
#define SPECIAL_PATTERN_SDA_L_SCL_L 0x0000000000000000ULL
#define SPECIAL_PATTERN_SDA_H_SCL_L 0x0202020202020202ULL
#define SPECIAL_PATTERN_SDA_L_SCL_H 0x0101010101010101ULL
#define SPECIAL_PATTERN_SDA_H_SCL_H 0x0303030303030303ULL
#define TICK_COUNT 24

#define MSEC    1000
#define USEC 1000000

#define SPECIAL_BUFFER_SIZE \
	(((SPECIAL_LEN_USEC * SPECIAL_FREQ * 2 / USEC) + 7) & ~7)

static int ccd_trigger_special_waveform(struct usb_endpoint *uep)
{
	uint8_t response[20];
	size_t rsize;
	uint8_t req[] = {
		0, /* Port 0. Might be necessary to modify. */
		CROS_CMD_ADDR, /* Chrome OS dedicated address. */
		1,	/* Will send a single byte command. */
		0,	/* No need to read back anything. */
		CROS_CMD_ITE_SYNC
	};

	usb_trx(uep, req, sizeof(req), response, sizeof(response), 1, &rsize);

	if (rsize < USB_I2C_HEADER_SIZE)
		return -1;

	if (response[0])
		return -response[0];

	return 0;
}

static int ftdi_send_special_waveform(struct ftdi_context *ftdi, uint64_t *wave)
{
	int ret;
	int i;
	uint8_t release_lines[] = {SET_BITS_LOW, 0, 0};

	/* Reset the FTDI into a known state */
	ret = ftdi_set_bitmode(ftdi, 0xFF, BITMODE_RESET);
	if (ret) {
		fprintf(stderr, "failed to reset FTDI\n");
		return ret;
	}

	/*
	 * set the clock divider, so we output a new bitbang value every
	 * 2.5us.
	 */
	ret = ftdi_set_baudrate(ftdi, 160000);
	if (ret) {
		fprintf(stderr, "failed to set bitbang clock\n");
		return ret;
	}

	/* Enable asynchronous bit-bang mode */
	ret = ftdi_set_bitmode(ftdi, 0xFF, BITMODE_BITBANG);
	if (ret) {
		fprintf(stderr, "failed to set bitbang mode\n");
		return ret;
	}

	/* do usb special waveform */
	wave[0] = 0x0;
	ftdi_write_data(ftdi, (uint8_t *)wave, 1);
	usleep(5000);

	/* program each special tick */
	for (i = 0; i < TICK_COUNT; ) {
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
	config_i2c(ftdi);
	ftdi_write_data(ftdi, release_lines, sizeof(release_lines));

	return ret;
}

static int send_special_waveform(struct common_hnd *chnd)
{
	int ret;
	uint64_t *wave;
	int iterations;

	if (chnd->iftype == FTDI_IF)
		wave = malloc(SPECIAL_BUFFER_SIZE);
	else
		wave = NULL;

	printf("Waiting for the EC power-on sequence ...");
	fflush(stdout);

	iterations = 0;

	do {
		if (chnd->iftype == FTDI_IF)
			ret = ftdi_send_special_waveform(chnd->ftdi_hnd, wave);
		else
			ret = ccd_trigger_special_waveform(&chnd->uep);

		if (ret)
			break;

		/* wait for PLL stable for 5ms (plus remaining USB transfers) */
		usleep(10 * MSEC);

		if (spi_flash_follow_mode(chnd, "enter follow mode") >= 0) {

			spi_flash_follow_mode_exit(chnd, "exit follow mode");
			/*
			 * If we can talk to chip, then we can break the retry
			 * loop
			 */
			ret = check_chipid(chnd);

			/* disable watchdog before programming sequence */
			if (!ret) {
				dbgr_disable_watchdog(chnd);
				dbgr_disable_protect_path(chnd);
			}
		} else {
			ret = -1;
			if (!(iterations % 10))
				printf("!please reset EC if flashing sequence"
						" is not starting!\n");
		}

	} while (ret && (iterations++ < 10));

	if (ret)
		printf(" Failed!\n");
	else
		printf(" Done.\n");

	if (wave)
		free(wave);
	return ret;
}

static int windex;
static const char wheel[] = {'|', '/', '-', '\\' };
static void draw_spinner(uint32_t remaining, uint32_t size)
{
	int percent = (size - remaining)*100/size;
	fprintf(stderr, "\r%c%3d%%", wheel[windex++], percent);
	windex %= sizeof(wheel);
}

static int command_read_pages(struct common_hnd *chnd, uint32_t address,
			      uint32_t size, uint8_t *buffer)
{
	int res = -EIO;
	uint32_t remaining = size;
	int cnt;
	uint16_t page;

	if (spi_flash_follow_mode(chnd, "fast read") < 0)
		goto failed_read;

	while (remaining) {
		uint8_t cmd = 0x9;

		cnt = (remaining > PAGE_SIZE) ? PAGE_SIZE : remaining;
		page = address / PAGE_SIZE;

		draw_spinner(remaining, size);

		/* Fast Read command */
		if (spi_flash_command_short(chnd, SPI_CMD_FAST_READ,
			"fast read") < 0)
			goto failed_read;
		res = i2c_write_byte(chnd, 0x08, page >> 8);
		res += i2c_write_byte(chnd, 0x08, page & 0xff);
		res += i2c_write_byte(chnd, 0x08, 0x00);
		res += i2c_write_byte(chnd, 0x08, 0x00);
		if (res < 0) {
			fprintf(stderr, "page address set failed\n");
			goto failed_read;
		}

		/* read page data */
		res = i2c_byte_transfer(chnd, I2C_CMD_ADDR, &cmd, 1, 1);
		res = i2c_byte_transfer(chnd, I2C_BLOCK_ADDR, buffer, 0, cnt);
		if (res < 0) {
			fprintf(stderr, "page data read failed\n");
			goto failed_read;
		}

		address += cnt;
		remaining -= cnt;
		buffer += cnt;
	}
	/* No error so far */
	res = size;
failed_read:
	if (spi_flash_follow_mode_exit(chnd, "fast read") < 0)
		res = -EIO;

	return res;
}

static int command_write_pages(struct common_hnd *chnd, uint32_t address,
			       uint32_t size, uint8_t *buffer)
{
	int res = -EIO;
	uint32_t remaining = size;
	int cnt;
	uint8_t addr_H, addr_M, addr_L;
	uint8_t cmd;

	if (spi_flash_follow_mode(chnd, "AAI write") < 0)
		goto failed_write;

	while (remaining) {
		cnt = (remaining > block_write_size()) ?
			block_write_size() : remaining;
		addr_H = (address >> 16) & 0xFF;
		addr_M = (address >> 8) & 0xFF;
		addr_L = (address) & 0xFF;

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

		/* Write up to block_write_size() data */
		res = i2c_write_byte(chnd, 0x10, 0x20);
		res = i2c_byte_transfer(chnd, I2C_BLOCK_ADDR, buffer, 1, cnt);
		buffer += cnt;

		if (res < 0) {
			fprintf(stderr, "Flash data write failed\n");
			goto failed_write;
		}

		cmd = 0xff;
		res = i2c_byte_transfer(chnd, I2C_DATA_ADDR, &cmd, 1, 1);
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
 * Write another program flow to match the
 * original ITE 8903 Download board.
 */
static int command_write_pages2(struct common_hnd *chnd, uint32_t address,
				uint32_t size, uint8_t *buffer)
{
	int res = 0;
	uint8_t BA, A1, A0, data;

	/*
	 * TODOD(b/<>):
	 * The code will merge to the original function
	 */

	res |= i2c_write_byte(chnd, 0x07, 0x7f);
	res |= i2c_write_byte(chnd, 0x06, 0xff);
	res |= i2c_write_byte(chnd, 0x04, 0xFF);

	/* SMB_SPI_Flash_Enable_Write_Status */
	if (spi_flash_command_short(chnd, SPI_CMD_EWSR,
		"Enable Write Status Register") < 0) {
		res = -EIO;
		goto failed_write;
	}

	/* SMB_SPI_Flash_Write_Status_Reg */
	res |= i2c_write_byte(chnd, 0x05, 0xfe);
	res |= i2c_write_byte(chnd, 0x08, 0x00);
	res |= i2c_write_byte(chnd, 0x05, 0xfd);
	res |= i2c_write_byte(chnd, 0x08, 0x01);
	res |= i2c_write_byte(chnd, 0x08, 0x00);

	/* SMB_SPI_Flash_Write_Enable */
	if (spi_flash_command_short(chnd, SPI_CMD_WRITE_ENABLE,
		"SPI Command Write Enable") < 0) {
		res = -EIO;
		goto failed_write;
	}

	/* SMB_SST_SPI_Flash_AAI2_Program */
	if (spi_flash_command_short(chnd, SPI_CMD_WORD_PROGRAM,
		"SPI AAI2 Program") < 0) {
		res = -EIO;
		goto failed_write;
	}

	BA = address>>16;
	A1 = address>>8;
	A0 = 0;

	res = i2c_byte_transfer(chnd, I2C_DATA_ADDR, &BA, 1, 1);
	res |= i2c_byte_transfer(chnd, I2C_DATA_ADDR, &A1, 1, 1);
	res |= i2c_byte_transfer(chnd, I2C_DATA_ADDR, &A0, 1, 1);
	res |= i2c_byte_transfer(chnd, I2C_DATA_ADDR, buffer++, 1, 1);
	res |= i2c_byte_transfer(chnd, I2C_DATA_ADDR, buffer++, 1, 1);

	/* Wait until not busy */
	if (spi_poll_busy(chnd, "AAI write") < 0)
		goto failed_write;

	res = i2c_write_byte(chnd, 0x10, 0x20);
	res = i2c_byte_transfer(chnd, I2C_BLOCK_ADDR,
		buffer, 1, block_write_size()-2);

	/* No error so far */
	res = size;

	data = 0xff;
	res = i2c_byte_transfer(chnd, I2C_DATA_ADDR, &data, 1, 1);
	res = i2c_write_byte(chnd, 0x10, 0x00);

failed_write:
	if (spi_flash_command_short(chnd, SPI_CMD_WRITE_DISABLE,
		"write disable exit AAI write") < 0)
		res = -EIO;

	return res;
}

static int command_write_unprotect(struct common_hnd *chnd)
{
	/* TODO(http://crosbug.com/p/23576): implement me */
	return 0;
}

static int command_erase(struct common_hnd *chnd, uint32_t len, uint32_t off)
{
	int res = -EIO;
	int page = 0;
	uint32_t remaining = len;

	printf("Erasing chip...\n");

	if (off != 0 || len != flash_size) {
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
		if (remaining == flash_size) {
			if (spi_flash_command_short(chnd, SPI_CMD_CHIP_ERASE,
				"chip erase") < 0)
				goto failed_erase;
			goto wait_busy_cleared;
		}

		/* do sector erase */
		if (spi_flash_command_short(chnd, SPI_CMD_SECTOR_ERASE,
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

		if (remaining == flash_size)  {
			remaining = 0;
			draw_spinner(remaining, len);
		} else {
			page += SECTOR_ERASE_PAGES;
			remaining -= SECTOR_ERASE_PAGES * PAGE_SIZE;
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
static int command_erase2(struct common_hnd *chnd, uint32_t len,
			  uint32_t off, uint32_t reset)
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

	if (off != 0 || len != flash_size) {
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
		if (spi_flash_command_short(chnd, SPI_CMD_SECTOR_ERASE,
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

		page += SECTOR_ERASE_PAGES;
		remaining -= SECTOR_ERASE_PAGES * PAGE_SIZE;
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
static int read_flash(struct common_hnd *chnd, const char *filename,
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
		size = flash_size;
	printf("Reading %d bytes at 0x%08x\n", size, offset);
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
	int size = flash_size;
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
	res = fread(buffer, 1, size, hnd);
	if (res <= 0) {
		fprintf(stderr, "Cannot read %s\n", filename);
		free(buffer);
		return -EIO;
	}
	fclose(hnd);

	printf("Writing %d bytes at 0x%08x\n", res, offset);
	written = command_write_pages(chnd, offset, res, buffer);
	if (written != res) {
		fprintf(stderr, "Error writing to flash\n");
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
	int res, written;
	FILE *hnd;
	int size = flash_size;
	int cnt;
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
	res = fread(buffer, 1, size, hnd);
	if (res <= 0) {
		fprintf(stderr, "Cannot read %s\n", filename);
		free(buffer);
		fclose(hnd);
		return -EIO;
	}
	fclose(hnd);

	offset = 0;
	printf("Writing %d bytes at 0x%08x.......\n", res, offset);
	while (res) {
		cnt = (res > block_write_size()) ?
				block_write_size() : res;
		written = command_write_pages2(chnd, offset, cnt,
				&buffer[offset]);
		if (written == -EIO)
			goto failed_write;

		res -= cnt;
		offset += cnt;
		draw_spinner(res, res + offset);
	}

	if (written != res) {
failed_write:
		fprintf(stderr, "Error writing to flash\n");
		free(buffer);
		return -EIO;
	}
	printf("\n\rWriting Done.\n");
	free(buffer);

	return 0;
}

/* Return zero on success, a non-zero value on failures. */
static int verify_flash(struct common_hnd *chnd, const char *filename,
			uint32_t offset)
{
	int res;
	int file_size;
	FILE *hnd;
	uint8_t *buffer  = malloc(flash_size);
	uint8_t *buffer2 = malloc(flash_size);

	if (!buffer || !buffer2) {
		fprintf(stderr, "Cannot allocate %d bytes\n", flash_size);
		free(buffer);
		free(buffer2);
		return -ENOMEM;
	}

	hnd = fopen(filename, "r");
	if (!hnd) {
		fprintf(stderr, "Cannot open file %s for reading\n", filename);
		res = -EIO;
		goto exit;
	}

	file_size = fread(buffer, 1, flash_size, hnd);
	fclose(hnd);
	if (file_size <= 0) {
		fprintf(stderr, "Cannot read %s\n", filename);
		res = -EIO;
		goto exit;
	}

	printf("Verify %d bytes at 0x%08x\n", file_size, offset);
	res = command_read_pages(chnd, offset, flash_size, buffer2);
	if (res > 0)
		res = memcmp(buffer, buffer2, file_size);

	printf("\n\rVerify %s\n", res ? "Failed!" : "Done.");

exit:
	free(buffer);
	free(buffer2);
	return res;
}

static struct ftdi_context *open_ftdi_device(int vid, int pid,
					     int interface, char *serial)
{
	struct ftdi_context *ftdi;
	int ret;

	ftdi = ftdi_new();
	if (!ftdi) {
		fprintf(stderr, "Cannot allocate context memory\n");
		return NULL;
	}

	ret = ftdi_set_interface(ftdi, interface);
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

static const struct option longopts[] = {
	{"ccd", 0, 0, 'c'},
	{"debug", 0, 0, 'd'},
	{"erase", 0, 0, 'e'},
	{"help", 0, 0, 'h'},
	{"interface", 1, 0, 'i'},
	{"product", 1, 0, 'p'},
	{"read", 1, 0, 'r'},
	{"serial", 1, 0, 's'},
	{"unprotect", 0, 0, 'u'},
	{"vendor", 1, 0, 'v'},
	{"write", 1, 0, 'w'},
	{NULL, 0, 0, 0}
};

static void display_usage(char *program)
{
	fprintf(stderr, "Usage: %s [-c] [-d] [-v <VID>] [-p <PID>] [-i <1|2>] "
		"[-s <serial>] [-u] [-e] [-r <file>] [-w <file>]\n", program);
	fprintf(stderr, "--c[cd] : use CCD interface instead of FTDI, make "
			"sure this option is included before -p and or -v\n");
	fprintf(stderr, "--d[ebug] : output debug traces\n");
	fprintf(stderr, "--e[rase] : erase all the flash content\n");
	fprintf(stderr, "--i[interface] <1> : FTDI interface: A=1, B=2, ...\n");
	fprintf(stderr, "--p[roduct] <0x1234> : USB product ID\n");
	fprintf(stderr, "--r[ead] <file> : read the flash content and "
			"write it into <file>\n");
	fprintf(stderr, "--s[erial] <serialname> : USB serial string\n");
	fprintf(stderr, "--u[nprotect] : remove flash write protect\n");
	fprintf(stderr, "--v[endor] <0x1234> : USB vendor ID\n");
	fprintf(stderr, "--w[rite] <file> : read <file> and "
			"write it to flash\n");
	exit(2);
}

static int parse_parameters(int argc, char **argv)
{
	int opt, idx;
	int flags = 0;

	while ((opt = getopt_long(argc, argv, "?cdehi:p:r:s:uv:w:",
				  longopts, &idx)) != -1) {
		switch (opt) {
		case 'c':
			flags |= FLAG_CCD_MODE;
			usb_vid = CR50_USB_VID;
			usb_pid = CR50_USB_PID;
			block_write_size_ = PAGE_SIZE;
			break;
		case 'd':
			debug = 1;
			break;
		case 'e':
			flags |= FLAG_ERASE;
			break;
		case 'h':
		case '?':
			display_usage(argv[0]);
			break;
		case 'i':
			usb_interface = atoi(optarg);
			break;
		case 'p':
			usb_pid = strtol(optarg, NULL, 16);
			break;
		case 'r':
			input_filename = optarg;
			break;
		case 's':
			usb_serial = optarg;
			break;
		case 'u':
			flags |= FLAG_UNPROTECT;
			break;
		case 'v':
			usb_vid = strtol(optarg, NULL, 16);
			break;
		case 'w':
			output_filename = optarg;
			break;
		}
	}
	return flags;
}

static void sighandler(int signum)
{
	printf("\nCaught signal %d: %s\nExiting...\n",
		signum, sys_siglist[signum]);
	++exit_requested;
}

static void register_sigaction(void)
{
	struct sigaction sigact;

	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
}

int main(int argc, char **argv)
{
	struct common_hnd chnd;
	int ret = 1;
	int flags;

	/* Parse command line options */
	flags = parse_parameters(argc, argv);

	/* Open the communications channel. */
	memset(&chnd, 0, sizeof(chnd));
	if (flags & FLAG_CCD_MODE) {
		usb_findit(usb_vid, usb_pid, CR50_I2C_SUBCLASS,
			   CR50_I2C_PROTOCOL, &chnd.uep);
		chnd.iftype = CCD_IF;
		printf("Using CCD device%s\n",
		       usb_serial ? ", ignoring serial number" : "");
	} else {
		chnd.ftdi_hnd = open_ftdi_device(usb_vid, usb_pid,
						 usb_interface, usb_serial);
		if (chnd.ftdi_hnd == NULL)
			return 1;
		chnd.iftype = FTDI_IF;
	}

	/* Register signal handler after opening USB handle. */
	register_sigaction();

	/* Trigger embedded monitor detection */
	if (send_special_waveform(&chnd) < 0)
		goto terminate;

	if (chnd.iftype == FTDI_IF) {
		if (config_i2c(chnd.ftdi_hnd) < 0)
			goto terminate;
		if (check_chipid(&chnd) < 0)
			goto terminate;
	}

	if (flags & FLAG_UNPROTECT)
		command_write_unprotect(&chnd);

	if (input_filename) {
		ret = read_flash(&chnd, input_filename, 0, flash_size);
		if (ret)
			goto terminate;
	}

	if (flags & FLAG_ERASE) {
		if (is8320dx)
			/* Do Normal Erase Function */
			command_erase2(&chnd, flash_size, 0, 0);
		else
			command_erase(&chnd, flash_size, 0);
		/* Call DBGR Rest to clear the EC lock status after erasing */
		dbgr_reset(&chnd, RSTS_VCCDO_PW_ON|RSTS_HGRST|RSTS_GRST);
	}

	if (output_filename) {
		if (is8320dx)
			ret = write_flash2(&chnd, output_filename, 0);
		else
			ret = write_flash(&chnd, output_filename, 0);
		if (ret)
			goto terminate;
		ret = verify_flash(&chnd, output_filename, 0);
		if (ret)
			goto terminate;
	}

	/* Normal exit */
	ret = 0;

terminate:
	/* Enable EC Host Global Reset to reset EC resource and EC domain. */
	dbgr_reset(&chnd, RSTS_VCCDO_PW_ON|RSTS_HGRST|RSTS_GRST);

	/* Close the FTDI USB handle */
	if (chnd.iftype == FTDI_IF) {
		ftdi_usb_close(chnd.ftdi_hnd);
		ftdi_free(chnd.ftdi_hnd);
	}

	return ret;
}
