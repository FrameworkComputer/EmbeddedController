/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "link_defs.h"
#include "registers.h"
#include "i2c.h"
#include "usb_descriptor.h"
#include "usb_i2c.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)


static int16_t usb_i2c_map_error(int error)
{
	switch (error) {
	case EC_SUCCESS:       return USB_I2C_SUCCESS;
	case EC_ERROR_TIMEOUT: return USB_I2C_TIMEOUT;
	case EC_ERROR_BUSY:    return USB_I2C_BUSY;
	default:               return USB_I2C_UNKNOWN_ERROR | (error & 0x7fff);
	}
}

static uint8_t usb_i2c_read_packet(struct usb_i2c_config const *config)
{
	size_t  i;
	uint16_t bytes = btable_ep[config->endpoint].rx_count & 0x3ff;
	size_t   count = MAX((bytes + 1) / 2, USB_MAX_PACKET_SIZE / 2);

	/*
	 * The USB peripheral doesn't support DMA access to its packet
	 * RAM so we have to copy messages out into a bounce buffer.
	 */
	for (i = 0; i < count; ++i)
		config->buffer[i] = config->rx_ram[i];

	/*
	 * RX packet consumed, mark the packet as VALID.  The master
	 * could queue up the next command while we process this I2C
	 * transaction and prepare the response.
	 */
	STM32_TOGGLE_EP(config->endpoint, EP_RX_MASK, EP_RX_VALID, 0);

	return bytes;
}

static void usb_i2c_write_packet(struct usb_i2c_config const *config,
				 uint8_t count)
{
	size_t  i;
	/* count is bounds checked in usb_i2c_deferred. */

	/*
	 * Copy read bytes and status back out of bounce buffer and
	 * update TX packet state (mark as VALID for master to read).
	 */
	for (i = 0; i < (count + 1) / 2; ++i)
		config->tx_ram[i] = config->buffer[i];

	btable_ep[config->endpoint].tx_count = count;

	STM32_TOGGLE_EP(config->endpoint, EP_TX_MASK, EP_TX_VALID, 0);
}

static int rx_valid(struct usb_i2c_config const *config)
{
	return (STM32_USB_EP(config->endpoint) & EP_RX_MASK) == EP_RX_VALID;
}

void usb_i2c_deferred(struct usb_i2c_config const *config)
{
	/*
	 * And if there is a USB packet waiting we process it and generate a
	 * response.
	 */
	if (!rx_valid(config)) {
		uint16_t count      = usb_i2c_read_packet(config);
		int portindex       = (config->buffer[0] >> 0) & 0xff;

		/* Convert 7-bit slave address to stm32 8-bit address. */
		uint8_t slave_addr  = (config->buffer[0] >> 7) & 0xfe;
		int write_count     = (config->buffer[1] >> 0) & 0xff;
		int read_count      = (config->buffer[1] >> 8) & 0xff;
		int port = 0;

		config->buffer[0] = 0;
		config->buffer[1] = 0;

		if (write_count > USB_I2C_MAX_WRITE_COUNT ||
		    write_count != (count - 4)) {
			config->buffer[0] = USB_I2C_WRITE_COUNT_INVALID;
		} else if (read_count > USB_I2C_MAX_READ_COUNT) {
			config->buffer[0] = USB_I2C_READ_COUNT_INVALID;
		} else if (portindex >= i2c_ports_used) {
			config->buffer[0] = USB_I2C_PORT_INVALID;
		} else {
			port = i2c_ports[portindex].port;
			config->buffer[0] = usb_i2c_map_error(
				i2c_xfer(port, slave_addr,
					(uint8_t *)(config->buffer + 2),
					write_count,
					(uint8_t *)(config->buffer + 2),
					read_count, I2C_XFER_SINGLE));
		}

		usb_i2c_write_packet(config, read_count + 4);
	}
}

void usb_i2c_tx(struct usb_i2c_config const *config)
{
	STM32_TOGGLE_EP(config->endpoint, EP_TX_MASK, EP_TX_NAK, 0);
}

void usb_i2c_rx(struct usb_i2c_config const *config)
{
	STM32_TOGGLE_EP(config->endpoint, EP_RX_MASK, EP_RX_NAK, 0);

	hook_call_deferred(config->deferred, 0);
}

void usb_i2c_reset(struct usb_i2c_config const *config)
{
	int endpoint = config->endpoint;

	btable_ep[endpoint].tx_addr  = usb_sram_addr(config->tx_ram);
	btable_ep[endpoint].tx_count = 0;

	btable_ep[endpoint].rx_addr  = usb_sram_addr(config->rx_ram);
	btable_ep[endpoint].rx_count =
		0x8000 | ((USB_MAX_PACKET_SIZE / 32 - 1) << 10);

	STM32_USB_EP(endpoint) = ((endpoint <<  0) | /* Endpoint Addr*/
				  (2        <<  4) | /* TX NAK */
				  (0        <<  9) | /* Bulk EP */
				  (3        << 12)); /* RX Valid */
}
