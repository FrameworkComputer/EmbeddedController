/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "consumer.h"
#include "i2c.h"
#include "link_defs.h"
#include "producer.h"
#include "queue.h"
#include "queue_policies.h"
#include "registers.h"
#include "task.h"

#if defined(CONFIG_PLATFORM_EC_USB_I2C)
#include "drivers/usb_stream.h"
#else
#include "usb-stream.h"
#endif

#include "usb_descriptor.h"
#include "usb_i2c.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)

USB_I2C_CONFIG(i2c, USB_IFACE_I2C, USB_STR_I2C_NAME, USB_EP_I2C)

static int (*cros_cmd_handler)(void *data_in, size_t in_size, void *data_out,
			       size_t out_size);

static int16_t usb_i2c_map_error(int error)
{
	switch (error) {
	case EC_SUCCESS:
		return USB_I2C_SUCCESS;
	case EC_ERROR_TIMEOUT:
		return USB_I2C_TIMEOUT;
	case EC_ERROR_BUSY:
		return USB_I2C_BUSY;
	default:
		return USB_I2C_UNKNOWN_ERROR | (error & 0x7fff);
	}
}

/*
 * Return value should be large enough to accommodate the entire read queue
 * buffer size. Let's use 4 bytes in case future designs have a lot of RAM and
 * allow for large buffers.
 */
static uint32_t usb_i2c_read_packet(struct usb_i2c_config const *config)
{
	return QUEUE_REMOVE_UNITS(config->consumer.queue, config->buffer,
				  queue_count(config->consumer.queue));
}

static void usb_i2c_write_packet(struct usb_i2c_config const *config,
				 size_t count)
{
	QUEUE_ADD_UNITS(config->tx_queue, config->buffer, count);
}

static uint8_t usb_i2c_executable(struct usb_i2c_config const *config)
{
	static size_t expected_size;

	if (!expected_size) {
		uint8_t peek[4];

		/*
		 * In order to support larger write payload, we need to peek
		 * the queue to see if we need to wait for more data.
		 */
		if (queue_peek_units(config->consumer.queue, peek, 0,
				     sizeof(peek)) != sizeof(peek)) {
			/* Not enough data to calculate expected_size. */
			return 0;
		}
		/*
		 * The first four bytes of the packet will describe its
		 * expected size.
		 */
		/* Header bytes  and extra rc bytes, if present. */
		if (peek[3] & 0x80)
			expected_size = 6;
		else
			expected_size = 4;

		/* write count */
		expected_size += (((size_t)peek[0] & 0xf0) << 4) | peek[2];
	}

	if (queue_count(config->consumer.queue) >= expected_size) {
		expected_size = 0;
		return 1;
	}

	return 0;
}

static void usb_i2c_execute(struct usb_i2c_config const *config)
{
	/* Payload is ready to execute. */
	uint32_t count = usb_i2c_read_packet(config);
	int portindex = (config->buffer[0] >> 0) & 0xf;
	uint16_t addr_flags = (config->buffer[0] >> 8) & 0x7f;
	int write_count = ((config->buffer[0] << 4) & 0xf00) |
			  ((config->buffer[1] >> 0) & 0xff);
	int read_count = (config->buffer[1] >> 8) & 0xff;
	int offset = 0; /* Offset for extended reading header. */

	config->buffer[0] = 0;
	config->buffer[1] = 0;

	if (read_count & 0x80) {
		read_count = ((config->buffer[2] & 0xff) << 7) |
			     (read_count & 0x7f);
		offset = 2;
	}

	if (!count || (!read_count && !write_count))
		return;

	if (!usb_i2c_board_is_enabled()) {
		config->buffer[0] = USB_I2C_DISABLED;
	} else if (write_count > CONFIG_USB_I2C_MAX_WRITE_COUNT ||
		   write_count != (count - 4 - offset)) {
		config->buffer[0] = USB_I2C_WRITE_COUNT_INVALID;
	} else if (read_count > CONFIG_USB_I2C_MAX_READ_COUNT) {
		config->buffer[0] = USB_I2C_READ_COUNT_INVALID;
	} else if (portindex >= i2c_ports_used) {
		config->buffer[0] = USB_I2C_PORT_INVALID;
	} else if (addr_flags == USB_I2C_CMD_ADDR_FLAGS) {
		/*
		 * This is a non-i2c command, invoke the handler if it has
		 * been registered, if not - report the appropriate error.
		 */
		if (!cros_cmd_handler)
			config->buffer[0] = USB_I2C_MISSING_HANDLER;
		else
			config->buffer[0] = cros_cmd_handler(config->buffer + 2,
							     write_count,
							     config->buffer + 2,
							     read_count);
	} else {
		int ret;

		/*
		 * TODO (crbug.com/750397): Add security.  This currently
		 * blindly passes through ALL I2C commands on any bus the EC
		 * knows about.  It should behave closer to
		 * EC_CMD_I2C_PASSTHRU, which can protect ports and ranges.
		 */
		ret = i2c_xfer(i2c_ports[portindex].port, addr_flags,
			       (uint8_t *)(config->buffer + 2) + offset,
			       write_count, (uint8_t *)(config->buffer + 2),
			       read_count);
		config->buffer[0] = usb_i2c_map_error(ret);
	}
	usb_i2c_write_packet(config, read_count + 4);
}

void usb_i2c_deferred(struct usb_i2c_config const *config)
{
	/* Check if we can proceed the queue. */
	if (usb_i2c_executable(config))
		usb_i2c_execute(config);
}

static void usb_i2c_written(struct consumer const *consumer, size_t count)
{
	struct usb_i2c_config const *config =
		DOWNCAST(consumer, struct usb_i2c_config, consumer);

	hook_call_deferred(config->deferred, 0);
}

struct consumer_ops const usb_i2c_consumer_ops = {
	.written = usb_i2c_written,
};

int usb_i2c_register_cros_cmd_handler(int (*cmd_handler)(
	void *data_in, size_t in_size, void *data_out, size_t out_size))
{
	if (cros_cmd_handler)
		return -1;
	cros_cmd_handler = cmd_handler;
	return 0;
}
