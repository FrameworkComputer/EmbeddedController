/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_USB_SPI_H
#define __CROS_EC_USB_SPI_H

/* USB SPI driver for Chrome EC */

#include "compile_time_macros.h"
#include "hooks.h"
#include "queue.h"
#include "queue_policies.h"
#include "usb_descriptor.h"
#include "usb-stream.h"

#define HEADER_SIZE 2

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
	USB_SPI_REQ_ENABLE          = 0x0000,
	USB_SPI_REQ_DISABLE         = 0x0001,
	USB_SPI_REQ_ENABLE_AP       = 0x0002,
	USB_SPI_REQ_ENABLE_EC       = 0x0003,
	USB_SPI_REQ_ENABLE_H1       = 0x0004,
	USB_SPI_REQ_RESET           = 0x0005,
	USB_SPI_REQ_BOOT_CFG        = 0x0006,
	USB_SPI_REQ_SOCKET          = 0x0007,
	USB_SPI_REQ_SIGNING_START   = 0x0008,
	USB_SPI_REQ_SIGNING_SIGN    = 0x0009,
};

/* USB SPI device bitmasks */
enum usb_spi {
	USB_SPI_DISABLE = 0,
	USB_SPI_AP = BIT(0),
	USB_SPI_EC = BIT(1),
	USB_SPI_H1 = BIT(2),
	USB_SPI_ALL = USB_SPI_AP | USB_SPI_EC | USB_SPI_H1
};


#define USB_SPI_MAX_WRITE_COUNT 62
#define USB_SPI_MAX_READ_COUNT  62

BUILD_ASSERT(USB_MAX_PACKET_SIZE == (1 + 1 + USB_SPI_MAX_WRITE_COUNT));
BUILD_ASSERT(USB_MAX_PACKET_SIZE == (2 + USB_SPI_MAX_READ_COUNT));

struct usb_spi_state {
	/*
	 * The SPI bridge must be enabled both locally and by the host to allow
	 * access to the SPI device.  The enabled_host flag is set and cleared
	 * by sending USB_SPI_REQ_ENABLE_EC, USB_SPI_REQ_ENABLE_HOST, and
	 * USB_SPI_REQ_DISABLE to the device control endpoint.  The
	 * enabled_device flag is set by calling usb_spi_enable.
	 */
	int enabled_host;
	int enabled_device;

	/*
	 * The current enabled state.  This is only updated in the deferred
	 * callback.  Whenever either of the host or device specific enable
	 * flags is changed the deferred callback is queued, and it will check
	 * their combined state against this flag.  If the combined state is
	 * different, then one of usb_spi_board_enable or usb_spi_board_disable
	 * is called and this flag is updated.  This ensures that the board
	 * specific state update routines are only called from the deferred
	 * callback.
	 */
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
	 * Interface and endpoint indices.
	 */
	int interface;
	int endpoint;

	/*
	 * Deferred function to call to handle SPI request.
	 */
	const struct deferred_data *deferred;



	/*
	 * Pointer to tx and rx queues and bounce buffer.
	 */
	uint8_t *buffer;
	struct consumer const consumer;
	struct queue const *tx_queue;
};

extern struct consumer_ops const usb_spi_consumer_ops;

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
									\
	static uint8_t CONCAT2(NAME, _buffer_)[USB_MAX_PACKET_SIZE];	\
	static void CONCAT2(NAME, _deferred_)(void);			\
	DECLARE_DEFERRED(CONCAT2(NAME, _deferred_));			\
	static struct queue const CONCAT2(NAME, _to_usb_);		\
	static struct queue const CONCAT3(usb_to_, NAME, _);		\
	USB_STREAM_CONFIG_FULL(CONCAT2(NAME, _usb_),			\
			       INTERFACE,				\
			       USB_CLASS_VENDOR_SPEC,			\
			       USB_SUBCLASS_GOOGLE_SPI,			\
			       USB_PROTOCOL_GOOGLE_SPI,			\
			       USB_STR_SPI_NAME,			\
			       ENDPOINT,				\
			       USB_MAX_PACKET_SIZE,			\
			       USB_MAX_PACKET_SIZE,			\
			       CONCAT3(usb_to_, NAME, _),		\
			       CONCAT2(NAME, _to_usb_))			\
	static struct usb_spi_state CONCAT2(NAME, _state_);		\
	struct usb_spi_config const NAME = {				\
		.state     = &CONCAT2(NAME, _state_),			\
		.interface = INTERFACE,					\
		.endpoint  = ENDPOINT,					\
		.deferred  = &CONCAT2(NAME, _deferred__data),		\
		.buffer    = CONCAT2(NAME, _buffer_),			\
		.consumer  = {						\
			.queue = &CONCAT3(usb_to_, NAME, _),		\
			.ops   = &usb_spi_consumer_ops,			\
		},							\
		.tx_queue = &CONCAT2(NAME, _to_usb_),			\
	};								\
	static struct queue const CONCAT2(NAME, _to_usb_) =		\
		QUEUE_DIRECT(USB_MAX_PACKET_SIZE, uint8_t,		\
		null_producer, CONCAT2(NAME, _usb_).consumer);		\
	static struct queue const CONCAT3(usb_to_, NAME, _) =		\
		QUEUE_DIRECT(USB_MAX_PACKET_SIZE, uint8_t,		\
		CONCAT2(NAME, _usb_).producer, NAME.consumer);		\
	static int CONCAT2(NAME, _interface_)				\
		(struct usb_setup_packet *req)				\
	{								\
		return usb_spi_interface(&NAME, req);			\
	}								\
	USB_DECLARE_IFACE(INTERFACE, CONCAT2(NAME, _interface_));	\
	static void CONCAT2(NAME, _deferred_)(void)			\
	{								\
		usb_spi_deferred(&NAME);				\
	}

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
 * This is used by the trampoline function defined above  interpret the USB
 * endpoint events with the generic USB GPIO driver.
 */
int usb_spi_interface(struct usb_spi_config const *config,
		      struct usb_setup_packet *req);

/*
 * These functions should be implemented by the board to provide any board
 * specific operations required to enable or disable access to the SPI device.
 * usb_spi_board_enable should return EC_SUCCESS on success or an error
 * otherwise.
 */
int usb_spi_board_enable(struct usb_spi_config const *config);
void usb_spi_board_disable(struct usb_spi_config const *config);

#endif /* __CROS_EC_USB_SPI_H */
