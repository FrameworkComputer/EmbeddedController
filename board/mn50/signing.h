/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Add inline signing to verify records pasased through
 * AUTH haven on Scribe SLT board.
 */

#ifndef __CROS_EC_SIGNER_H
#define __CROS_EC_SIGNER_H

#include "queue.h"
#include "queue_policies.h"

/*
 * Data signing is supported on the following inputs:
 * stream_spi: data received on H1's SPI master.
 * stream_uart: data received on UART1.
 *
 * It's possible to hash multiple streams at once.
 * Currently the hash will print out as console output
 * in hex format when the stream is stopped.
 */
enum stream_id {
	stream_spi = 0,
	stream_uart = 1,
	stream_count = 2,
};

/* Init the signing state and derive keys. Must be called before sig_start. */
void init_signing(void);

/* Zero the existing hash and start processing data in the specified stream. */
int sig_start(enum stream_id id);

/* Append data into the specified stream, if started, otherwise do nothing. */
int sig_append(enum stream_id id, const uint8_t *data, size_t data_len);

/* Stop a stream and sign the hash. The signature will print to the console. */
int sig_sign(enum stream_id id);


struct signer_config {
	enum stream_id id;

	struct producer const producer;
	struct consumer const consumer;
};

extern struct consumer_ops const signer_consumer_ops;
extern struct producer_ops const signer_producer_ops;

void signer_written(struct consumer const *consumer, size_t count);

/*
 * Macro to set up inline signer.
 * Usage: SIGNER_CONFIG(name, stream_id, tx queue, rx queue)
 */
#define SIGNER_CONFIG(NAME, ID, RX_QUEUE, TX_QUEUE)			\
	struct signer_config const NAME = {				\
		.id = ID,						\
		.consumer  = {						\
			.queue = &TX_QUEUE,				\
			.ops   = &signer_consumer_ops,			\
		},							\
		.producer  = {						\
			.queue = &RX_QUEUE,				\
			.ops   = &signer_producer_ops,			\
		},							\
	}
#endif
