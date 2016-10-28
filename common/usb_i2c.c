/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "link_defs.h"
#include "registers.h"
#include "i2c.h"
#include "usb_descriptor.h"
#include "util.h"

#include "common.h"
#include "console.h"
#include "consumer.h"
#include "queue.h"
#include "queue_policies.h"
#include "producer.h"
#include "task.h"
#include "usb-stream.h"
#include "usb_i2c.h"


#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

USB_I2C_CONFIG(i2c,
	       USB_IFACE_I2C,
	       USB_STR_I2C_NAME,
	       USB_EP_I2C)

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
	return QUEUE_REMOVE_UNITS(config->consumer.queue, config->buffer,
		queue_count(config->consumer.queue));
}

static void usb_i2c_write_packet(struct usb_i2c_config const *config,
				 uint8_t count)
{
	QUEUE_ADD_UNITS(config->tx_queue, config->buffer, count);
}

void usb_i2c_deferred(struct usb_i2c_config const *config)
{
	/*
	 * And if there is a USB packet waiting we process it and generate a
	 * response.
	 */
	uint8_t count      = usb_i2c_read_packet(config);
	int portindex       = (config->buffer[0] >> 0) & 0xff;
	/* Convert 7-bit slave address to chromium EC 8-bit address. */
	uint8_t slave_addr  = (config->buffer[0] >> 7) & 0xfe;
	int write_count     = (config->buffer[1] >> 0) & 0xff;
	int read_count      = (config->buffer[1] >> 8) & 0xff;
	int port;
	int rv;

	config->buffer[0] = 0;
	config->buffer[1] = 0;

	if (!count || (!read_count && !write_count))
		return;

	if (write_count > USB_I2C_MAX_WRITE_COUNT ||
	    write_count != (count - 4)) {
		config->buffer[0] = USB_I2C_WRITE_COUNT_INVALID;
	} else if (read_count > USB_I2C_MAX_READ_COUNT) {
		config->buffer[0] = USB_I2C_READ_COUNT_INVALID;
	} else if (portindex >= i2c_ports_used) {
		config->buffer[0] = USB_I2C_PORT_INVALID;
	} else {
		rv = usb_i2c_board_enable();
		if (rv) {
			config->buffer[0] = usb_i2c_map_error(rv);
		} else {
			port = i2c_ports[portindex].port;
			config->buffer[0] = usb_i2c_map_error(
				i2c_xfer(port, slave_addr,
					(uint8_t *)(config->buffer + 2),
					write_count,
					(uint8_t *)(config->buffer + 2),
					read_count, I2C_XFER_SINGLE));
			usb_i2c_board_disable(1);
		}
	}

	usb_i2c_write_packet(config, read_count + 4);
}

static void usb_i2c_written(struct consumer const *consumer, size_t count)
{
	struct usb_i2c_config const *config =
		DOWNCAST(consumer, struct usb_i2c_config, consumer);

	hook_call_deferred(config->deferred, 0);
}

static void usb_i2c_flush(struct consumer const *consumer)
{
}

struct consumer_ops const usb_i2c_consumer_ops = {
	.written = usb_i2c_written,
	.flush   = usb_i2c_flush,
};
