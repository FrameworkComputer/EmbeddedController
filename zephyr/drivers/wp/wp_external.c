/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"
#include "wp_external.h"

static bool unlocked = false;

void disable_write_protect_external(void)
{
	unlocked = true;
}

bool write_protect_is_asserted_external(void)
{
	if (unlocked) {
		return false;
	}

	return true;
}
