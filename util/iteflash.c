/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ITE83xx SoC in-system programming tool
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include <ftdi.h>
#pragma GCC diagnostic pop

/* default USB device : Servo v2 */
#define SERVO_USB_VID 0x18d1
#define SERVO_USB_PID 0x5002
#define SERVO_INTERFACE INTERFACE_B

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

/* Embedded flash block write size */
#define BLOCK_WRITE_SIZE	65536

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

/* Size for FTDI outgoing buffer */
#define FTDI_CMD_BUF_SIZE (1<<12)

/* store custom parameters */
const char *input_filename;
const char *output_filename;
static int usb_vid = SERVO_USB_VID;
static int usb_pid = SERVO_USB_PID;
static int usb_interface = SERVO_INTERFACE;
static char *usb_serial;
static int flash_size;

/* debug traces : default OFF*/
static int debug;

/* optional command flags */
enum {
	FLAG_UNPROTECT      = 0x01,
	FLAG_ERASE          = 0x02,
};

/* number of bytes to send consecutively before checking for ACKs */
#define TX_BUFFER_LIMIT	32

static int i2c_add_send_byte(struct ftdi_context *ftdi, uint8_t *buf,
			     uint8_t *ptr, uint8_t *tbuf, int tcnt)
{
	int ret, i, j;
	int tx_buffered = 0;
	static uint8_t ack[TX_BUFFER_LIMIT];
	uint8_t *b = ptr;
	uint8_t failed_ack = 0;

	for (i = 0; i < tcnt; i++) {
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
			ret = ftdi_read_data(ftdi, &ack[0], tx_buffered);
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
	int ret, i;
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
	ret = ftdi_read_data(ftdi, rbuf, rcnt);
	if (ret < 0)
		fprintf(stderr, "read byte failed\n");
	return ret;
}

static int i2c_byte_transfer(struct ftdi_context *ftdi, uint8_t addr,
			     uint8_t *data, int write, int numbytes)
{
	int ret = 0, rets;
	static uint8_t buf[FTDI_CMD_BUF_SIZE];
	uint8_t *b = buf;
	uint8_t slave_addr;

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

static int i2c_write_byte(struct ftdi_context *ftdi, uint8_t cmd, uint8_t data)
{
	int ret;

	ret = i2c_byte_transfer(ftdi, I2C_CMD_ADDR, &cmd, 1, 1);
	if (ret < 0)
		return -EIO;
	ret = i2c_byte_transfer(ftdi, I2C_DATA_ADDR, &data, 1, 1);
	if (ret < 0)
		return -EIO;

	return 0;
}

static int i2c_read_byte(struct ftdi_context *ftdi, uint8_t cmd, uint8_t *data)
{
	int ret;

	ret = i2c_byte_transfer(ftdi, I2C_CMD_ADDR, &cmd, 1, 1);
	if (ret < 0)
		return -EIO;
	ret = i2c_byte_transfer(ftdi, I2C_DATA_ADDR, data, 0, 1);
	if (ret < 0)
		return -EIO;

	return 0;
}

static int check_chipid(struct ftdi_context *ftdi)
{
	int ret;
	uint8_t ver = 0xff;
	uint16_t id = 0xffff;

	ret = i2c_read_byte(ftdi, 0x00, (uint8_t *)&id + 1);
	if (ret < 0)
		return ret;
	ret = i2c_read_byte(ftdi, 0x01, (uint8_t *)&id);
	if (ret < 0)
		return ret;
	ret = i2c_read_byte(ftdi, 0x02, &ver);
	if (ret < 0)
		return ret;
	if ((id & 0xff00) != (CHIP_ID & 0xff00)) {
		fprintf(stderr, "Invalid chip id: %04x\n", id);
		return -EINVAL;
	}
	/* compute embedded flash size from CHIPVER field */
	flash_size = (128 + (ver & 0xF0)) * 1024;

	printf("CHIPID %04x, CHIPVER %02x, Flash size %d kB\n", id, ver,
			flash_size / 1024);

	return 0;
}

/* DBGR Reset*/
static int dbgr_reset(struct ftdi_context *ftdi)
{
	int ret = 0;

	ret |= i2c_write_byte(ftdi, 0x27, 0x80);
	if (ret < 0)
		printf("DBGR RESET FAILED\n");

	return 0;
}

/* Do WatchDog Reset*/
static int do_watchdog_reset(struct ftdi_context *ftdi)
{
	int ret = 0;

	ret |= i2c_write_byte(ftdi, 0x2f, 0x20);
	ret |= i2c_write_byte(ftdi, 0x2e, 0x06);
	ret |= i2c_write_byte(ftdi, 0x30, 0x4C);
	ret |= i2c_write_byte(ftdi, 0x27, 0x80);

	if (ret < 0)
		printf("WATCHDOG RESET FAILED\n");

	return 0;
}

/* Enter follow mode and FSCE# high level */
static int spi_flash_follow_mode(struct ftdi_context *ftdi, char *desc)
{
	int ret = 0;

	ret |= i2c_write_byte(ftdi, 0x07, 0x7f);
	ret |= i2c_write_byte(ftdi, 0x06, 0xff);
	ret |= i2c_write_byte(ftdi, 0x05, 0xfe);
	ret |= i2c_write_byte(ftdi, 0x04, 0x00);
	ret |= i2c_write_byte(ftdi, 0x08, 0x00);

	ret = (ret ? -EIO : 0);
	if (ret < 0)
		fprintf(stderr, "Flash %s enter follow mode FAILED (%d)\n",
			desc, ret);

	return ret;
}

/* Exit follow mode */
static int spi_flash_follow_mode_exit(struct ftdi_context *ftdi, char *desc)
{
	int ret = 0;

	ret |= i2c_write_byte(ftdi, 0x07, 0x00);
	ret |= i2c_write_byte(ftdi, 0x06, 0x00);

	ret = (ret ? -EIO : 0);
	if (ret < 0)
		fprintf(stderr, "Flash %s exit follow mode FAILED (%d)\n",
			desc, ret);

	return ret;
}

/* SPI Flash generic command, short version */
static int spi_flash_command_short(struct ftdi_context *ftdi,
						uint8_t cmd,
						char *desc)
{
	int ret = 0;

	ret |= i2c_write_byte(ftdi, 0x05, 0xfe);
	ret |= i2c_write_byte(ftdi, 0x08, 0x00);
	ret |= i2c_write_byte(ftdi, 0x05, 0xfd);
	ret |= i2c_write_byte(ftdi, 0x08, cmd);

	ret = (ret ? -EIO : 0);
	if (ret < 0)
		fprintf(stderr, "Flash CMD %s FAILED (%d)\n", desc, ret);

	return ret;
}

/* SPI Flash set erase page */
static int spi_flash_set_erase_page(struct ftdi_context *ftdi,
						int page,
						char *desc)
{
	int ret = 0;

	ret |= i2c_write_byte(ftdi, 0x08, page >> 8);
	ret |= i2c_write_byte(ftdi, 0x08, page & 0xff);
	ret |= i2c_write_byte(ftdi, 0x08, 0);

	ret = (ret ? -EIO : 0);
	if (ret < 0)
		fprintf(stderr, "Flash %s set page FAILED (%d)\n", desc, ret);

	return ret;
}

/* Poll SPI Flash Read Status register until BUSY is reset */
static int spi_poll_busy(struct ftdi_context *ftdi, char *desc)
{
	uint8_t reg = 0xff;
	int ret = -EIO;

	if (spi_flash_command_short(ftdi, SPI_CMD_READ_STATUS,
		"read status for busy bit") < 0) {
		fprintf(stderr, "Flash %s wait busy cleared FAILED\n", desc);
		goto failed_read_status;
	}

	while (1) {
		if (i2c_byte_transfer(ftdi, I2C_DATA_ADDR, &reg, 0, 1) < 0) {
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

static int spi_check_write_enable(struct ftdi_context *ftdi, char *desc)
{
	uint8_t reg = 0xff;
	int ret = -EIO;

	if (spi_flash_command_short(ftdi, SPI_CMD_READ_STATUS,
		"read status for write enable bit") < 0) {
		fprintf(stderr, "Flash %s wait WE FAILED\n", desc);
		goto failed_read_status;
	}

	while (1) {
		if (i2c_byte_transfer(ftdi, I2C_DATA_ADDR, &reg, 0, 1) < 0) {
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

static int send_special_waveform(struct ftdi_context *ftdi)
{
	int ret;
	int i;
	uint64_t *wave;
	uint8_t release_lines[] = {SET_BITS_LOW, 0, 0};

	wave = malloc(SPECIAL_BUFFER_SIZE);

	printf("Waiting for the EC power-on sequence ...");
	fflush(stdout);

retry:
	/* Reset the FTDI into a known state */
	ret = ftdi_set_bitmode(ftdi, 0xFF, BITMODE_RESET);
	if (ret != 0) {
		fprintf(stderr, "failed to reset FTDI\n");
		goto special_failed;
	}

	/*
	 * set the clock divider,
	 * so we output a new bitbang value every 2.5us.
	 */
	ret = ftdi_set_baudrate(ftdi, 160000);
	if (ret != 0) {
		fprintf(stderr, "failed to set bitbang clock\n");
		goto special_failed;
	}

	/* Enable asynchronous bit-bang mode */
	ret = ftdi_set_bitmode(ftdi, 0xFF, BITMODE_BITBANG);
	if (ret != 0) {
		fprintf(stderr, "failed to set bitbang mode\n");
		goto special_failed;
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

	/* clean everything to go back to regular I2C communication */
	ftdi_usb_purge_buffers(ftdi);
	ftdi_set_bitmode(ftdi, 0xff, BITMODE_RESET);
	config_i2c(ftdi);
	ftdi_write_data(ftdi, release_lines, sizeof(release_lines));

	/* wait for PLL stable for 5ms (plus remaining USB transfers) */
	usleep(10 * MSEC);

	/* if we cannot communicate, retry the sequence */
	if (check_chipid(ftdi) < 0) {
		sleep(1);
		goto retry;
	}
special_failed:
	printf("Done.\n");
	free(wave);
	return ret;
}

static int windex;
static const char wheel[] = {'|', '/', '-', '\\' };
static void draw_spinner(uint32_t remaining, uint32_t size)
{
	int percent = (size - remaining)*100/size;
	printf("\r%c%3d%%", wheel[windex++], percent);
	windex %= sizeof(wheel);
}

int command_read_pages(struct ftdi_context *ftdi, uint32_t address,
		       uint32_t size, uint8_t *buffer)
{
	int res = -EIO;
	uint32_t remaining = size;
	int cnt;
	uint16_t page;

	if (spi_flash_follow_mode(ftdi, "fast read") < 0)
		goto failed_read;

	while (remaining) {
		uint8_t cmd = 0x9;

		cnt = (remaining > PAGE_SIZE) ? PAGE_SIZE : remaining;
		page = address / PAGE_SIZE;

		draw_spinner(remaining, size);

		/* Fast Read command */
		if (spi_flash_command_short(ftdi, SPI_CMD_FAST_READ,
			"fast read") < 0)
			goto failed_read;
		res = i2c_write_byte(ftdi, 0x08, page >> 8);
		res += i2c_write_byte(ftdi, 0x08, page & 0xff);
		res += i2c_write_byte(ftdi, 0x08, 0x00);
		res += i2c_write_byte(ftdi, 0x08, 0x00);
		if (res < 0) {
			fprintf(stderr, "page address set failed\n");
			goto failed_read;
		}

		/* read page data */
		res = i2c_byte_transfer(ftdi, I2C_CMD_ADDR, &cmd, 1, 1);
		res = i2c_byte_transfer(ftdi, I2C_BLOCK_ADDR, buffer, 0, cnt);
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
	if (spi_flash_follow_mode_exit(ftdi, "fast read") < 0)
		res = -EIO;

	return res;
}

int command_write_pages(struct ftdi_context *ftdi, uint32_t address,
			uint32_t size, uint8_t *buffer)
{
	int res = -EIO;
	uint32_t remaining = size;
	int cnt;
	uint8_t page;
	uint8_t cmd;

	if (spi_flash_follow_mode(ftdi, "AAI write") < 0)
		goto failed_write;

	while (remaining) {
		cnt = (remaining > BLOCK_WRITE_SIZE) ?
				BLOCK_WRITE_SIZE : remaining;
		page = address / BLOCK_WRITE_SIZE;

		draw_spinner(remaining, size);

		/* Write enable */
		if (spi_flash_command_short(ftdi, SPI_CMD_WRITE_ENABLE,
			"write enable for AAI write") < 0)
			goto failed_write;

		/* Check write enable bit */
		if (spi_check_write_enable(ftdi, "AAI write") < 0)
			goto failed_write;

		/* Setup write */
		if (spi_flash_command_short(ftdi, SPI_CMD_WORD_PROGRAM,
			"AAI write") < 0)
			goto failed_write;

		/* Set page */
		cmd = 0;
		res = i2c_byte_transfer(ftdi, I2C_DATA_ADDR, &page, 1, 1);
		res |= i2c_byte_transfer(ftdi, I2C_DATA_ADDR, &cmd, 1, 1);
		res |= i2c_byte_transfer(ftdi, I2C_DATA_ADDR, &cmd, 1, 1);
		if (res < 0) {
			fprintf(stderr, "Flash write set page FAILED (%d)\n",
					res);
			goto failed_write;
		}

		/* Wait until not busy */
		if (spi_poll_busy(ftdi, "AAI write") < 0)
			goto failed_write;

		/* Write up to BLOCK_WRITE_SIZE data */
		res = i2c_write_byte(ftdi, 0x10, 0x20);
		res = i2c_byte_transfer(ftdi, I2C_BLOCK_ADDR, buffer, 1, cnt);
		buffer += cnt;

		if (res < 0) {
			fprintf(stderr, "Flash data write failed\n");
			goto failed_write;
		}

		cmd = 0xff;
		res = i2c_byte_transfer(ftdi, I2C_DATA_ADDR, &cmd, 1, 1);
		res |= i2c_write_byte(ftdi, 0x10, 0x00);
		if (res < 0) {
			fprintf(stderr, "Flash end data write FAILED (%d)\n",
					res);
			goto failed_write;
		}

		/* Write disable */
		if (spi_flash_command_short(ftdi, SPI_CMD_WRITE_DISABLE,
			"write disable for AAI write") < 0)
			goto failed_write;

		/* Wait until available */
		if (spi_poll_busy(ftdi, "write disable for AAI write") < 0)
			goto failed_write;

		address += cnt;
		remaining -= cnt;
	}
	/* No error so far */
	res = size;
failed_write:
	if (spi_flash_command_short(ftdi, SPI_CMD_WRITE_DISABLE,
		"write disable exit AAI write") < 0)
		res = -EIO;

	if (spi_flash_follow_mode_exit(ftdi, "AAI write") < 0)
		res = -EIO;

	return res;
}

int command_write_unprotect(struct ftdi_context *ftdi)
{
	/* TODO(http://crosbug.com/p/23576): implement me */
	return 0;
}

int command_erase(struct ftdi_context *ftdi, uint32_t len, uint32_t off)
{
	int res = -EIO;
	int page = 0;
	uint32_t remaining = len;

	printf("Erasing chip...\n");

	if (off != 0 || len != flash_size) {
		fprintf(stderr, "Only full chip erase is supported\n");
		return -EINVAL;
	}

	if (spi_flash_follow_mode(ftdi, "erase") < 0)
		goto failed_erase;

	while (remaining) {
		draw_spinner(remaining, len);

		if (spi_flash_command_short(ftdi, SPI_CMD_WRITE_ENABLE,
			"write enable for erase") < 0)
			goto failed_erase;

		if (spi_check_write_enable(ftdi, "erase") < 0)
			goto failed_erase;

		/* do chip erase */
		if (remaining == flash_size) {
			if (spi_flash_command_short(ftdi, SPI_CMD_CHIP_ERASE,
				"chip erase") < 0)
				goto failed_erase;
			goto wait_busy_cleared;
		}

		/* do sector erase */
		if (spi_flash_command_short(ftdi, SPI_CMD_SECTOR_ERASE,
			"sector erase") < 0)
			goto failed_erase;

		if (spi_flash_set_erase_page(ftdi, page, "sector erase") < 0)
			goto failed_erase;

wait_busy_cleared:
		if (spi_poll_busy(ftdi, "erase") < 0)
			goto failed_erase;

		if (spi_flash_command_short(ftdi, SPI_CMD_WRITE_DISABLE,
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
	res = 0;
failed_erase:
	if (spi_flash_command_short(ftdi, SPI_CMD_WRITE_DISABLE,
		"write disable exit erase") < 0)
		res = -EIO;

	if (spi_flash_follow_mode_exit(ftdi, "erase") < 0)
		res = -EIO;

	printf("\n");

	return res;
}

/* Return zero on success, a negative error value on failures. */
int read_flash(struct ftdi_context *ftdi, const char *filename,
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
	res = command_read_pages(ftdi, offset, size, buffer);
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
int write_flash(struct ftdi_context *ftdi, const char *filename,
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
	written = command_write_pages(ftdi, offset, res, buffer);
	if (written != res) {
		fprintf(stderr, "Error writing to flash\n");
		free(buffer);
		return -EIO;
	}
	printf("\rDone.\n");

	free(buffer);
	return 0;
}

/* Return zero on success, a negative error value on failures. */
int verify_flash(struct ftdi_context *ftdi, const char *filename,
		uint32_t offset)
{
	int res;
	int file_size;
	FILE *hnd;
	uint8_t *buffer  = malloc(flash_size);
	uint8_t *buffer2 = malloc(flash_size);

	if (!buffer || !buffer2) {
		fprintf(stderr, "Cannot allocate %d bytes\n", flash_size);
		return -ENOMEM;
	}

	hnd = fopen(filename, "r");
	if (!hnd) {
		fprintf(stderr, "Cannot open file %s for reading\n", filename);
		res = -EIO;
		goto exit;
	}

	file_size = fread(buffer, 1, flash_size, hnd);
	if (file_size <= 0) {
		fprintf(stderr, "Cannot read %s\n", filename);
		res = -EIO;
		goto exit;
	}
	fclose(hnd);

	printf("Verify %d bytes at 0x%08x\n", file_size, offset);
	res = command_read_pages(ftdi, offset, flash_size, buffer2);
	draw_spinner(flash_size-res, flash_size);
	res = memcmp(buffer, buffer2, file_size);
	if (res != 0) {
		fprintf(stderr, "Verify Error!! ");
		goto exit;
	}

	printf("\n\rVerify Done.\n");
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
	{"debug", 0, 0, 'd'},
	{"product", 1, 0, 'p'},
	{"vendor", 1, 0, 'v'},
	{"interface", 1, 0, 'i'},
	{"serial", 1, 0, 's'},
	{"read", 1, 0, 'r'},
	{"write", 1, 0, 'w'},
	{"erase", 0, 0, 'e'},
	{"help", 0, 0, 'h'},
	{"unprotect", 0, 0, 'u'},
	{NULL, 0, 0, 0}
};

void display_usage(char *program)
{
	fprintf(stderr, "Usage: %s [-d] [-v <VID>] [-p <PID>] [-i <1|2>] "
		"[-s <serial>] [-u] [-e] [-r <file>] [-w <file>]\n", program);
	fprintf(stderr, "--d[ebug] : output debug traces\n");
	fprintf(stderr, "--v[endor] <0x1234> : USB vendor ID\n");
	fprintf(stderr, "--p[roduct] <0x1234> : USB product ID\n");
	fprintf(stderr, "--s[erial] <serialname> : USB serial string\n");
	fprintf(stderr, "--i[interface] <1> : FTDI interface: A=1, B=2, ...\n");
	fprintf(stderr, "--u[nprotect] : remove flash write protect\n");
	fprintf(stderr, "--e[rase] : erase all the flash content\n");
	fprintf(stderr, "--r[ead] <file> : read the flash content and "
			"write it into <file>\n");
	fprintf(stderr, "--w[rite] <file> : read <file> and "
			"write it to flash\n");

	exit(2);
}

int parse_parameters(int argc, char **argv)
{
	int opt, idx;
	int flags = 0;

	while ((opt = getopt_long(argc, argv, "dv:p:i:s:ehr:w:u?",
				  longopts, &idx)) != -1) {
		switch (opt) {
		case 'd':
			debug = 1;
			break;
		case 'v':
			usb_vid = strtol(optarg, NULL, 16);
			break;
		case 'p':
			usb_pid = strtol(optarg, NULL, 16);
			break;
		case 'i':
			usb_interface = atoi(optarg);
			break;
		case 's':
			usb_serial = optarg;
			break;
		case 'e':
			flags |= FLAG_ERASE;
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
	void *hnd;
	int ret = 1;
	int flags;

	/* Parse command line options */
	flags = parse_parameters(argc, argv);

	/* Open the USB device */
	hnd = open_ftdi_device(usb_vid, usb_pid, usb_interface, usb_serial);
	if (hnd == NULL)
		return 1;

	/* Trigger embedded monitor detection */
	if (send_special_waveform(hnd) < 0)
		goto terminate;

	if (config_i2c(hnd) < 0)
		goto terminate;

	if (check_chipid(hnd) < 0)
		goto terminate;

	if (flags & FLAG_UNPROTECT)
		command_write_unprotect(hnd);

	if (flags & FLAG_ERASE || output_filename) {
		command_erase(hnd, flash_size, 0);

		/* Call DBGR Rest to clear the EC lock status */
		dbgr_reset(hnd);
	}

	if (input_filename) {
		ret = read_flash(hnd, input_filename, 0, flash_size);
		if (ret)
			goto terminate;
	}

	if (output_filename) {
		ret = write_flash(hnd, output_filename, 0);
		if (ret)
			goto terminate;

		ret = verify_flash(hnd, output_filename, 0);
		if (ret)
			goto terminate;
	}

	/* Normal exit */
	ret = 0;
terminate:

	/* DO EC WATCHDOG RESET */
	do_watchdog_reset(hnd);

	/* Close the FTDI USB handle */
	ftdi_usb_close(hnd);
	ftdi_free(hnd);
	return ret;
}
