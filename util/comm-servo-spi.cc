/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/*
 * Transport using the Servo V2 SPI1 interface through the FT4232 MPSSE
 * hardware engine (driven by libftdi) in order to send host commands V3
 * directly to a MCU slave SPI controller.
 *
 * It allows to drive a MCU with the cros_ec host SPI interface directly from
 * a developer workstation or another test system.
 *
 * The USB serial number of the servo board can be passed in the 'name'
 * parameter, e.g. :
 * sudo ectool_servo --name=905537-00474 version
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libftdi1/ftdi.h>

#include "comm-host.h"
#include "cros_ec_dev.h"

/* Servo V2 SPI1 interface identifiers */
#define SERVO_V2_USB_VID 0x18d1
#define SERVO_V2_USB_PID 0x5003
#define SERVO_V2_USB_SPI1_INTERFACE INTERFACE_B

/* SPI clock frequency in Hz */
#define SPI_CLOCK_FREQ 1000000

#define FTDI_LATENCY_1MS 2

/* Timeout when waiting for the EC answer to our request */
#define RESP_TIMEOUT 2 /* second */

#ifdef DEBUG
#define debug(format, arg...) printf(format, ##arg)
#else
#define debug(...)
#endif

/* Communication context */
static struct ftdi_context ftdi;

/* Size of a MPSSE command packet */
#define MPSSE_CMD_SIZE 3

enum mpsse_commands {
	ENABLE_ADAPTIVE_CLOCK = 0x96,
	DISABLE_ADAPTIVE_CLOCK = 0x97,
	TCK_X5 = 0x8A,
	TCK_D5 = 0x8B,
	TRISTATE_IO = 0x9E,
};

enum mpsse_pins {
	SCLK = 1,
	MOSI = 2,
	MISO = 4,
	CS_L = 8,
};
/* SCLK/MOSI/CS_L are outputs, MISO is an input */
#define PINS_DIR (SCLK | MOSI | CS_L)

/* SPI mode 0:
 * propagates data on the falling edge
 * and reads data on the rising edge of the clock.
 */
#define SPI_CMD_TX (MPSSE_DO_WRITE | MPSSE_WRITE_NEG)
#define SPI_CMD_RX (MPSSE_DO_READ)
#define SPI_CMD_TXRX (MPSSE_DO_WRITE | MPSSE_DO_READ | MPSSE_WRITE_NEG)

static int raw_read(uint8_t *buf, int size)
{
	int rlen;

	while (size) {
		rlen = ftdi_read_data(&ftdi, buf, size);
		if (rlen < 0)
			break;
		buf += rlen;
		size -= rlen;
	}
	return !!size;
}

static int mpsse_set_pins(uint8_t levels)
{
	uint8_t buf[MPSSE_CMD_SIZE] = { 0 };

	buf[0] = SET_BITS_LOW;
	buf[1] = levels;
	buf[2] = PINS_DIR;

	return ftdi_write_data(&ftdi, buf, sizeof(buf)) != sizeof(buf);
}

static int send_request(int cmd, int version, const uint8_t *outdata,
			size_t outsize)
{
	uint8_t *txbuf;
	struct ec_host_request *request;
	size_t i;
	int ret = -EC_RES_ERROR;
	uint8_t csum = 0;
	size_t block_size = sizeof(struct ec_host_request) + outsize;
	size_t total_len = MPSSE_CMD_SIZE + block_size;

	txbuf = (uint8_t *)(calloc(1, total_len));
	if (!txbuf)
		return -ENOMEM;

	/* MPSSE block size is the full command minus 1 byte */
	txbuf[0] = SPI_CMD_TXRX;
	txbuf[1] = ((block_size - 1) & 0xFF);
	txbuf[2] = (((block_size - 1) >> 8) & 0xFF);

	/* Command header first */
	request = (struct ec_host_request *)(txbuf + MPSSE_CMD_SIZE);
	request->struct_version = EC_HOST_REQUEST_VERSION;
	request->checksum = 0;
	request->command = cmd;
	request->command_version = version;
	request->reserved = 0;
	request->data_len = outsize;

	/* copy the data to transmit after the command header */
	memcpy(txbuf + MPSSE_CMD_SIZE + sizeof(struct ec_host_request), outdata,
	       outsize);

	/* Compute the checksum */
	for (i = MPSSE_CMD_SIZE; i < total_len; i++)
		csum += txbuf[i];
	request->checksum = -csum;

	if (ftdi_write_data(&ftdi, txbuf, total_len) != total_len)
		goto free_request;

	if (raw_read(txbuf, block_size) != 0)
		goto free_request;

	/* Make sure the EC was listening */
	ret = 0;
	for (i = 0; i < block_size; i++) {
		switch (txbuf[i]) {
		case EC_SPI_PAST_END:
		case EC_SPI_RX_BAD_DATA:
		case EC_SPI_NOT_READY:
			ret = txbuf[i];
			/* Fall-through */
		default:
			break;
		}
		if (ret)
			break;
	}

free_request:
	free(txbuf);
	return ret;
}

static int spi_read(uint8_t *buf, size_t size)
{
	uint8_t cmd[MPSSE_CMD_SIZE];

	cmd[0] = SPI_CMD_RX;
	cmd[1] = ((size - 1) & 0xFF);
	cmd[2] = (((size - 1) >> 8) & 0xFF);

	if (ftdi_write_data(&ftdi, cmd, sizeof(cmd)) != sizeof(cmd))
		return -EC_RES_ERROR;

	return raw_read(buf, size) != 0;
}

static int get_response(uint8_t *bodydest, size_t bodylen)
{
	uint8_t sum = 0;
	size_t i;
	struct ec_host_response hdr;
	uint8_t status;
	time_t deadline = time(NULL) + RESP_TIMEOUT;

	/*
	 * Read a byte at a time until we see the start of the frame.
	 * This is slow, but often still faster than the EC.
	 */
	while (time(NULL) < deadline) {
		if (spi_read(&status, sizeof(status)))
			goto read_error;
		if (status == EC_SPI_FRAME_START)
			break;
	}
	if (status != EC_SPI_FRAME_START) {
		fprintf(stderr, "timeout wait for response\n");
		return -EC_RES_ERROR;
	}

	/* Now read the response header */
	if (spi_read((uint8_t *)(&hdr), sizeof(hdr)))
		goto read_error;

	/* Check the header */
	if (hdr.struct_version != EC_HOST_RESPONSE_VERSION) {
		fprintf(stderr, "response version %d (should be %d)\n",
			hdr.struct_version, EC_HOST_RESPONSE_VERSION);
		return -EC_RES_ERROR;
	}
	if (hdr.data_len > bodylen) {
		fprintf(stderr, "response data_len %d is > %zd\n", hdr.data_len,
			bodylen);
		return -EC_RES_ERROR;
	}

	/* Read the data if needed */
	if (hdr.data_len && spi_read(bodydest, hdr.data_len))
		goto read_error;

	/* Verify the checksum */
	for (i = 0; i < sizeof(struct ec_host_response); i++)
		sum += ((uint8_t *)&hdr)[i];
	for (i = 0; i < hdr.data_len; i++)
		sum += bodydest[i];
	if (sum) {
		fprintf(stderr, "Checksum invalid\n");
		return -EC_RES_ERROR;
	}

	return hdr.result ? -EECRESULT - hdr.result : 0;

read_error:
	fprintf(stderr, "Read failed: %s\n", ftdi_get_error_string(&ftdi));
	return -EC_RES_ERROR;
}

static int ec_command_servo_spi(int cmd, int version, const void *outdata,
				int outsize, void *indata, int insize)
{
	int ret = -EC_RES_ERROR;

	/* Set the chip select low */
	if (mpsse_set_pins(0) != 0) {
		fprintf(stderr, "Start failed: %s\n",
			ftdi_get_error_string(&ftdi));
		return -EC_RES_ERROR;
	}

	if (send_request(cmd, version, (const uint8_t *)(outdata), outsize) ==
	    0)
		ret = get_response((uint8_t *)(indata), insize);

	if (mpsse_set_pins(CS_L) != 0) {
		fprintf(stderr, "Stop failed: %s\n",
			ftdi_get_error_string(&ftdi));
		return -EC_RES_ERROR;
	}
	/* SPI protocol gap ... */
	usleep(10);

	return ret;
}

static int mpsse_set_clock(uint32_t freq)
{
	uint32_t system_clock = 0;
	uint16_t divisor = 0;
	uint8_t buf[MPSSE_CMD_SIZE] = { 0 };

	if (freq > 6000000) {
		buf[0] = TCK_X5;
		system_clock = 60000000;
	} else {
		buf[0] = TCK_D5;
		system_clock = 12000000;
	}

	if (ftdi_write_data(&ftdi, buf, 1) != 1)
		return -EC_RES_ERROR;

	divisor = (((system_clock / freq) / 2) - 1);

	buf[0] = TCK_DIVISOR;
	buf[1] = (divisor & 0xFF);
	buf[2] = ((divisor >> 8) & 0xFF);

	return ftdi_write_data(&ftdi, buf, MPSSE_CMD_SIZE) != MPSSE_CMD_SIZE;
}

static void servo_spi_close(void)
{
	ftdi_set_bitmode(&ftdi, 0, BITMODE_RESET);
	ftdi_usb_close(&ftdi);
	ftdi_deinit(&ftdi);
}

int comm_init_servo_spi(const char *device_name)
{
	int status;
	uint8_t buf[MPSSE_CMD_SIZE] = { 0 };
	/* if the user mentioned a device name, use it as serial string */
	const char *serial =
		strcmp(CROS_EC_DEV_NAME, device_name) ? device_name : NULL;

	if (ftdi_init(&ftdi))
		return -EC_RES_ERROR;
	ftdi_set_interface(&ftdi, SERVO_V2_USB_SPI1_INTERFACE);

	status = ftdi_usb_open_desc(&ftdi, SERVO_V2_USB_VID, SERVO_V2_USB_PID,
				    NULL, serial);
	if (status) {
		debug("Can't find a Servo v2 USB device\n");
		return -EC_RES_ERROR;
	}

	status |= ftdi_usb_reset(&ftdi);
	status |= ftdi_set_latency_timer(&ftdi, FTDI_LATENCY_1MS);
	status |= ftdi_set_bitmode(&ftdi, 0, BITMODE_RESET);
	if (status)
		goto err_close;

	ftdi_set_bitmode(&ftdi, 0, BITMODE_MPSSE);
	if (mpsse_set_clock(SPI_CLOCK_FREQ))
		goto err_close;

	/* Disable FTDI internal loopback */
	buf[0] = LOOPBACK_END;
	if (ftdi_write_data(&ftdi, buf, 1) != 1)
		goto err_close;
	/* Ensure adaptive clock is disabled */
	buf[0] = DISABLE_ADAPTIVE_CLOCK;
	if (ftdi_write_data(&ftdi, buf, 1) != 1)
		goto err_close;
	/* Set the idle pin states */
	if (mpsse_set_pins(CS_L) != 0)
		goto err_close;

	ec_command_proto = ec_command_servo_spi;
	/* Set temporary size, will be updated later. */
	ec_max_outsize = EC_PROTO2_MAX_PARAM_SIZE - 8;
	ec_max_insize = EC_PROTO2_MAX_PARAM_SIZE;

	return 0;

err_close:
	servo_spi_close();
	return -EC_RES_ERROR;
}
