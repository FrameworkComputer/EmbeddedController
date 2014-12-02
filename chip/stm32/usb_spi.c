/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "link_defs.h"
#include "registers.h"
#include "spi.h"
#include "usb.h"
#include "usb_spi.h"

static int16_t usb_spi_map_error(int error)
{
	switch (error) {
	case EC_SUCCESS:       return USB_SPI_SUCCESS;
	case EC_ERROR_TIMEOUT: return USB_SPI_TIMEOUT;
	case EC_ERROR_BUSY:    return USB_SPI_BUSY;
	default:               return USB_SPI_UNKNOWN_ERROR | (error & 0x7fff);
	}
}

static uint8_t usb_spi_read_packet(struct usb_spi_config const *config)
{
	size_t  i;
	uint8_t count = btable_ep[config->endpoint].rx_count & 0x3ff;

	/*
	 * The USB peripheral doesn't support DMA access to its packet
	 * RAM so we have to copy messages out into a bounce buffer.
	 */
	for (i = 0; i < (count + 1) / 2; ++i)
		config->buffer[i] = config->rx_ram[i];

	/*
	 * RX packet consumed, mark the packet as VALID.  The master
	 * could queue up the next command while we process this SPI
	 * transaction and prepare the response.
	 */
	STM32_TOGGLE_EP(config->endpoint, EP_RX_MASK, EP_RX_VALID, 0);

	return count;
}

static void usb_spi_write_packet(struct usb_spi_config const *config,
				 uint8_t count)
{
	size_t  i;

	/*
	 * Copy read bytes and status back out of bounce buffer and
	 * update TX packet state (mark as VALID for master to read).
	 */
	for (i = 0; i < (count + 1) / 2; ++i)
		config->tx_ram[i] = config->buffer[i];

	btable_ep[config->endpoint].tx_count = count;

	STM32_TOGGLE_EP(config->endpoint, EP_TX_MASK, EP_TX_VALID, 0);
}

static int rx_valid(struct usb_spi_config const *config)
{
	return (STM32_USB_EP(config->endpoint) & EP_RX_MASK) == EP_RX_VALID;
}

int usb_spi_service_request(struct usb_spi_config const *config)
{
	uint8_t count;
	uint8_t write_count;
	uint8_t read_count;

	if (rx_valid(config))
		return 0;

	count       = usb_spi_read_packet(config);
	write_count = (config->buffer[0] >> 0) & 0xff;
	read_count  = (config->buffer[0] >> 8) & 0xff;

	if (config->state->disabled || !config->state->enabled) {
		config->buffer[0] = USB_SPI_DISABLED;
	} else if (write_count > USB_SPI_MAX_WRITE_COUNT ||
		   write_count != (count - 2)) {
		config->buffer[0] = USB_SPI_WRITE_COUNT_INVALID;
	} else if (read_count > USB_SPI_MAX_READ_COUNT) {
		config->buffer[0] = USB_SPI_READ_COUNT_INVALID;
	} else {
		config->buffer[0] = usb_spi_map_error(
			spi_transaction((uint8_t *)(config->buffer + 1),
					write_count,
					(uint8_t *)(config->buffer + 1),
					read_count));
	}

	usb_spi_write_packet(config, read_count + 2);

	return 1;
}

void usb_spi_tx(struct usb_spi_config const *config)
{
	STM32_TOGGLE_EP(config->endpoint, EP_TX_MASK, EP_TX_NAK, 0);
}

void usb_spi_rx(struct usb_spi_config const *config)
{
	STM32_TOGGLE_EP(config->endpoint, EP_RX_MASK, EP_RX_NAK, 0);
	config->ready(config);
}

void usb_spi_reset(struct usb_spi_config const *config)
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

int usb_spi_interface(struct usb_spi_config const *config,
		      usb_uint *rx_buf,
		      usb_uint *tx_buf)
{
	struct usb_setup_packet setup;

	usb_read_setup_packet(rx_buf, &setup);

	if (setup.bmRequestType != (USB_DIR_OUT |
				    USB_TYPE_VENDOR |
				    USB_RECIP_INTERFACE))
		return 1;

	if (setup.wValue  != 0 ||
	    setup.wIndex  != config->interface ||
	    setup.wLength != 0)
		return 1;

	if (config->state->disabled)
		return 1;

	switch (setup.bRequest) {
	case USB_SPI_REQ_ENABLE:
		usb_spi_board_enable(config);
		config->state->enabled = 1;
		break;

	case USB_SPI_REQ_DISABLE:
		config->state->enabled = 0;
		usb_spi_board_disable(config);
		break;

	default: return 1;
	}

	btable_ep[0].tx_count = 0;
	STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID, EP_STATUS_OUT);
	return 0;
}

void usb_spi_enable(struct usb_spi_config const *config, int enabled)
{
	config->state->disabled = !enabled;

	if (config->state->disabled &&
	    config->state->enabled) {
		config->state->enabled = 0;
		usb_spi_board_disable(config);
	}
}
