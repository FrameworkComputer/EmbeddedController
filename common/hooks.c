/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System hooks for Chrome EC */

#include "hooks.h"
#include "uart.h"
#include "util.h"

/* Hooks are described in special sections */
extern const struct hook_data __hooks_freq_change[];
extern const struct hook_data __hooks_freq_change_end[];


int hook_notify(enum hook_type type, int stop_on_error)
{
	const struct hook_data *start, *end, *p;
	int count, called = 0;
	int last_prio = HOOK_PRIO_FIRST - 1, prio;
	int rv_error = EC_SUCCESS, rv;

	/* Get the start and end pointers for the hook type */
	switch (type) {
	case HOOK_FREQ_CHANGE:
		start = __hooks_freq_change;
		end = __hooks_freq_change_end;
		break;
	default:
		/* Unhandled hook type */
		return EC_ERROR_UNKNOWN;
	}

	count = ((uint32_t)end - (uint32_t)start) / sizeof(struct hook_data);

	/* Call all the hooks in priority order */
	while (called < count) {
		/* Find the lowest remaining priority */
		for (p = start, prio = HOOK_PRIO_LAST + 1; p < end; p++) {
			if (p->priority < prio && p->priority > last_prio)
				prio = p->priority;
		}
		last_prio = prio;

		/* Call all the hooks with that priority */
		for (p = start; p < end; p++) {
			if (p->priority == prio) {
				called++;
				rv = p->routine();
				if (rv != EC_SUCCESS) {
					if (stop_on_error)
						return rv;
					else if (rv_error == EC_SUCCESS)
						rv_error = rv;
				}
			}
		}
	}

	/* Return the first error seen, if any */
	return rv_error;
}
