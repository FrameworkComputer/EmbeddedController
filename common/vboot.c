/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Verified boot module for Chrome EC */

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "system.h"
#include "vboot.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_VBOOT, outstr)
#define CPRINTF(format, args...) cprintf(CC_VBOOT, format, ## args)

/****************************************************************************/
/* Host commands */

static int host_cmd_vboot(struct host_cmd_handler_args *args)
{
	const struct ec_params_vboot_cmd *p = args->params;
	struct ec_params_vboot_cmd *r = args->response;
	uint8_t v;

	switch (p->in.cmd) {
	case VBOOT_CMD_GET_FLAGS:
		v = VBOOT_FLAGS_IMAGE_MASK & system_get_image_copy();
		r->out.get_flags.val = v;
		args->response_size = sizeof(r);
		break;
	case VBOOT_CMD_SET_FLAGS:
		v = p->in.set_flags.val;
		break;
	default:
		CPRINTF("[%T LB bad cmd 0x%x]\n", p->in.cmd);
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_VBOOT_CMD,
		     host_cmd_vboot,
		     EC_VER_MASK(0));
