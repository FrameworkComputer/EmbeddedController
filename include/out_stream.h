/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef INCLUDE_OUT_STREAM_H
#define INCLUDE_OUT_STREAM_H

#include <stddef.h>
#include <stdint.h>

struct out_stream;

/*
 * An out_stream is a generic interface providing operations that can be used
 * to send a character stream over a USB endpoint, UART, I2C host interface, and
 * more.  Each realization of an out_stream provides a constant instance of
 * the out_stream_ops structure that is used to operate on that realizations
 * out_streams.  For example, the UART driver could provide one out_stream_ops
 * structure and four UART configs.  Each UART config uses the same
 * out_stream_ops structure.
 */
struct out_stream_ops {
	/*
	 * Write at most count characters from buffer into the output stream.
	 * Return the number of characters actually written.
	 */
	size_t (*write)(struct out_stream const *stream,
			uint8_t const *buffer,
			size_t count);

	/*
	 * Flush all outgoing data.  This works if we are in an interrupt
	 * context, or normal context.  The call blocks until the output
	 * stream is empty.
	 */
	void (*flush)(struct out_stream const *stream);

};

/*
 * The out_stream structure is embedded in the device configuration structure
 * that wishes to publish an out_stream capable interface.  Uses of that device
 * can pass a pointer to the embedded out_stream around and use it like any
 * other out_stream.
 */
struct out_stream {
	/*
	 * Ready will be called by the stream every time characters are removed
	 * from the stream.  This may be called from an interrupt context
	 * so work done by the ready callback should be minimal.  Likely this
	 * callback will be used to call task_wake, or some similar signaling
	 * mechanism.
	 *
	 * This callback is part of the user configuration of a stream, and not
	 * a stream manipulation function (in_stream_ops).  That means that
	 * each stream can be configured with its own ready callback.
	 *
	 * If no callback functionality is required ready can be specified as
	 * NULL.
	 */
	void (*ready)(struct out_stream const *stream);

	struct out_stream_ops const *ops;
};

/*
 * Helper functions that call the associated stream operation and pass it the
 * given stream.  These help prevent mistakes where one stream is passed to
 * another stream's functions.
 */
size_t out_stream_write(struct out_stream const *stream,
			uint8_t const *buffer,
			size_t count);
void   out_stream_flush(struct out_stream const *stream);
void   out_stream_ready(struct out_stream const *stream);

#endif /* INCLUDE_OUT_STREAM_H */
