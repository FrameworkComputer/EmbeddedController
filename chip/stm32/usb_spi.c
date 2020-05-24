/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "link_defs.h"
#include "registers.h"
#include "spi.h"
#include "usb_descriptor.h"
#include "usb_hw.h"
#include "usb_spi.h"
#include "util.h"

/* Forward declare platform specific functions. */
static bool usb_spi_received_packet(struct usb_spi_config const *config);
static void usb_spi_read_packet(struct usb_spi_config const *config,
			struct usb_spi_packet_ctx_t *packet);
static void usb_spi_write_packet(struct usb_spi_config const *config,
			struct usb_spi_packet_ctx_t *packet);

/*
 * Map EC error codes to USB_SPI error codes.
 *
 * @param error     EC error code
 *
 * @returns         USB SPI error code based on the mapping.
 */
static int16_t usb_spi_map_error(int error)
{
	switch (error) {
	case EC_SUCCESS:       return USB_SPI_SUCCESS;
	case EC_ERROR_TIMEOUT: return USB_SPI_TIMEOUT;
	case EC_ERROR_BUSY:    return USB_SPI_BUSY;
	default:               return USB_SPI_UNKNOWN_ERROR | (error & 0x7fff);
	}
}

/*
 * Read data into the receive buffer.
 *
 * @param dst       Destination receive context we are writing data to.
 * @param src       Source packet context we are reading data from.
 *
 * @returns         USB_SPI_RX_DATA_OVERFLOW if the source packet is too large
 */
static int usb_spi_read_usb_packet(struct usb_spi_transfer_ctx_t *dst,
				const struct usb_spi_packet_ctx_t *src)
{
	size_t max_read_length = dst->transfer_size - dst->transfer_index;
	size_t bytes_in_buffer = src->packet_size - src->header_size;
	const uint8_t *packet_buffer = src->bytes + src->header_size;

	if (bytes_in_buffer > max_read_length) {
		/*
		 * An error occurred, we should not receive more data than
		 * the buffer can support.
		 */
		return USB_SPI_RX_DATA_OVERFLOW;
	}
	memcpy(dst->buffer + dst->transfer_index, packet_buffer,
		bytes_in_buffer);

	dst->transfer_index += bytes_in_buffer;
	return USB_SPI_SUCCESS;
}

/*
 * Fill the USB packet with data from the transmit buffer.
 *
 * @param dst       Destination packet context we are writing data to.
 * @param src       Source transmit context we are reading data from.
 */
static void usb_spi_fill_usb_packet(struct usb_spi_packet_ctx_t *dst,
				struct usb_spi_transfer_ctx_t *src)
{
	size_t transfer_size = src->transfer_size - src->transfer_index;
	size_t max_buffer_size = USB_MAX_PACKET_SIZE - dst->header_size;
	uint8_t *packet_buffer = dst->bytes + dst->header_size;

	if (transfer_size > max_buffer_size)
		transfer_size = max_buffer_size;

	memcpy(packet_buffer, src->buffer + src->transfer_index, transfer_size);

	dst->packet_size = dst->header_size + transfer_size;
	src->transfer_index += transfer_size;
}

/*
 * Setup the USB SPI state to start a new SPI transfer.
 *
 * @param config        USB SPI config
 * @param write_count   Number of bytes to write in the SPI transfer
 * @param read_count    Number of bytes to read in the SPI transfer
 */
static void usb_spi_setup_transfer(struct usb_spi_config const *config,
				size_t write_count, size_t read_count)
{
	/* Reset any status code. */
	config->state->status_code = USB_SPI_SUCCESS;

	/* Reset the write and read counts. */
	config->state->spi_write_ctx.transfer_size = write_count;
	config->state->spi_write_ctx.transfer_index = 0;
	config->state->spi_read_ctx.transfer_size = read_count;
	config->state->spi_read_ctx.transfer_index = 0;
}

/*
 * Handle USB events that will reset the USB SPI state.
 *
 * @param config        USB SPI config
 */
static void usb_spi_reset_interface(struct usb_spi_config const *config)
{
	/* Setup a 0 byte transfer to clear the contexts. */
	usb_spi_setup_transfer(config, 0, 0);
}

/*
 * Deferred function to handle state changes, process USB SPI packets,
 * and construct responses.
 *
 * @param config        USB SPI config
 */
void usb_spi_deferred(struct usb_spi_config const *config)
{
	int enabled;
	struct usb_spi_packet_ctx_t *receive_packet =
		&config->state->receive_packet;
	struct usb_spi_packet_ctx_t *transmit_packet =
		&config->state->transmit_packet;

	if (config->flags & USB_SPI_CONFIG_FLAGS_IGNORE_HOST_SIDE_ENABLE)
		enabled = config->state->enabled_device;
	else
		enabled = config->state->enabled_device &&
			  config->state->enabled_host;

	/*
	 * If our overall enabled state has changed we call the board specific
	 * enable or disable routines and save our new state.
	 */
	if (enabled != config->state->enabled) {
		if (enabled) usb_spi_board_enable(config);
		else         usb_spi_board_disable(config);

		config->state->enabled = enabled;
	}

	/*
	 * And if there is a USB packet waiting we process it and generate a
	 * response.
	 */
	usb_spi_read_packet(config, receive_packet);
	if (receive_packet->packet_size) {
		int write_count = receive_packet->command.write_count;
		int read_count = receive_packet->command.read_count;
		int status_code = USB_SPI_SUCCESS;

		receive_packet->header_size =
			offsetof(struct usb_spi_command_v1_t, data);
		transmit_packet->header_size =
			offsetof(struct usb_spi_response_v1_t, data);

		if (!config->state->enabled) {
			status_code = USB_SPI_DISABLED;
		} else if (write_count > USB_SPI_MAX_WRITE_COUNT ||
			   write_count != (receive_packet->packet_size - 2)) {
			status_code = USB_SPI_WRITE_COUNT_INVALID;
		} else if (read_count > USB_SPI_MAX_READ_COUNT) {
			status_code = USB_SPI_READ_COUNT_INVALID;
		} else {
			usb_spi_setup_transfer(config, write_count, read_count);

			status_code = usb_spi_read_usb_packet(
					&config->state->spi_write_ctx,
					receive_packet);
		}

		/* If no error codes are present, perform the transfer. */
		if (status_code == USB_SPI_SUCCESS) {
			status_code = spi_transaction(SPI_FLASH_DEVICE,
				config->state->spi_write_ctx.buffer,
				config->state->spi_write_ctx.transfer_size,
				config->state->spi_read_ctx.buffer,
				config->state->spi_read_ctx.transfer_size);
			/* Cast the EC status code to USB SPI */
			status_code = usb_spi_map_error(status_code);
			usb_spi_fill_usb_packet(transmit_packet,
					&config->state->spi_read_ctx);
		}

		transmit_packet->response.status_code = status_code;

		usb_spi_write_packet(config, transmit_packet);
	}
}

/*
 * Sets which SPI modes will be enabled
 *
 * @param config        USB SPI config
 * @param enabled       usb_spi_request indicating which SPI mode is enabled.
 */
void usb_spi_enable(struct usb_spi_config const *config, int enabled)
{
	config->state->enabled_device = enabled;

	hook_call_deferred(config->deferred, 0);
}


/*
 * STM32 Platform: Receive the data from the endpoint into the packet and
 *  mark the endpoint as ready to accept more data.
 *
 * @param config        USB SPI config
 * @param packet        Destination packet used to store the endpoint data.
 */
static void usb_spi_read_packet(struct usb_spi_config const *config,
				struct usb_spi_packet_ctx_t *packet)
{
	size_t packet_size;

	if (!usb_spi_received_packet(config)) {
		/* No data is present on the endpoint. */
		packet->packet_size = 0;
		return;
	}

	/* Copy bytes from endpoint memory. */
	packet_size = btable_ep[config->endpoint].rx_count & RX_COUNT_MASK;
	memcpy_from_usbram(packet->bytes,
		(void *)usb_sram_addr(config->ep_rx_ram), packet_size);
	packet->packet_size = packet_size;
	/* Set endpoint as valid for accepting new packet. */
	STM32_TOGGLE_EP(config->endpoint, EP_RX_MASK, EP_RX_VALID, 0);
}

/*
 * STM32 Platform: Transmit data from the packet to the endpoint buffer.
 *  If a packet is written, the endpoint will be marked valid for transmitting.
 *
 * @param config        USB SPI config
 * @param packet        Source packet we will write to the endpoint data.
 */
static void usb_spi_write_packet(struct usb_spi_config const *config,
				struct usb_spi_packet_ctx_t *packet)
{
	if (packet->packet_size == 0)
		return;

	/* Copy bytes to endpoint memory. */
	memcpy_to_usbram((void *)usb_sram_addr(config->ep_tx_ram),
		packet->bytes, packet->packet_size);
	btable_ep[config->endpoint].tx_count = packet->packet_size;
	/* Set endpoint as valid for transmitting new packet*/
	STM32_TOGGLE_EP(config->endpoint, EP_TX_MASK, EP_TX_VALID, 0);
}

/*
 * STM32 Platform: Returns the RX endpoint status
 *
 * @param config        USB SPI config
 *
 * @returns             Returns true when the RX endpoint has a packet.
 */
static bool usb_spi_received_packet(struct usb_spi_config const *config)
{
	return (STM32_USB_EP(config->endpoint) & EP_RX_MASK) != EP_RX_VALID;
}

/*
 * STM32 Platform: Handle interrupt for USB data received.
 *
 * @param config        USB SPI config
 */
void usb_spi_rx(struct usb_spi_config const *config)
{
	STM32_TOGGLE_EP(config->endpoint, EP_RX_MASK, EP_RX_NAK, 0);

	hook_call_deferred(config->deferred, 0);
}

/*
 * STM32 Platform: Handle interrupt for USB data transmitted.
 *
 * @param config        USB SPI config
 */
void usb_spi_tx(struct usb_spi_config const *config)
{
	STM32_TOGGLE_EP(config->endpoint, EP_TX_MASK, EP_TX_NAK, 0);
}

/*
 * STM32 Platform: Handle interrupt for USB events
 *
 * @param config        USB SPI config
 * @param evt           USB event
 */
void usb_spi_event(struct usb_spi_config const *config, enum usb_ep_event evt)
{
	int endpoint;

	if (evt != USB_EVENT_RESET)
		return;

	endpoint = config->endpoint;

	usb_spi_reset_interface(config);

	btable_ep[endpoint].tx_addr  = usb_sram_addr(config->ep_tx_ram);
	btable_ep[endpoint].tx_count = 0;

	btable_ep[endpoint].rx_addr  = usb_sram_addr(config->ep_rx_ram);
	btable_ep[endpoint].rx_count =
		0x8000 | ((USB_MAX_PACKET_SIZE / 32 - 1) << 10);

	STM32_USB_EP(endpoint) = ((endpoint <<  0) | /* Endpoint Addr*/
				  (2        <<  4) | /* TX NAK */
				  (0        <<  9) | /* Bulk EP */
				  (3        << 12)); /* RX Valid */
}

/*
 * STM32 Platform: Handle control transfers.
 *
 * @param config        USB SPI config
 * @param rx_buf        Contains setup packet
 * @param tx_buf        unused
 */
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

	switch (setup.bRequest) {
	case USB_SPI_REQ_ENABLE:
		config->state->enabled_host = 1;
		break;

	case USB_SPI_REQ_DISABLE:
		config->state->enabled_host = 0;
		break;

	default: return 1;
	}

	/*
	 * Our state has changed, call the deferred function to handle the
	 * state change.
	 */
	if (!(config->flags & USB_SPI_CONFIG_FLAGS_IGNORE_HOST_SIDE_ENABLE))
		hook_call_deferred(config->deferred, 0);

	usb_spi_reset_interface(config);

	btable_ep[0].tx_count = 0;
	STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID, EP_STATUS_OUT);
	return 0;
}
