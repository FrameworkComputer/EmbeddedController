/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "extpower.h"
#include "hooks.h"
#include "host_command.h"

void extpower_handle_update(int is_present)
{
	uint8_t *memmap_batt_flags;

	hook_notify(HOOK_AC_CHANGE);
	memmap_batt_flags = host_get_memmap(EC_MEMMAP_BATT_FLAG);

	/* Forward notification to host */
	if (is_present) {
		*memmap_batt_flags |= EC_BATT_FLAG_AC_PRESENT;
		host_set_single_event(EC_HOST_EVENT_AC_CONNECTED);
	} else {
		*memmap_batt_flags &= ~EC_BATT_FLAG_AC_PRESENT;
		host_set_single_event(EC_HOST_EVENT_AC_DISCONNECTED);
	}
}
