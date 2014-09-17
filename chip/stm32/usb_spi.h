/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef CHIP_STM32_USB_SPI_H
#define CHIP_STM32_USB_SPI_H

/* STM32 USB SPI driver for Chrome EC */

#include "compile_time_macros.h"
#include "usb.h"

/*
 * Command:
 *     +------------------+-----------------+------------------------+
 *     | write count : 1B | read count : 1B | write payload : <= 62B |
 *     +------------------+-----------------+------------------------+
 *
 *     write count:   1 byte, zero based count of bytes to write
 *
 *     read count:    1 byte, zero based count of bytes to read
 *
 *     write payload: up to 62 bytes of data to write, length must match
 *                    write count
 *
 * Response:
 *     +-------------+-----------------------+
 *     | status : 2B | read payload : <= 62B |
 *     +-------------+-----------------------+
 *
 *     status: 2 byte status
 *         0x0000: Success
 *         0x0001: SPI timeout
 *         0x0002: Busy, try again
 *             This can happen if someone else has acquired the shared memory
 *             buffer that the SPI driver uses as /dev/null
 *         0x0003: Write count invalid (> 62 bytes, or mismatch with payload)
 *         0x0004: Read count invalid (> 62 bytes)
 *         0x8000: Unknown error mask
 *             The bottom 15 bits will contain the bottom 15 bits from the EC
 *             error code.
 *
 *     read payload: up to 62 bytes of data read from SPI, length will match
 *                   requested read count
 */

enum usb_spi_error {
	usb_spi_success             = 0x0000,
	usb_spi_timeout             = 0x0001,
	usb_spi_busy                = 0x0002,
	usb_spi_write_count_invalid = 0x0003,
	usb_spi_read_count_invalid  = 0x0004,
	usb_spi_unknown_error       = 0x8000,
};

#define USB_SPI_MAX_WRITE_COUNT 62
#define USB_SPI_MAX_READ_COUNT  62

BUILD_ASSERT(USB_MAX_PACKET_SIZE == (1 + 1 + USB_SPI_MAX_WRITE_COUNT));
BUILD_ASSERT(USB_MAX_PACKET_SIZE == (2 + USB_SPI_MAX_READ_COUNT));

/*
 * Compile time Per-USB gpio configuration stored in flash.  Instances of this
 * structure are provided by the user of the USB gpio.  This structure binds
 * together all information required to operate a USB gpio.
 */
struct usb_spi_config {
	/*
	 * Endpoint index, and pointers to the USB packet RAM buffers.
	 */
	int endpoint;

	/*
	 * Pointers to USB packet RAM and bounce buffer.
	 */
	uint16_t *buffer;
	usb_uint *rx_ram;
	usb_uint *tx_ram;

	/*
	 * Callback to notify managing task that a new SPI request has been
	 * received.  This should wake up a task that will eventually call
	 * the usb_spi_service_request function.
	 */
	void (*ready)(struct usb_spi_config const *config);
};

/*
 * Convenience macro for defining a USB SPI bridge driver.
 *
 * NAME is used to construct the names of the trampoline functions and the
 * usb_spi_config struct, the latter is just called NAME.
 *
 * INTERFACE is the index of the USB interface to associate with this
 * SPI driver.
 *
 * ENDPOINT is the index of the USB bulk endpoint used for receiving and
 * transmitting bytes.
 *
 * READY callback function for command reception notification.
 */
#define USB_SPI_CONFIG(NAME,						\
		       INTERFACE,					\
		       ENDPOINT,					\
		       READY)						\
	static uint16_t CONCAT2(NAME, _buffer)[USB_MAX_PACKET_SIZE / 2]; \
	static usb_uint CONCAT2(NAME, _ep_rx_buffer)[USB_MAX_PACKET_SIZE / 2] __usb_ram; \
	static usb_uint CONCAT2(NAME, _ep_tx_buffer)[USB_MAX_PACKET_SIZE / 2] __usb_ram; \
	struct usb_spi_config const NAME = {				\
		.endpoint  = ENDPOINT,					\
		.buffer    = CONCAT2(NAME, _buffer),			\
		.rx_ram    = CONCAT2(NAME, _ep_rx_buffer),		\
		.tx_ram    = CONCAT2(NAME, _ep_tx_buffer),		\
		.ready     = READY,					\
	};								\
	const struct usb_interface_descriptor				\
	USB_IFACE_DESC(INTERFACE) = {					\
		.bLength            = USB_DT_INTERFACE_SIZE,		\
		.bDescriptorType    = USB_DT_INTERFACE,			\
		.bInterfaceNumber   = INTERFACE,			\
		.bAlternateSetting  = 0,				\
		.bNumEndpoints      = 2,				\
		.bInterfaceClass    = USB_CLASS_VENDOR_SPEC,		\
		.bInterfaceSubClass = 0,				\
		.bInterfaceProtocol = 0,				\
		.iInterface         = 0,				\
	};								\
	const struct usb_endpoint_descriptor				\
	USB_EP_DESC(INTERFACE, 0) = {					\
		.bLength          = USB_DT_ENDPOINT_SIZE,		\
		.bDescriptorType  = USB_DT_ENDPOINT,			\
		.bEndpointAddress = 0x80 | ENDPOINT,			\
		.bmAttributes     = 0x02 /* Bulk IN */,			\
		.wMaxPacketSize   = USB_MAX_PACKET_SIZE,		\
		.bInterval        = 10,					\
	};								\
	const struct usb_endpoint_descriptor				\
	USB_EP_DESC(INTERFACE, 1) = {					\
		.bLength          = USB_DT_ENDPOINT_SIZE,		\
		.bDescriptorType  = USB_DT_ENDPOINT,			\
		.bEndpointAddress = ENDPOINT,				\
		.bmAttributes     = 0x02 /* Bulk OUT */,		\
		.wMaxPacketSize   = USB_MAX_PACKET_SIZE,		\
		.bInterval        = 0,					\
	};								\
	static void CONCAT2(NAME, _ep_tx)   (void) { usb_spi_tx   (&NAME); } \
	static void CONCAT2(NAME, _ep_rx)   (void) { usb_spi_rx   (&NAME); } \
	static void CONCAT2(NAME, _ep_reset)(void) { usb_spi_reset(&NAME); } \
	USB_DECLARE_EP(ENDPOINT,					\
		       CONCAT2(NAME, _ep_tx),				\
		       CONCAT2(NAME, _ep_rx),				\
		       CONCAT2(NAME, _ep_reset));

/*
 * Check for a new request and process it synchronously, the SPI transaction
 * will complete before this function returns.
 *
 * Returns:
 *     1: request serviced
 *     0: no request waiting
 */
int usb_spi_service_request(struct usb_spi_config const *config);

/*
 * These functions are used by the trampoline functions defined above to
 * connect USB endpoint events with the generic USB GPIO driver.
 */
void usb_spi_tx(struct usb_spi_config const *config);
void usb_spi_rx(struct usb_spi_config const *config);
void usb_spi_reset(struct usb_spi_config const *config);

#endif /* CHIP_STM32_USB_SPI_H */
