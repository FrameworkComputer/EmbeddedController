/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "in_stream.h"

size_t in_stream_read(struct in_stream const *stream,
		      uint8_t *buffer,
		      size_t count)
{
	return stream->ops->read(stream, buffer, count);
}

void in_stream_ready(struct in_stream const *stream)
{
	if (stream->ready)
		stream->ready(stream);
}
