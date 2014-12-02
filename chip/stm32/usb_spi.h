/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef CHIP_STM32_USB_SPI_H
#define CHIP_STM32_USB_SPI_H

/* STM32 USB SPI driver for Chrome EC */

#include "compile_time_macros.h"
#include "hooks.h"
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
 *         0x0005: The SPI bridge is disabled.
 *         0x8000: Unknown error mask
 *             The bottom 15 bits will contain the bottom 15 bits from the EC
 *             error code.
 *
 *     read payload: up to 62 bytes of data read from SPI, length will match
 *                   requested read count
 */

enum usb_spi_error {
	USB_SPI_SUCCESS             = 0x0000,
	USB_SPI_TIMEOUT             = 0x0001,
	USB_SPI_BUSY                = 0x0002,
	USB_SPI_WRITE_COUNT_INVALID = 0x0003,
	USB_SPI_READ_COUNT_INVALID  = 0x0004,
	USB_SPI_DISABLED            = 0x0005,
	USB_SPI_UNKNOWN_ERROR       = 0x8000,
};

enum usb_spi_request {
	USB_SPI_REQ_ENABLE  = 0x0000,
	USB_SPI_REQ_DISABLE = 0x0001,
};

#define USB_SPI_MAX_WRITE_COUNT 62
#define USB_SPI_MAX_READ_COUNT  62

BUILD_ASSERT(USB_MAX_PACKET_SIZE == (1 + 1 + USB_SPI_MAX_WRITE_COUNT));
BUILD_ASSERT(USB_MAX_PACKET_SIZE == (2 + USB_SPI_MAX_READ_COUNT));

struct usb_spi_state {
	/*
	 * The SPI bridge must be both not disabled and enabled to allow access
	 * to the SPI device.  The disabled bit is dictated by the caller of
	 * usb_spi_enable.  The enabled bit is set by the USB host, most likely
	 * flashrom, by sending a USB_SPI_REQ_ENABLE message to the device.
	 */
	int disabled;
	int enabled;
};

/*
 * Compile time Per-USB gpio configuration stored in flash.  Instances of this
 * structure are provided by the user of the USB gpio.  This structure binds
 * together all information required to operate a USB gpio.
 */
struct usb_spi_config {
	/*
	 * In RAM state of the USB SPI bridge.
	 */
	struct usb_spi_state *state;

	/*
	 * Interface and endpoint indicies.
	 */
	int interface;
	int endpoint;

	/*
	 * Deferred function to call to handle SPI request.
	 */
	void (*deferred)(void);

	/*
	 * Pointers to USB packet RAM and bounce buffer.
	 */
	uint16_t *buffer;
	usb_uint *rx_ram;
	usb_uint *tx_ram;
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
 */
#define USB_SPI_CONFIG(NAME,						\
		       INTERFACE,					\
		       ENDPOINT)					\
	static uint16_t CONCAT2(NAME, _buffer_)[USB_MAX_PACKET_SIZE / 2]; \
	static usb_uint CONCAT2(NAME, _ep_rx_buffer_)[USB_MAX_PACKET_SIZE / 2] __usb_ram; \
	static usb_uint CONCAT2(NAME, _ep_tx_buffer_)[USB_MAX_PACKET_SIZE / 2] __usb_ram; \
	static void CONCAT2(NAME, _deferred_)(void);			\
	struct usb_spi_state CONCAT2(NAME, _state_) = {			\
		.disabled = 1,						\
		.enabled  = 0,						\
	};								\
	struct usb_spi_config const NAME = {				\
		.state     = &CONCAT2(NAME, _state_),			\
		.interface = INTERFACE,					\
		.endpoint  = ENDPOINT,					\
		.deferred  = CONCAT2(NAME, _deferred_),			\
		.buffer    = CONCAT2(NAME, _buffer_),			\
		.rx_ram    = CONCAT2(NAME, _ep_rx_buffer_),		\
		.tx_ram    = CONCAT2(NAME, _ep_tx_buffer_),		\
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
	static void CONCAT2(NAME, _ep_tx_)   (void) { usb_spi_tx   (&NAME); } \
	static void CONCAT2(NAME, _ep_rx_)   (void) { usb_spi_rx   (&NAME); } \
	static void CONCAT2(NAME, _ep_reset_)(void) { usb_spi_reset(&NAME); } \
	USB_DECLARE_EP(ENDPOINT,					\
		       CONCAT2(NAME, _ep_tx_),				\
		       CONCAT2(NAME, _ep_rx_),				\
		       CONCAT2(NAME, _ep_reset_));			\
	static int CONCAT2(NAME, _interface_)(usb_uint *rx_buf,		\
					      usb_uint *tx_buf)		\
	{ return usb_spi_interface(&NAME, rx_buf, tx_buf); }		\
	USB_DECLARE_IFACE(INTERFACE,					\
			  CONCAT2(NAME, _interface_));			\
	static void CONCAT2(NAME, _deferred_)(void)			\
	{ usb_spi_deferred(&NAME); }					\
	DECLARE_DEFERRED(CONCAT2(NAME, _deferred_));

/*
 * Handle SPI request in a deferred callback.
 */
void usb_spi_deferred(struct usb_spi_config const *config);

/*
 * Set the enable state for the USB-SPI bridge.
 *
 * The bridge must be enabled from both the host and device side
 * before the SPI bus is usable.  This allows the bridge to be
 * available for host tools to use without forcing the device to
 * disconnect or disable whatever else might be using the SPI bus.
 */
void usb_spi_enable(struct usb_spi_config const *config, int enabled);

/*
 * These functions are used by the trampoline functions defined above to
 * connect USB endpoint events with the generic USB GPIO driver.
 */
void usb_spi_tx(struct usb_spi_config const *config);
void usb_spi_rx(struct usb_spi_config const *config);
void usb_spi_reset(struct usb_spi_config const *config);
int  usb_spi_interface(struct usb_spi_config const *config,
		       usb_uint *rx_buf,
		       usb_uint *tx_buf);

/*
 * These functions should be implemented by the board to provide any board
 * specific operations required to enable or disable access to the SPI device.
 */
void usb_spi_board_enable(struct usb_spi_config const *config);
void usb_spi_board_disable(struct usb_spi_config const *config);

#endif /* CHIP_STM32_USB_SPI_H */
