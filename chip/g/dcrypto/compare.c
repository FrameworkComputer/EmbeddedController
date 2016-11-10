/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"

/* Constant time comparator. */
int DCRYPTO_equals(const void *a, const void *b, size_t len)
{
	size_t i;
	const uint8_t *pa = a;
	const uint8_t *pb = b;
	uint8_t diff = 0;

	for (i = 0; i < len; i++)
		diff |= pa[i] ^ pb[i];

	return !diff;
}
