/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_isl923x_emul

#include <device.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>
#include <emul.h>
#include <errno.h>
#include <sys/__assert.h>

#include "driver/charger/isl923x.h"
#include "driver/charger/isl923x_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_isl923x.h"
#include "i2c.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(isl923x_emul, CONFIG_ISL923X_EMUL_LOG_LEVEL);

struct isl923x_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;
};

struct isl923x_emul_cfg {
	/** Common I2C config */
	const struct i2c_common_emul_cfg common;
};

static int emul_isl923x_init(const struct emul *emul,
			     const struct device *parent)
{
	const struct isl923x_emul_cfg *cfg = emul->cfg;
	struct isl923x_emul_data *data = emul->data;

	data->common.emul.api = &i2c_common_emul_api;
	data->common.emul.addr = cfg->common.addr;
	data->common.emul.parent = emul;
	data->common.i2c = parent;
	data->common.cfg = &cfg->common;
	i2c_common_emul_init(&data->common);

	return i2c_emul_register(parent, emul->dev_label, &data->common.emul);
}

#define INIT_ISL923X(n)                                                        \
	static struct isl923x_emul_data isl923x_emul_data_##n = {              \
		.common = {},                                                  \
	};                                                                     \
	static struct isl923x_emul_cfg isl923x_emul_cfg_##n = {                \
	.common = {                                                            \
		.i2c_label = DT_INST_BUS_LABEL(n),                             \
		.dev_label = DT_INST_LABEL(n),                                 \
		.addr = DT_INST_REG_ADDR(n),                                   \
		},                                                             \
	};                                                                     \
	EMUL_DEFINE(emul_isl923x_init, DT_DRV_INST(n), &isl923x_emul_cfg_##n,  \
		    &isl923x_emul_data_##n)

DT_INST_FOREACH_STATUS_OKAY(INIT_ISL923X)
