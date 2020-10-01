/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <common.h>
#include <stdlib.h>

/* Int and Long are same size, just forward to existing Long implementation */
int strtoi(const char *nptr, char **endptr, int base)
{
	return strtol(nptr, endptr, base);
}
BUILD_ASSERT(sizeof(int) == sizeof(long));
