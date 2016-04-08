/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common chipset throttling code for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "host_command.h"
#include "task.h"
#include "throttle_ap.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/*****************************************************************************/
/* This enforces the virtual OR of all throttling sources. */
static struct mutex throttle_mutex;
static uint32_t throttle_request[NUM_THROTTLE_TYPES];

void throttle_ap(enum throttle_level level,
		 enum throttle_type type,
		 enum throttle_sources source)
{
	uint32_t tmpval, bitmask;

	mutex_lock(&throttle_mutex);

	bitmask = (1 << source);

	switch (level) {
	case THROTTLE_ON:
		throttle_request[type] |= bitmask;
		break;
	case THROTTLE_OFF:
		throttle_request[type] &= ~bitmask;
		break;
	}

	tmpval = throttle_request[type];	/* save for printing */

	switch (type) {
	case THROTTLE_SOFT:
#ifdef HAS_TASK_HOSTCMD
		host_throttle_cpu(tmpval);
#endif
		break;
	case THROTTLE_HARD:
#ifdef CONFIG_CHIPSET_CAN_THROTTLE
		chipset_throttle_cpu(tmpval);
#endif
		break;

	case NUM_THROTTLE_TYPES:
		/* Make the compiler shut up. Don't use 'default', because
		 * we still want to catch any new types.
		 */
		break;
	}

	mutex_unlock(&throttle_mutex);

	/* print outside the mutex */
	CPRINTS("set AP throttling type %d to %s (0x%08x)",
		type, tmpval ? "on" : "off", tmpval);

}

/*****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_APTHROTTLE
static int command_apthrottle(int argc, char **argv)
{
	int i;
	uint32_t tmpval;

	for (i = 0; i < NUM_THROTTLE_TYPES; i++) {
		mutex_lock(&throttle_mutex);
		tmpval = throttle_request[i];
		mutex_unlock(&throttle_mutex);

		ccprintf("AP throttling type %d is %s (0x%08x)\n", i,
			 tmpval ? "on" : "off", tmpval);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(apthrottle, command_apthrottle,
			NULL,
			"Display the AP throttling state",
			NULL);
#endif
