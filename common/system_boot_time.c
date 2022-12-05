/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "system.h"
#include "util.h"

#ifdef CONFIG_SYSTEM_BOOT_TIME_LOGGING
/* Content of ap_boot_time will be lost on sysjump */
static struct ap_boot_time_data ap_boot_time;
#endif

/* This function updates timestamp for ap boot time params */
void update_ap_boot_time(enum boot_time_param param)
{
#ifdef CONFIG_SYSTEM_BOOT_TIME_LOGGING
	if (param > RESET_CNT) {
		ccprintf("invalid boot_time_param: %d\n", param);
		return;
	}
	if (param < RESET_CNT) {
		ap_boot_time.timestamp[param] = get_time().val;
		ccprintf("Boot Time: %d, %" PRIu64 "\n", param,
			 ap_boot_time.timestamp[param]);
	}

	switch (param) {
	case PLTRST_LOW:
		ap_boot_time.cnt++;
		break;
	case RESET_CNT:
		ap_boot_time.cnt = 0;
		break;
	default:
		break;
	}
#endif
}

#ifdef CONFIG_SYSTEM_BOOT_TIME_LOGGING
/* Returns system boot time data */
static enum ec_status
host_command_get_boot_time(struct host_cmd_handler_args *args)
{
	struct ap_boot_time_data *boot_time = args->response;

	if (args->response_max < sizeof(*boot_time)) {
		return EC_RES_RESPONSE_TOO_BIG;
	}

	/* update current time */
	update_ap_boot_time(EC_CUR_TIME);

	/* copy data from ap_boot_time struct */
	memcpy(boot_time, &ap_boot_time, sizeof(*boot_time));

	args->response_size = sizeof(*boot_time);

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_GET_BOOT_TIME, host_command_get_boot_time,
		     EC_VER_MASK(0));
#endif
