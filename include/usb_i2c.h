/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "consumer.h"
#include "producer.h"
#include "registers.h"
#include "task.h"
#include "usb_descriptor.h"
#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __CROS_USB_I2C_H
#define __CROS_USB_I2C_H

/*
 * This header file describes i2c encapsulation when communicated over USB.
 *
 * Note that current implementation assumes that there is only one instance of
 * interface of this kind per device.
 *
 * 2 forms of command are supported:
 *   - When write payload + header is larger than 64 bytes, which exceed the
 *     common USB packet (64 bytes), remaining payload should send without
 *     header.
 *
 *   - CONFIG_USB_I2C_MAX_WRITE_COUNT / CONFIG_USB_I2C_MAX_READ_COUNT have to
 *     be defined properly based on the use cases.
 *
 *   - Read less than 128 (0x80) bytes.
 *   +---------+------+----+----+---------------+
 *   | wc/port | addr | wc | rc | write payload |
 *   +---------+------+----+----+---------------+
 *   |   1B    |  1B  | 1B | 1B |  < 256 bytes  |
 *   +---------+------+----+----+---------------+
 *
 *   - Read less than 32768 (0x8000) bytes.
 *   +---------+------+----+----+-----+----------+---------------+
 *   | wc/port | addr | wc | rc | rc1 | reserved | write payload |
 *   +---------+------+----+----+----------------+---------------+
 *   |    1B   |  1B  | 1B | 1B |  1B |    1B    |  < 256 bytes  |
 *   +---------+------+----+----+----------------+---------------+
 *
 *   - Special notes for rc and rc1:
 *     If the most significant bit in rc is set (rc >= 0x80), this indicates
 *     that we want to read back more than 127 bytes, so the first byte of
 *     data contains rc1 (read count continuation), and the final read count
 *     will be (rc1 << 7) | (rc & 0x7F).
 *
 *   Fields:
 *
 *   - wc/port: 1 byte: 4 top bits are the 4 top bits of the 12 bit write
 *         counter, the 4 bottom bits are the port address, i2c interface
 *         index.
 *
 *   - addr: peripheral address, 1 byte, i2c 7-bit bus address.
 *
 *   - wc: write count, 1 byte, zero based count of bytes to write. If the
 *         indicated write count cause the payload + header exceeds 64 bytes,
 *         Following packets are expected to continue the payload without
 *         header.
 *
 *   - rc: read count, 1 byte, zero based count of bytes to read. To read more
 *         than 127 (0x7F) bytes please see the special notes above.
 *
 *   - data: payload of data to write. See wc above for more information.
 *
 *   - rc1: extended read count, 1 byte. An extended version indicates we want
 *          to read more data. While the most significant bits is set in read
 *          count (rc >= 0x80), rc1 will concatenate with rc together. See the
 *          special notes above for concatenating details.
 *
 *   - reserved: reserved byte, 1 byte.
 *
 * Response:
 *     +-------------+---+---+--------------+
 *     | status : 2B | 0 | 0 | read payload |
 *     +-------------+---+---+--------------+
 *
 *   - read payload might not fit into a single USB packets. Remaining will be
 *     transimitted witout header. Receiving side should concatenate them.
 *
 *     status: 2 byte status
 *         0x0000: Success
 *         0x0001: I2C timeout
 *         0x0002: Busy, try again
 *             This can happen if someone else has acquired the shared memory
 *             buffer that the I2C driver uses as /dev/null
 *         0x0003: Write count invalid (mismatch with merged payload)
 *         0x0004: Read count invalid (e.g. larger than available buffer)
 *         0x0005: The port specified is invalid.
 *         0x0006: The I2C interface is disabled.
 *         0x8000: Unknown error mask
 *             The bottom 15 bits will contain the bottom 15 bits from the EC
 *             error code.
 *
 *     read payload: Depends on the buffer size and implementation. Length will
 *             match requested read count
 */

enum usb_i2c_error {
	USB_I2C_SUCCESS = 0x0000,
	USB_I2C_TIMEOUT = 0x0001,
	USB_I2C_BUSY = 0x0002,
	USB_I2C_WRITE_COUNT_INVALID = 0x0003,
	USB_I2C_READ_COUNT_INVALID = 0x0004,
	USB_I2C_PORT_INVALID = 0x0005,
	USB_I2C_DISABLED = 0x0006,
	USB_I2C_MISSING_HANDLER = 0x0007,
	USB_I2C_UNSUPPORTED_COMMAND = 0x0008,
	USB_I2C_UNKNOWN_ERROR = 0x8000,
};

#define USB_I2C_WRITE_BUFFER (CONFIG_USB_I2C_MAX_WRITE_COUNT + 4)
/* If read payload is larger or equal to 128 bytes, header contains rc1 */
#define USB_I2C_READ_BUFFER                            \
	((CONFIG_USB_I2C_MAX_READ_COUNT < 128) ?       \
		 (CONFIG_USB_I2C_MAX_READ_COUNT + 4) : \
		 (CONFIG_USB_I2C_MAX_READ_COUNT + 6))

#define USB_I2C_BUFFER_SIZE                                                 \
	(USB_I2C_READ_BUFFER > USB_I2C_WRITE_BUFFER ? USB_I2C_READ_BUFFER : \
						      USB_I2C_WRITE_BUFFER)

BUILD_ASSERT(POWER_OF_TWO(USB_I2C_READ_BUFFER));
BUILD_ASSERT(POWER_OF_TWO(USB_I2C_WRITE_BUFFER));

/*
 * Compile time Per-USB gpio configuration stored in flash.  Instances of this
 * structure are provided by the user of the USB i2c.  This structure binds
 * together all information required to operate a USB i2c.
 */
struct usb_i2c_config {
	uint16_t *buffer;

	/* Deferred function to call to handle SPI request. */
	const struct deferred_data *deferred;

	struct consumer const consumer;
	struct queue const *tx_queue;
};

extern struct consumer_ops const usb_i2c_consumer_ops;

/*
 * Convenience macro for defining a USB I2C bridge driver.
 *
 * NAME is used to construct the names of the trampoline functions and the
 * usb_i2c_config struct, the latter is just called NAME.
 *
 * INTERFACE is the index of the USB interface to associate with this
 * I2C driver.
 *
 * INTERFACE_NAME is the index of the USB string descriptor (iInterface).
 *
 * ENDPOINT is the index of the USB bulk endpoint used for receiving and
 * transmitting bytes.
 */
#define USB_I2C_CONFIG(NAME, INTERFACE, INTERFACE_NAME, ENDPOINT)              \
	static uint16_t CONCAT2(NAME, _buffer_)[USB_I2C_BUFFER_SIZE / 2];      \
	static void CONCAT2(NAME, _deferred_)(void);                           \
	DECLARE_DEFERRED(CONCAT2(NAME, _deferred_));                           \
	static struct queue const CONCAT2(NAME, _to_usb_);                     \
	static struct queue const CONCAT3(usb_to_, NAME, _);                   \
	USB_STREAM_CONFIG_FULL(CONCAT2(NAME, _usb_), INTERFACE,                \
			       USB_CLASS_VENDOR_SPEC, USB_SUBCLASS_GOOGLE_I2C, \
			       USB_PROTOCOL_GOOGLE_I2C, INTERFACE_NAME,        \
			       ENDPOINT, USB_MAX_PACKET_SIZE,                  \
			       USB_MAX_PACKET_SIZE, CONCAT3(usb_to_, NAME, _), \
			       CONCAT2(NAME, _to_usb_), 1, 0)                  \
	struct usb_i2c_config const NAME = {				\
		.buffer    = CONCAT2(NAME, _buffer_),			\
		.deferred  = &CONCAT2(NAME, _deferred__data),		\
		.consumer  = {						\
			.queue = &CONCAT3(usb_to_, NAME, _),		\
			.ops   = &usb_i2c_consumer_ops,			\
		},							\
		.tx_queue = &CONCAT2(NAME, _to_usb_),			\
	};                              \
	static struct queue const CONCAT2(NAME, _to_usb_) =                    \
		QUEUE_DIRECT(USB_I2C_READ_BUFFER, uint8_t, null_producer,      \
			     CONCAT2(NAME, _usb_).consumer);                   \
	static struct queue const CONCAT3(usb_to_, NAME, _) =                  \
		QUEUE_DIRECT(USB_I2C_WRITE_BUFFER, uint8_t,                    \
			     CONCAT2(NAME, _usb_).producer, NAME.consumer);    \
	static void CONCAT2(NAME, _deferred_)(void)                            \
	{                                                                      \
		usb_i2c_deferred(&NAME);                                       \
	}

/*
 * Handle I2C request in a deferred callback.
 */
void usb_i2c_deferred(struct usb_i2c_config const *config);

/*
 * These functions should be implemented by the board to provide any board
 * specific operations required to enable or disable access to the I2C device,
 * and to return the current board enable state.
 */

/**
 * Check if the I2C device is enabled
 *
 * @return 1 if enabled, 0 if disabled.
 */
int usb_i2c_board_is_enabled(void);

/*
 * Special i2c address to use when the client is required to execute some
 * command which does not directly involve the i2c controller driver.
 */
#define USB_I2C_CMD_ADDR_FLAGS 0x78

/*
 * Function to call to register a handler for commands sent to the special i2c
 * address above.
 */
int usb_i2c_register_cros_cmd_handler(int (*cmd_handler)(
	void *data_in, size_t in_size, void *data_out, size_t out_size));

#ifdef __cplusplus
}
#endif

#endif /* __CROS_USB_I2C_H */
