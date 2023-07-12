/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"

#include <zephyr/devicetree.h>

/* TODO(b/270507438): Allow configuring drv, etc using devicetree */
#define CEC_PORT(node_id)               \
	[CEC_PORT_ID(node_id)] = {      \
		.drv = NULL,            \
		.drv_config = NULL,     \
		.offline_policy = NULL, \
	},

const struct cec_config_t cec_config[] = { DT_FOREACH_STATUS_OKAY(cros_ec_cec,
								  CEC_PORT) };
BUILD_ASSERT(ARRAY_SIZE(cec_config) == CEC_PORT_COUNT);
