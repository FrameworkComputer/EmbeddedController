/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#include "driver/cec/bitbang.h"
#include "driver/cec/it83xx.h"

#include <zephyr/devicetree.h>

#define CEC_BITBANG_COMPAT cros_ec_cec_bitbang
#define CEC_IT83XX_COMPAT cros_ec_cec_it83xx
#define CEC_POLICY_COMPAT cros_ec_cec_policy

#define CEC_POLICY_NODE DT_INST(0, CEC_POLICY_COMPAT)
#define CEC_POLICY_CONFIG(node_id)                            \
	{                                                     \
		.command = DT_STRING_TOKEN(node_id, command), \
		.action = DT_STRING_TOKEN(node_id, action),   \
	},

static struct cec_offline_policy default_cec_policy[] = {
#if DT_NODE_EXISTS(CEC_POLICY_NODE)
	DT_FOREACH_CHILD(CEC_POLICY_NODE, CEC_POLICY_CONFIG){ 0 },
#endif
};

#define CEC_DRV_CONFIG_BITBANG(node_id)                                     \
	static const struct bitbang_cec_config node_id##_bitbang_config = { \
		.gpio_out = GPIO_SIGNAL(DT_PHANDLE(node_id, gpio_out)),     \
		.gpio_in = GPIO_SIGNAL(DT_PHANDLE(node_id, gpio_in)),       \
		.gpio_pull_up =                                             \
			GPIO_SIGNAL(DT_PHANDLE(node_id, gpio_pull_up)),     \
		.timer = 0,                                                 \
	};

DT_FOREACH_STATUS_OKAY(CEC_BITBANG_COMPAT, CEC_DRV_CONFIG_BITBANG)

#define CEC_CONFIG_BITBANG(node_id)                                 \
	{                                                           \
		.drv = &bitbang_cec_drv,                            \
		.drv_config = &node_id##_bitbang_config,            \
		.offline_policy = DT_NODE_EXISTS(CEC_POLICY_NODE) ? \
					  default_cec_policy :      \
					  NULL,                     \
	}

#define CEC_CONFIG_IT83XX(node_id)                                  \
	{                                                           \
		.drv = &it83xx_cec_drv, .drv_config = NULL,         \
		.offline_policy = DT_NODE_EXISTS(CEC_POLICY_NODE) ? \
					  default_cec_policy :      \
					  NULL,                     \
	}

#define CHECK_COMPAT(node_id, compat, config) \
	COND_CODE_1(DT_NODE_HAS_COMPAT(node_id, compat), (config(node_id)), ())

#define CEC_CONFIG_FIND(node_id)                                      \
	CHECK_COMPAT(node_id, CEC_BITBANG_COMPAT, CEC_CONFIG_BITBANG) \
	CHECK_COMPAT(node_id, CEC_IT83XX_COMPAT, CEC_CONFIG_IT83XX)

#define CEC_CONFIG_ENTRY(node_id) \
	[CEC_PORT_ID(node_id)] = CEC_CONFIG_FIND(node_id),

test_overridable_const struct cec_config_t cec_config[] = { DT_FOREACH_CHILD(
	CEC_NODE, CEC_CONFIG_ENTRY) };
BUILD_ASSERT(ARRAY_SIZE(cec_config) == CEC_PORT_COUNT);
