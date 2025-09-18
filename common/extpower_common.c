/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "extpower.h"
#include "hooks.h"
#include "host_command.h"

#define EC_HOST_EVENT_AC_CONNECTED_MASK \
	EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_CONNECTED)
#define EC_HOST_EVENT_AC_DISCONNECTED_MASK \
	EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_DISCONNECTED)

__overridable void board_check_extpower(void)
{
}

void extpower_update_host_events(int is_present)
{
	uint8_t *memmap_batt_flags;
	memmap_batt_flags = host_get_memmap(EC_MEMMAP_BATT_FLAG);
	host_event_t mask;

	/* Forward notification to host */
	if (is_present) {
		*memmap_batt_flags |= EC_BATT_FLAG_AC_PRESENT;
		host_set_single_event(EC_HOST_EVENT_AC_CONNECTED);
		mask = EC_HOST_EVENT_AC_DISCONNECTED_MASK;
	} else {
		*memmap_batt_flags &= ~EC_BATT_FLAG_AC_PRESENT;
		host_set_single_event(EC_HOST_EVENT_AC_DISCONNECTED);
		mask = EC_HOST_EVENT_AC_CONNECTED_MASK;
	}

	/* Clear the mask depending upon the charging status. */
	host_clear_events_b(mask);
	host_clear_events(mask);
}

test_mockable void extpower_handle_update(int is_present)
{
	hook_notify(HOOK_AC_CHANGE);

	if (!IS_ENABLED(HAS_TASK_HOSTCMD)) {
		return;
	}

	extpower_update_host_events(is_present);
}
