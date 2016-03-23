/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_USB_I2C_H
#define __CROS_EC_USB_I2C_H

/* STM32 USB I2C driver for Chrome EC */

#include "compile_time_macros.h"
#include "hooks.h"
#include "usb_descriptor.h"

/*
 * Command:
 *     +----------+-----------+---------------+---------------+---------------+
 *     | port: 1B | addr: 1B  | wr count : 1B | rd count : 1B | data : <= 60B |
 *     +----------+-----------+---------------+---------------+---------------+
 *
 *     port address:  1 byte, stm32 i2c interface index
 *
 *     slave address: 1 byte, i2c 7-bit bus address
 *
 *     write count:   1 byte, zero based count of bytes to write
 *
 *     read count:    1 byte, zero based count of bytes to read
 *
 *     data:          write payload up to 60 bytes of data to write,
 *                    length must match write count
 *
 * Response:
 *     +-------------+---+---+-----------------------+
 *     | status : 2B | 0 | 0 | read payload : <= 60B |
 *     +-------------+---+---+-----------------------+
 *
 *     status: 2 byte status
 *         0x0000: Success
 *         0x0001: I2C timeout
 *         0x0002: Busy, try again
 *             This can happen if someone else has acquired the shared memory
 *             buffer that the I2C driver uses as /dev/null
 *         0x0003: Write count invalid (> 60 bytes, or mismatch with payload)
 *         0x0004: Read count invalid (> 60 bytes)
 *         0x0005: The port specified is invalid.
 *         0x8000: Unknown error mask
 *             The bottom 15 bits will contain the bottom 15 bits from the EC
 *             error code.
 *
 *     read payload: up to 60 bytes of data read from I2C, length will match
 *                   requested read count
 */

enum usb_i2c_error {
	USB_I2C_SUCCESS             = 0x0000,
	USB_I2C_TIMEOUT             = 0x0001,
	USB_I2C_BUSY                = 0x0002,
	USB_I2C_WRITE_COUNT_INVALID = 0x0003,
	USB_I2C_READ_COUNT_INVALID  = 0x0004,
	USB_I2C_PORT_INVALID        = 0x0005,
	USB_I2C_UNKNOWN_ERROR       = 0x8000,
};


#define USB_I2C_MAX_WRITE_COUNT 60
#define USB_I2C_MAX_READ_COUNT  60

BUILD_ASSERT(USB_MAX_PACKET_SIZE == (1 + 1 + 1 + 1 + USB_I2C_MAX_WRITE_COUNT));
BUILD_ASSERT(USB_MAX_PACKET_SIZE == (2 + 1 + 1 + USB_I2C_MAX_READ_COUNT));

/*
 * Compile time Per-USB gpio configuration stored in flash.  Instances of this
 * structure are provided by the user of the USB gpio.  This structure binds
 * together all information required to operate a USB gpio.
 */
struct usb_i2c_config {
	/*
	 * Interface and endpoint indicies.
	 */
	int interface;
	int endpoint;

	/*
	 * Deferred function to call to handle I2C request.
	 */
	const struct deferred_data *deferred;

	/*
	 * Pointers to USB packet RAM and bounce buffer.
	 */
	uint16_t *buffer;
	usb_uint *rx_ram;
	usb_uint *tx_ram;
};

/*
 * Convenience macro for defining a USB I2C bridge driver.
 *
 * NAME is used to construct the names of the trampoline functions and the
 * usb_i2c_config struct, the latter is just called NAME.
 *
 * INTERFACE is the index of the USB interface to associate with this
 * I2C driver.
 *
 * ENDPOINT is the index of the USB bulk endpoint used for receiving and
 * transmitting bytes.
 */
#define USB_I2C_CONFIG(NAME,						\
		       INTERFACE,					\
		       ENDPOINT)					\
	static uint16_t							\
		CONCAT2(NAME, _buffer_)[USB_MAX_PACKET_SIZE / 2];	\
	static usb_uint CONCAT2(NAME, _ep_rx_buffer_)			\
		[USB_MAX_PACKET_SIZE / 2] __usb_ram;			\
	static usb_uint CONCAT2(NAME, _ep_tx_buffer_)			\
		[USB_MAX_PACKET_SIZE / 2] __usb_ram;			\
	static void CONCAT2(NAME, _deferred_)(void);			\
	DECLARE_DEFERRED(CONCAT2(NAME, _deferred_));			\
	struct usb_i2c_config const NAME = {				\
		.interface = INTERFACE,					\
		.endpoint  = ENDPOINT,					\
		.deferred  = &CONCAT2(NAME, _deferred__data),		\
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
		.bInterfaceSubClass = USB_SUBCLASS_GOOGLE_I2C,		\
		.bInterfaceProtocol = USB_PROTOCOL_GOOGLE_I2C,		\
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
	static void CONCAT2(NAME, _ep_tx_)   (void)			\
		{ usb_i2c_tx(&NAME); }					\
	static void CONCAT2(NAME, _ep_rx_)   (void)			\
		{ usb_i2c_rx(&NAME); }					\
	static void CONCAT2(NAME, _ep_reset_)(void)			\
		{ usb_i2c_reset(&NAME); }				\
	USB_DECLARE_EP(ENDPOINT,					\
		       CONCAT2(NAME, _ep_tx_),				\
		       CONCAT2(NAME, _ep_rx_),				\
		       CONCAT2(NAME, _ep_reset_));			\
	static void CONCAT2(NAME, _deferred_)(void)			\
	{ usb_i2c_deferred(&NAME); }

/*
 * Handle I2C request in a deferred callback.
 */
void usb_i2c_deferred(struct usb_i2c_config const *config);

/*
 * These functions are used by the trampoline functions defined above to
 * connect USB endpoint events with the generic USB GPIO driver.
 */
void usb_i2c_tx(struct usb_i2c_config const *config);
void usb_i2c_rx(struct usb_i2c_config const *config);
void usb_i2c_reset(struct usb_i2c_config const *config);

#endif /* __CROS_EC_USB_I2C_H */
