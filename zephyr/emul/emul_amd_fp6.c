/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/usb_mux/amd_fp6.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "usbc/amd_fp6_usb_mux.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT AMD_FP6_USB_MUX_COMPAT

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(emul_amd_fp6);

/* Target supports only 3-byte reads */
enum amd_fp6_read_bytes {
	AMD_FP6_STATUS,
	AMD_FP6_PORT0,
	AMD_FP6_PORT1,
	AMD_FP6_MAX_REG,
};

struct amd_fp6_data {
	struct i2c_common_emul_data common;
	int finish_delay; /* How many reads before mux set "completes"? */
	int waiting_reads; /* How many reads have we waited to complete? */
	uint8_t last_mux_set; /* Last value of mux set call */
	uint8_t regs[AMD_FP6_MAX_REG];
};

/* Helper for setting the mux register with a value and "done" */
static uint8_t amd_fp6_emul_mux_complete(uint8_t mux_val)
{
	return (AMD_FP6_MUX_PORT_CMD_COMPLETE
		<< AMD_FP6_MUX_PORT_STATUS_OFFSET) |
	       mux_val;
}

void amd_fp6_emul_reset_regs(const struct emul *emul)
{
	struct amd_fp6_data *data = (struct amd_fp6_data *)emul->data;

	/* Default to easy use (ready and no delay) */
	data->finish_delay = 0;

	/* Mux starts in low power mode and ready */
	data->regs[AMD_FP6_STATUS] = AMD_FP6_MUX_PD_STATUS_READY
				     << AMD_FP6_MUX_PD_STATUS_OFFSET;
	data->regs[AMD_FP6_PORT0] =
		amd_fp6_emul_mux_complete(AMD_FP6_MUX_LOW_POWER);
	/* "Port 1" is unused in current code */
	data->regs[AMD_FP6_PORT1] = 0;
}

void amd_fp6_emul_set_delay(const struct emul *emul, int delay_reads)
{
	struct amd_fp6_data *data = (struct amd_fp6_data *)emul->data;

	data->finish_delay = delay_reads;
}

void amd_fp6_emul_set_xbar(const struct emul *emul, bool ready)
{
	struct amd_fp6_data *data = (struct amd_fp6_data *)emul->data;

	data->regs[AMD_FP6_STATUS] = ready ? (AMD_FP6_MUX_PD_STATUS_READY
					      << AMD_FP6_MUX_PD_STATUS_OFFSET) :
					     0;
}

static int amd_fp6_emul_read(const struct emul *emul, int reg, uint8_t *val,
			     int bytes, void *unused_data)
{
	struct amd_fp6_data *data = (struct amd_fp6_data *)emul->data;
	uint8_t *regs = data->regs;
	int pos = reg + bytes;

	if (!IN_RANGE(pos, 0, AMD_FP6_MAX_REG)) {
		return -EINVAL;
	}

	/* Decide if we've finally finished our operation */
	if (pos == AMD_FP6_PORT0 && data->finish_delay > 0) {
		data->waiting_reads++;

		if (((regs[pos] >> AMD_FP6_MUX_PORT_STATUS_OFFSET) ==
		     AMD_FP6_MUX_PORT_CMD_BUSY) &&
		    (data->waiting_reads >= data->finish_delay))
			regs[pos] =
				amd_fp6_emul_mux_complete(data->last_mux_set);
	}

	*val = regs[pos];

	return 0;
}

static int amd_fp6_emul_write(const struct emul *emul, int reg, uint8_t val,
			      int bytes, void *unused_data)
{
	struct amd_fp6_data *data = (struct amd_fp6_data *)emul->data;
	uint8_t *regs = data->regs;

	/* We only support Port 0 */
	if (reg != 0 || bytes != 1)
		return -EINVAL;

	data->last_mux_set = val;

	if (data->finish_delay == 0) {
		regs[AMD_FP6_PORT0] = amd_fp6_emul_mux_complete(val);
	} else {
		data->waiting_reads = 0;
		regs[AMD_FP6_PORT0] = AMD_FP6_MUX_PORT_CMD_BUSY
				      << AMD_FP6_MUX_PORT_STATUS_OFFSET;
	}

	return 0;
}

static int amd_fp6_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	struct amd_fp6_data *data = (struct amd_fp6_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, amd_fp6_emul_read, NULL);
	i2c_common_emul_set_write_func(common_data, amd_fp6_emul_write, NULL);

	amd_fp6_emul_reset_regs(emul);

	return 0;
}

#define INIT_AMD_FP6_EMUL(n)                                         \
	static struct i2c_common_emul_cfg common_cfg_##n;            \
	static struct amd_fp6_data amd_fp6_data_##n;                 \
	static struct i2c_common_emul_cfg common_cfg_##n = {         \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),      \
		.data = &amd_fp6_data_##n.common,                    \
		.addr = DT_INST_REG_ADDR(n)                          \
	};                                                           \
	EMUL_DT_INST_DEFINE(n, amd_fp6_emul_init, &amd_fp6_data_##n, \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_AMD_FP6_EMUL)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

static void amd_fp6_emul_reset_rule_before(const struct ztest_unit_test *test,
					   void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

#define AMD_FP6_EMUL_RESET_RULE_BEFORE(n) \
	amd_fp6_emul_reset_regs(EMUL_DT_GET(DT_DRV_INST(n)))

	DT_INST_FOREACH_STATUS_OKAY(AMD_FP6_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(amd_fp6_usb_mux_emul_reset, amd_fp6_emul_reset_rule_before, NULL);
