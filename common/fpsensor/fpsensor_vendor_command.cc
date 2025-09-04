/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "host_command.h"
#include "system.h"
#include "util.h"

#ifdef CONFIG_PLATFORM_EC_FINGERPRINT_VENDOR_COMMAND
static enum ec_status fp_command_vendor(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const struct ec_params_fp_vendor *>(args->params);
	int ret;

	if (system_is_locked()) {
		return EC_RES_ACCESS_DENIED;
	}

	ret = fp_vendor_command(params->param1,
				reinterpret_cast<uint8_t *>(args->response),
				args->response_max);
	if (ret < 0) {
		return EC_RES_ERROR;
	} else if (ret > args->response_max) {
		/*
		 * This should be handled by the host command subsystem, but we
		 * can't easily test it in the unit tests unless we handle it
		 * here.
		 */
		return EC_RES_RESPONSE_TOO_BIG;
	}
	args->response_size = ret;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_VENDOR, fp_command_vendor, EC_VER_MASK(0));
#endif /* CONFIG_PLATFORM_EC_FINGERPRINT_VENDOR_COMMAND */
