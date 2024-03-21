/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_kb8010.h"
#include "emul/emul_stub_device.h"
#include "usbc/kb8010_usb_mux.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT zephyr_kb8010_emul

#define KB8010_REG_MAX 0xffff

struct kb8010_data {
	struct i2c_common_emul_data common;

	/* GPIO ports connected to the kb8010 */
	struct gpio_dt_spec reset_gpio; /* "reset-pin" */

	uint8_t regs[KB8010_REG_MAX + 1];
};

static void kb8010_emul_reset_regs(const struct emul *emul)
{
	struct kb8010_data *data = (struct kb8010_data *)emul->data;

	memset(data->regs, 0, sizeof(data->regs));
}

void kb8010_emul_set_reset(const struct emul *emul, bool assert_reset)
{
	const struct kb8010_data *data = emul->data;

	int res = gpio_emul_input_set(data->reset_gpio.port,
				      /* The signal is inverted. */
				      data->reset_gpio.pin, !assert_reset);
	__ASSERT_NO_MSG(res == 0);
}

static int kb8010_emul_read(const struct emul *emul, int reg, uint8_t *val,
			    int bytes, void *unused_data)
{
	struct kb8010_data *data = emul->data;
	const uint8_t *regs = data->regs;
	int pos = reg + bytes;

	if (!IN_RANGE(pos, 0, KB8010_REG_MAX)) {
		return -EINVAL;
	}

	*val = regs[pos];

	return EC_SUCCESS;
}

static int kb8010_emul_write(const struct emul *emul, int reg, uint8_t val,
			     int bytes, void *unused_data)
{
	struct kb8010_data *data = emul->data;
	uint8_t *regs = data->regs;
	int pos = reg + bytes - 1;

	if (!IN_RANGE(pos, 0, KB8010_REG_MAX) || !IN_RANGE(val, 0, UINT8_MAX)) {
		return -EINVAL;
	}

	regs[pos] = val;

	return EC_SUCCESS;
}

static int kb8010_emul_init(const struct emul *emul,
			    const struct device *parent)
{
	struct kb8010_data *data = emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, kb8010_emul_read, NULL);
	i2c_common_emul_set_write_func(common_data, kb8010_emul_write, NULL);

	kb8010_emul_reset_regs(emul);

	return EC_SUCCESS;
}

#define INIT_KB8010_EMUL(n)                                               \
	static struct i2c_common_emul_cfg common_cfg_##n;                 \
	static struct kb8010_data kb8010_data_##n = {                     \
		.common = { .cfg = &common_cfg_##n },                     \
		.reset_gpio = GPIO_DT_SPEC_INST_GET(n, emul_reset_gpios), \
	};                                                                \
	static struct i2c_common_emul_cfg common_cfg_##n = {              \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),           \
		.data = &kb8010_data_##n.common,                          \
		.addr = DT_INST_REG_ADDR(n)                               \
	};                                                                \
	EMUL_DT_INST_DEFINE(n, kb8010_emul_init, &kb8010_data_##n,        \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_KB8010_EMUL)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
