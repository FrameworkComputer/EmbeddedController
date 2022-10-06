/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>

#include "android.h"

int in_android(void)
{
	if (getenv("ANDROID_ROOT"))
		return 1;

	return 0;
}

char *android_tmpdir_path(void)
{
	return getenv("TMPDIR");
}
