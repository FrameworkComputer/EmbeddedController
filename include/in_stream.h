/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef INCLUDE_IN_STREAM_H
#define INCLUDE_IN_STREAM_H

#include <stddef.h>
#include <stdint.h>

struct in_stream;

struct in_stream_ops {
	/*
	 * Read at most count characters from the input stream into the user
	 * buffer provided.  Return the number of characters actually read
	 * into the buffer.
	 */
	size_t (*read)(struct in_stream const *stream,
		       uint8_t *buffer,
		       size_t count);
};

struct in_stream {
	/*
	 * Ready will be called by the stream every time new characters are
	 * added to the stream.  This may be called from an interrupt context
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
	void (*ready)(struct in_stream const *stream);

	struct in_stream_ops const *ops;
};

/*
 * Helper functions that call the associated stream operation and pass it the
 * given stream.  These help prevent mistakes where one stream is passed to
 * another stream's functions.
 */
size_t in_stream_read(struct in_stream const *stream,
		      uint8_t *buffer,
		      size_t count);
void   in_stream_ready(struct in_stream const *stream);

#endif /* INCLUDE_IN_STREAM_H */
