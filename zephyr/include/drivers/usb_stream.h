/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "consumer.h"
#include "producer.h"

struct usb_stream_config {
	struct consumer consumer;
	struct producer producer;
};

#define USB_STREAM_CONFIG_FULL(NAME, INTERFACE, INTERFACE_CLASS,             \
			       INTERFACE_SUBCLASS, INTERFACE_PROTOCOL,       \
			       INTERFACE_NAME, ENDPOINT, RX_SIZE, TX_SIZE,   \
			       RX_QUEUE, TX_QUEUE, RX_IDX, TX_IDX)           \
	void CONCAT2(NAME, _stream_written)(struct consumer const *consumer, \
					    size_t count);                   \
	static const struct consumer_ops CONCAT2(consumer_ops_, NAME) = {    \
		.written = CONCAT2(NAME, _stream_written),                   \
	};                                                                   \
	static const struct producer_ops CONCAT2(producer_ops_, NAME) = {    \
		.read = NULL,                                                \
	};                                                                   \
	const struct usb_stream_config NAME = {                            \
		.consumer = {                                              \
			.queue = &TX_QUEUE,                                \
			.ops = &CONCAT2(consumer_ops_, NAME),              \
		},                                                         \
		.producer = {                                              \
			.queue = &RX_QUEUE,                                \
			.ops = &CONCAT2(producer_ops_, NAME),              \
		},                                                         \
	};

extern const struct usb_stream_config usb_update;
extern const struct usb_stream_config i2c_usb_;
