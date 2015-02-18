/* Copyright (c) 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Stream adaptor implementation.
 */

#include "producer.h"
#include "consumer.h"
#include "stream_adaptor.h"
#include "util.h"

static size_t in_stream_from_producer_read(struct in_stream const *stream,
					   uint8_t *buffer,
					   size_t count)
{
	struct in_stream_from_producer const *adaptor =
		DOWNCAST(stream, struct in_stream_from_producer, in);

	return consumer_read_memcpy(&adaptor->consumer, buffer, count, memcpy);
}

static void in_stream_from_producer_written(struct consumer const *consumer,
					    size_t count)
{
	struct in_stream_from_producer const *adaptor =
		DOWNCAST(consumer, struct in_stream_from_producer, consumer);

	in_stream_ready(&adaptor->in);
}

struct in_stream_ops const in_stream_from_producer_in_stream_ops = {
	.read = in_stream_from_producer_read,
};

struct consumer_ops const in_stream_from_producer_consumer_ops = {
	.written = in_stream_from_producer_written,
};

static size_t out_stream_from_consumer_write(struct out_stream const *stream,
					     uint8_t const *buffer,
					     size_t count)
{
	struct out_stream_from_consumer const *adaptor =
		DOWNCAST(stream, struct out_stream_from_consumer, out);

	return producer_write_memcpy(&adaptor->producer, buffer, count, memcpy);
}

static void out_stream_from_consumer_read(struct producer const *producer,
					     size_t count)
{
	struct out_stream_from_consumer const *adaptor =
		DOWNCAST(producer, struct out_stream_from_consumer, producer);

	out_stream_ready(&adaptor->out);
}

struct out_stream_ops const out_stream_from_consumer_out_stream_ops = {
	.write = out_stream_from_consumer_write,
};

struct producer_ops const out_stream_from_consumer_producer_ops = {
	.read = out_stream_from_consumer_read,
};
