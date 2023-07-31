/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#ifdef CONFIG_PLATFORM_EC_CEC_IT83XX
#include "driver/cec/it83xx.h"
#endif

#include <zephyr/devicetree.h>

/* TODO(b/287558802): Remove once shim is added for bitbang_cec_drv */
#define CEC_DRV(node_id)                            \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, drv), \
		    (&DT_STRING_TOKEN(node_id, drv)), (NULL))

#define CEC_PORT(node_id)                \
	[CEC_PORT_ID(node_id)] = {       \
		.drv = CEC_DRV(node_id), \
		.drv_config = NULL,      \
		.offline_policy = NULL,  \
	},

test_overridable_const struct cec_config_t cec_config[] = {
	DT_FOREACH_STATUS_OKAY(cros_ec_cec, CEC_PORT)
};
BUILD_ASSERT(ARRAY_SIZE(cec_config) == CEC_PORT_COUNT);
