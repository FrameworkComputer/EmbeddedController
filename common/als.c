/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This provides the interface for any Ambient Light Sensors that are connected
 * to the EC instead of the AP.
 */

#include "als.h"
#include "common.h"
#include "console.h"
#include "host_command.h"
#include "task.h"
#include "timer.h"
#include "util.h"

int als_read(enum als_id id, int *lux)
{
	int af = als[id].attenuation_factor;
	return als[id].read(lux, af);
}

void als_task(void)
{
	int i, val;
	uint16_t *mapped = (uint16_t *)host_get_memmap(EC_MEMMAP_ALS);

	while (1) {
		for (i = 0; i < EC_ALS_ENTRIES && i < ALS_COUNT; i++)
			mapped[i] = als_read(i, &val) == EC_SUCCESS ? val : 0;

		task_wait_event(SECOND);
	}
}

/*****************************************************************************/
/* Console commands */

static int command_als(int argc, char **argv)
{
	int i, rv, val;

	for (i = 0; i < ALS_COUNT; i++) {
		ccprintf("%s: ", als[i].name);
		rv = als_read(i, &val);
		switch (rv) {
		case EC_SUCCESS:
			ccprintf("%d lux\n", val);
			break;
		default:
			ccprintf("Error %d\n", rv);
		}
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(als, command_als,
			NULL,
			"Print ALS values",
			NULL);
