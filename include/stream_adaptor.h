/* Copyright (c) 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef INCLUDE_STREAM_ADAPTOR_H
#define INCLUDE_STREAM_ADAPTOR_H

/* STM32 USART driver for Chrome EC */

#include "common.h"
#include "in_stream.h"
#include "out_stream.h"
#include "consumer.h"
#include "producer.h"

/*
 * +..........+               +..........+------+...........+
 * .          .<------------->.          |      |           .
 * . Producer .  +---------+  . Consumer | ISFP | In Stream .
 * .          .->|  Queue  |->.          |      |           .
 * +..........+  +---------+  +..........+------+...........+
 */

struct in_stream_from_producer {
	struct consumer consumer;
	struct in_stream in;
};

/*
 *
 */
extern struct in_stream_ops const in_stream_from_producer_in_stream_ops;
extern struct consumer_ops const in_stream_from_producer_consumer_ops;

#define IN_STREAM_FROM_PRODUCER(NAME, PRODUCER, QUEUE, READY)		\
	struct in_stream_from_producer const NAME = {			\
		.consumer = {						\
			.producer = &PRODUCER,				\
			.queue    = &QUEUE,				\
			.ops      = &in_stream_from_producer_consumer_ops, \
		},							\
		.in = {							\
			.ready = READY,					\
			.ops   = &in_stream_from_producer_in_stream_ops, \
		},							\
	};

/*
 * +..........+               +..........+------+............+
 * .          .<------------->.          |      |            .
 * . Consumer .  +---------+  . Producer | OSFC | Out Stream .
 * .          .<-|  Queue  |<-.          |      |            .
 * +..........+  +---------+  +..........+------+............+
 */

struct out_stream_from_consumer {
	struct producer producer;
	struct out_stream out;
};

/*
 *
 */
extern struct out_stream_ops const out_stream_from_consumer_out_stream_ops;
extern struct producer_ops const out_stream_from_consumer_producer_ops;

#define OUT_STREAM_FROM_CONSUMER(NAME, CONSUMER, QUEUE, READY)		\
	struct out_stream_from_consumer const NAME = {			\
		.producer = {						\
			.consumer = &CONSUMER,				\
			.queue    = &QUEUE,				\
			.ops      = &out_stream_from_consumer_producer_ops, \
		},							\
		.out = {						\
			.ready = READY,					\
			.ops   = &out_stream_from_consumer_out_stream_ops, \
		},							\
	};

#endif /* INCLUDE_STREAM_ADAPTOR_H */
