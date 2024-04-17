/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "consumer.h"
#include "producer.h"

/* Fake USB stream for testing */

struct usb_stream_config {
	struct consumer consumer;
	struct producer producer;
};

#define USB_STREAM_CONFIG_FULL(NAME, INTERFACE, INTERFACE_CLASS,           \
			       INTERFACE_SUBCLASS, INTERFACE_PROTOCOL,     \
			       INTERFACE_NAME, ENDPOINT, RX_SIZE, TX_SIZE, \
			       RX_QUEUE, TX_QUEUE, RX_IDX, TX_IDX)         \
	static const struct consumer_ops consumer_ops_##NAME = {           \
		.written = NULL,                                           \
	};                                                                 \
	static const struct producer_ops producer_ops_##NAME = {           \
		.read = NULL,                                              \
	};                                                                 \
	const struct usb_stream_config NAME = {                            \
		.consumer = {                                              \
			.queue = &TX_QUEUE,                                \
			.ops = &consumer_ops_ ## NAME,                     \
		},                                                         \
		.producer = {                                              \
			.queue = &RX_QUEUE,                                \
			.ops = &producer_ops_ ## NAME,                     \
		},                                                         \
	}

extern const struct usb_stream_config usb_update;
