/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef INCLUDE_STREAM_ADAPTOR_H
#define INCLUDE_STREAM_ADAPTOR_H

#include "common.h"
#include "in_stream.h"
#include "out_stream.h"
#include "consumer.h"
#include "producer.h"
#include "queue.h"
#include "queue_policies.h"

/*
 * +..........+               +..........+------+...........+
 * .          .<------------->.          |      |           .
 * . Producer .  +---------+  . Consumer | ISFQ | In Stream .
 * .          .->|  Queue  |->.          |      |           .
 * +..........+  +---------+  +..........+------+...........+
 */

struct in_stream_from_queue {
	struct consumer consumer;
	struct in_stream in;
};

/*
 *
 */
extern struct in_stream_ops const in_stream_from_queue_in_stream_ops;
extern struct consumer_ops const in_stream_from_queue_consumer_ops;

#define IN_STREAM_FROM_QUEUE(QUEUE, READY)				\
	((struct in_stream_from_queue) {				\
		.consumer = {						\
			.queue = &QUEUE,				\
			.ops   = &in_stream_from_queue_consumer_ops,	\
		},							\
		.in = {							\
			.ready = READY,					\
			.ops   = &in_stream_from_queue_in_stream_ops,	\
		},							\
	})

/*
 * +..........+               +..........+------+............+
 * .          .<------------->.          |      |            .
 * . Consumer .  +---------+  . Producer | OSFQ | Out Stream .
 * .          .<-|  Queue  |<-.          |      |            .
 * +..........+  +---------+  +..........+------+............+
 */

struct out_stream_from_queue {
	struct producer producer;
	struct out_stream out;
};

/*
 *
 */
extern struct out_stream_ops const out_stream_from_queue_out_stream_ops;
extern struct producer_ops const out_stream_from_queue_producer_ops;

#define OUT_STREAM_FROM_QUEUE(QUEUE, READY)				\
	((struct out_stream_from_queue) {				\
		.producer = {						\
			.queue = &QUEUE,				\
			.ops   = &out_stream_from_queue_producer_ops,	\
		},							\
		.out = {						\
			.ready = READY,					\
			.ops   = &out_stream_from_queue_out_stream_ops, \
		},							\
	})

/*
 * Given a forward declared device configuration called NAME that implements
 * producer and consumer interfaces construct RX/TX queues and expose them as
 * streams called <NAME>_in and <NAME>_out.
 */
#define IO_STREAM_CONFIG(NAME, RX_SIZE, TX_SIZE, IN_READY, OUT_READY)	\
	struct in_stream_from_queue const CONCAT2(NAME, _in);		\
									\
	struct queue const CONCAT2(NAME, _rx_queue) =			\
		QUEUE_DIRECT(RX_SIZE,					\
			     uint8_t,					\
			     NAME.producer,				\
			     CONCAT2(NAME, _in).consumer);		\
	struct in_stream_from_queue const CONCAT2(NAME, _in) =		\
		IN_STREAM_FROM_QUEUE(CONCAT2(NAME, _rx_queue),		\
				     IN_READY);				\
									\
									\
	struct out_stream_from_queue const CONCAT2(NAME, _out);		\
									\
	struct queue const CONCAT2(NAME, _tx_queue) =			\
		QUEUE_DIRECT(TX_SIZE,					\
			     uint8_t,					\
			     CONCAT2(NAME, _out).producer,		\
			     NAME.consumer);				\
	struct out_stream_from_queue const CONCAT2(NAME, _out) =	\
		OUT_STREAM_FROM_QUEUE(CONCAT2(NAME, _tx_queue),		\
				      OUT_READY);

#endif /* INCLUDE_STREAM_ADAPTOR_H */
