/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/rand.h>

void rand_bytes(void *buf, size_t num)
{
	assert(RAND_bytes(buf, num) == 1);
}
