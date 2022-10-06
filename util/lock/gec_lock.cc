/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gec_lock.h"
#include "ipc_lock.h"
#include "locks.h"

static struct ipc_lock gec_lock = LOCKFILE_INIT(CROS_EC_LOCKFILE_NAME);

int acquire_gec_lock(int timeout_secs)
{
	return acquire_lock(&gec_lock, timeout_secs * 1000);
}

int release_gec_lock(void)
{
	return release_lock(&gec_lock);
}
