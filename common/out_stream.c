/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "out_stream.h"

size_t out_stream_write(struct out_stream const *stream,
			uint8_t const *buffer,
			size_t count)
{
	return stream->ops->write(stream, buffer, count);
}

void out_stream_flush(struct out_stream const *stream)
{
	stream->ops->flush(stream);
}

void out_stream_ready(struct out_stream const *stream)
{
	if (stream->ready)
		stream->ready(stream);
}
