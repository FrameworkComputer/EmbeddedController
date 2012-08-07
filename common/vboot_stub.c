/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Functions needed by vboot library */

#define _STUB_IMPLEMENTATION_
#include "util.h"
#include "utility.h"

void *Memcpy(void *dest, const void *src, uint64_t n)
{
	return memcpy(dest, src, (size_t)n);
}

void *Memset(void *d, const uint8_t c, uint64_t n)
{
	return memset(d, c, n);
}
