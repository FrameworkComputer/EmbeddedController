/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ccd_config.h"
#include "common.h"
#include "link_defs.h"
#include "gpio.h"
#include "registers.h"
#include "spi.h"
#include "spi_flash.h"
#include "usb_descriptor.h"
#include "usb_spi.h"
#include "util.h"

#ifdef CONFIG_STREAM_SIGNATURE
#include "signing.h"
#endif

#define CPUTS(outstr) cputs(CC_USB, outstr)
#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

static int16_t usb_spi_map_error(int error)
{
	switch (error) {
	case EC_SUCCESS:
		return USB_SPI_SUCCESS;
	case EC_ERROR_TIMEOUT:
		return USB_SPI_TIMEOUT;
	case EC_ERROR_BUSY:
		return USB_SPI_BUSY;
	default:
		return USB_SPI_UNKNOWN_ERROR | (error & 0x7fff);
	}
}

static uint16_t usb_spi_read_packet(struct usb_spi_config const *config)
{
	return QUEUE_REMOVE_UNITS(config->consumer.queue, config->buffer,
		queue_count(config->consumer.queue));
}

static void usb_spi_write_packet(struct usb_spi_config const *config,
				 uint8_t count)
{
#ifdef CONFIG_STREAM_SIGNATURE
	/*
	 * This hook allows mn50 to sign SPI data read from newly
	 * manufactured H1 devieces. The data is added to a running
	 * hash until a completion message is received.
	 */
	sig_append(stream_spi, config->buffer, count);
#endif

	QUEUE_ADD_UNITS(config->tx_queue, config->buffer, count);
}

void usb_spi_deferred(struct usb_spi_config const *config)
{
	uint16_t count;
	int write_count;
	int read_count;
	int read_length;
	uint16_t res;
	int rv = EC_SUCCESS;

	/*
	 * If our overall enabled state has changed we call the board specific
	 * enable or disable routines and save our new state.
	 */
	int enabled = !!(config->state->enabled_host &
			 config->state->enabled_device);

	if (enabled ^ config->state->enabled) {
		if (enabled)
			rv = usb_spi_board_enable(config);
		else
			usb_spi_board_disable(config);

		/* Only update our state if we were successful. */
		if (rv == EC_SUCCESS)
			config->state->enabled = enabled;
	}

	/*
	 * And if there is a USB packet waiting we process it and generate a
	 * response.
	 */
	count       = usb_spi_read_packet(config);
	write_count = config->buffer[0];
	read_count  = config->buffer[1];

	/* Handle SPI_READBACK_ALL case */
	if (read_count == 255) {
		/* Handle simultaneously clocked RX and TX */
		read_count = SPI_READBACK_ALL;
		read_length = write_count;
	} else {
		/* Normal case */
		read_length = read_count;
	}

	if (!count || (!read_count && !write_count) ||
	    (!write_count && read_count == (uint8_t)SPI_READBACK_ALL))
		return;

	if (!config->state->enabled) {
		res = USB_SPI_DISABLED;
	} else if (write_count > USB_SPI_MAX_WRITE_COUNT ||
		   write_count != (count - HEADER_SIZE)) {
		res = USB_SPI_WRITE_COUNT_INVALID;
	} else if (read_length > USB_SPI_MAX_READ_COUNT) {
		res = USB_SPI_READ_COUNT_INVALID;
	} else {
		res = usb_spi_map_error(
			spi_transaction(SPI_FLASH_DEVICE,
					config->buffer + HEADER_SIZE,
					write_count,
					config->buffer + HEADER_SIZE,
					read_count));
	}

	memcpy(config->buffer, &res, HEADER_SIZE);
	usb_spi_write_packet(config, read_length + HEADER_SIZE);
}

static void usb_spi_written(struct consumer const *consumer, size_t count)
{
	struct usb_spi_config const *config =
		DOWNCAST(consumer, struct usb_spi_config, consumer);

	hook_call_deferred(config->deferred, 0);
}

struct consumer_ops const usb_spi_consumer_ops = {
	.written = usb_spi_written,
};

void usb_spi_enable(struct usb_spi_config const *config, int enabled)
{
	config->state->enabled_device = 0;
	if (enabled) {
#ifdef CONFIG_CASE_CLOSED_DEBUG_V1
		if (ccd_is_cap_enabled(CCD_CAP_AP_FLASH))
			config->state->enabled_device |= USB_SPI_AP;
		if (ccd_is_cap_enabled(CCD_CAP_EC_FLASH))
			config->state->enabled_device |= USB_SPI_EC;
#else
		config->state->enabled_device = USB_SPI_ALL;
#endif
	}

	hook_call_deferred(config->deferred, 0);
}

