/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/temp_sensor/pct2075.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_pct2075.h"
#include "emul/emul_stub_device.h"
#include "util.h"

#include <zephyr/device.h>

/* NOTE: The emulator doesn't support OS pin */

#define DT_DRV_COMPAT nxp_pct2075

#define PCT2075_TEMP_MAX_MC 127000
#define PCT2075_TEMP_MIN_MC -55000
#define PCT2075_RESOLUTION_MC 125

static const uint16_t default_values[PCT2075_REG_NUMBER] = {
	[PCT2075_REG_TEMP] = 0x00,    [PCT2075_REG_CONF] = 0x00,
	[PCT2075_REG_THYST] = 0x4b00, [PCT2075_REG_TOS] = 0x5000,
	[PCT2075_REG_TIDLE] = 0x00,
};

void pct2075_emul_reset_regs(const struct emul *emul)
{
	struct pct2075_data *data = (struct pct2075_data *)emul->data;

	memcpy(data->regs, default_values, PCT2075_REG_NUMBER + 1);
}

int pct2075_emul_set_temp(const struct emul *emul, int mk)
{
	struct pct2075_data *data = (struct pct2075_data *)emul->data;
	int mc = MILLI_KELVIN_TO_MILLI_CELSIUS(mk);
	int reg;

	if (!IN_RANGE(mc, PCT2075_TEMP_MIN_MC, PCT2075_TEMP_MAX_MC)) {
		return -1;
	}

	/* Divide by the sensor resolution to get register value */
	reg = mc / PCT2075_RESOLUTION_MC;

	/* Use 11 most significant bits. */
	data->regs[PCT2075_REG_TEMP] = reg << 5;

	return 0;
}

int pct2075_emul_read_byte(const struct emul *target, int reg, uint8_t *val,
			   int bytes)
{
	struct pct2075_data *data = (struct pct2075_data *)target->data;

	if (!IN_RANGE(reg, 0, PCT2075_REG_NUMBER - 1)) {
		return -1;
	}

	if (bytes == 0) {
		*val = data->regs[reg] >> 8;
	} else if (bytes == 1) {
		*val = data->regs[reg] & 0x00FF;
	} else {
		/* Support up to 2 bytes read */
		return -1;
	}

	return 0;
}

static int pct2075_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	struct pct2075_data *data = (struct pct2075_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);

	pct2075_emul_reset_regs(emul);

	return 0;
}

#define INIT_PCT2075_EMUL(n)                                           \
	static struct i2c_common_emul_cfg common_cfg_##n;              \
	static struct pct2075_data pct2075_data_##n;                   \
	static struct i2c_common_emul_cfg common_cfg_##n = {           \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),        \
		.data = &pct2075_data_##n.common,                      \
		.addr = DT_INST_REG_ADDR(n)                            \
	};                                                             \
	static struct pct2075_data pct2075_data_##n = {              \
		.common = {                                          \
			.cfg = &common_cfg_##n,                      \
			.read_byte = pct2075_emul_read_byte,         \
		},                                                   \
	}; \
	EMUL_DT_INST_DEFINE(n, pct2075_emul_init, &pct2075_data_##n,   \
			    &common_cfg_##n, &i2c_common_emul_api)

DT_INST_FOREACH_STATUS_OKAY(INIT_PCT2075_EMUL)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
