/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Verified boot module for Chrome EC */

#include "board.h"
#include "config.h"
#include "console.h"
#include "eoption.h"
#include "host_command.h"
#include "system.h"
#include "vboot.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_VBOOT, outstr)
#define CPRINTF(format, args...) cprintf(CC_VBOOT, format, ## args)

int vboot_pre_init(void)
{
	/* FIXME(wfrichar): crosbug.com/p/7453: should protect flash */
	return EC_SUCCESS;
}

/****************************************************************************/
/* Host commands */

static int host_cmd_vboot(uint8_t *data, int *resp_size)
{
	struct ec_params_vboot_cmd *ptr =
		(struct ec_params_vboot_cmd *)data;
	uint8_t v;

	switch (ptr->in.cmd) {
	case VBOOT_CMD_GET_FLAGS:
		v = VBOOT_FLAGS_IMAGE_MASK & system_get_image_copy();
		ptr->out.get_flags.val = v;
		*resp_size = sizeof(struct ec_params_vboot_cmd);
		break;
	case VBOOT_CMD_SET_FLAGS:
		v = ptr->in.set_flags.val;
		break;
	default:
		CPRINTF("[%T LB bad cmd 0x%x]\n", ptr->in.cmd);
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_VBOOT_CMD, host_cmd_vboot);
