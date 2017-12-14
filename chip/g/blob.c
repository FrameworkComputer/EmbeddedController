/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Handle an opaque blob of data */

#include "common.h"
#include "console.h"
#include "consumer.h"
#include "queue.h"
#include "queue_policies.h"
#include "producer.h"
#include "task.h"
#include "usb-stream.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

struct consumer const blob_consumer;
struct usb_stream_config const usb_blob;

static struct queue const blob_to_usb = QUEUE_DIRECT(64, uint8_t,
						     null_producer,
						     usb_blob.consumer);
static struct queue const usb_to_blob = QUEUE_DIRECT(64, uint8_t,
						     usb_blob.producer,
						     blob_consumer);

USB_STREAM_CONFIG(usb_blob,
		  USB_IFACE_BLOB,
		  USB_STR_BLOB_NAME,
		  USB_EP_BLOB,
		  USB_MAX_PACKET_SIZE,
		  USB_MAX_PACKET_SIZE,
		  usb_to_blob,
		  blob_to_usb)

static void blob_written(struct consumer const *consumer, size_t count)
{
	int i;
	uint8_t buf[USB_MAX_PACKET_SIZE];

	count = QUEUE_REMOVE_UNITS(consumer->queue, buf, count);

	CPRINTS("Received: count=%d buf=((%s))", count, buf);

	/*
	 * Just to have something to test to begin with, we'll
	 * implement "tr a-zA-Z A-Za-z" and return the result.
	 */
	for (i = 0; i < count; i++) {
		char tmp = buf[i];

		if (tmp >= 'a' && tmp <= 'z')
			buf[i] = tmp - ('a' - 'A');
		else if (tmp >= 'A' && tmp <= 'Z')
			buf[i] = tmp + ('a' - 'A');
	}

	count = QUEUE_ADD_UNITS(&blob_to_usb, buf, count);
	CPRINTS("Sending: count=%d buf=((%s))", count, buf);
}

struct consumer const blob_consumer = {
	.queue = &usb_to_blob,
	.ops   = &((struct consumer_ops const) {
		.written = blob_written,
	}),
};
