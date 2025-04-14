/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_board_info.h"
#include "ec_commands.h"
#include "host_command.h"

#ifdef HOST_TOOLS_BUILD
#include <string.h>
#else
#include "util.h"
#endif

test_mockable int cbi_get_ssfc(uint32_t *ssfc)
{
	uint8_t size = sizeof(*ssfc);

	return cbi_get_board_info(CBI_TAG_SSFC, (uint8_t *)ssfc, &size);
}

test_mockable int cbi_get_fw_config(uint32_t *fw_config)
{
	uint8_t size = sizeof(*fw_config);

	return cbi_get_board_info(CBI_TAG_FW_CONFIG, (uint8_t *)fw_config,
				  &size);
}

static enum ec_status hc_cbi_get(struct host_cmd_handler_args *args)
{
	const struct __ec_align4 ec_params_get_cbi *p = args->params;
	uint8_t size = MIN(args->response_max, UINT8_MAX);

	if (p->flag & CBI_GET_RELOAD) {
		cbi_invalidate_cache();
	}

	if (cbi_get_board_info(p->tag, args->response, &size)) {
		return EC_RES_INVALID_PARAM;
	}

	args->response_size = size;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_CROS_BOARD_INFO, hc_cbi_get, EC_VER_MASK(0));
