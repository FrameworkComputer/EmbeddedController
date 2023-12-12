/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_rt1739.h"
#include "emul/emul_stub_device.h"

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/sys/slist.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT zephyr_rt1739_emul

#define RT1739_REG_MAX 0x61

struct rt1739_data {
	struct i2c_common_emul_data common;
	uint8_t regs[RT1739_REG_MAX + 1];
	struct _slist set_private_reg_history;
};

int rt1739_emul_peek_reg(const struct emul *emul, int reg, uint8_t *val)
{
	struct rt1739_data *data = emul->data;

	if (reg > RT1739_REG_MAX) {
		return -EINVAL;
	}
	*val = data->regs[reg];
	return EC_SUCCESS;
}

int rt1739_emul_write_reg(const struct emul *emul, int reg, int val)
{
	struct rt1739_data *data = emul->data;

	if (reg > RT1739_REG_MAX) {
		return -EINVAL;
	}
	data->regs[reg] = val;
	return EC_SUCCESS;
}

struct _snode *rt1739_emul_get_reg_set_history_head(const struct emul *emul)
{
	struct rt1739_data *data = emul->data;

	return sys_slist_peek_head(&data->set_private_reg_history);
}

void rt1739_emul_reset_set_reg_history(const struct emul *emul)
{
	struct _snode *iter_node;
	struct rt1739_set_reg_entry_t *iter_entry;
	struct rt1739_data *data = emul->data;

	while (!sys_slist_is_empty(&(data->set_private_reg_history))) {
		iter_node = sys_slist_get(&(data->set_private_reg_history));
		iter_entry = SYS_SLIST_CONTAINER(iter_node, iter_entry, node);
		free(iter_entry);
	}
}

/**
 * @brief Function called on reset
 *
 * @param emul Pointer to rt1739 emulator
 */
static void rt1739_emul_reset(const struct emul *emul)
{
	struct rt1739_data *data = emul->data;

	memset(data->regs, 0, sizeof(data->regs));
	rt1739_emul_reset_set_reg_history(emul);
}

static void add_set_reg_history_entry(struct rt1739_data *data, int reg,
				      uint16_t val)
{
	struct rt1739_set_reg_entry_t *entry;

	entry = malloc(sizeof(struct rt1739_set_reg_entry_t));
	entry->reg = reg;
	entry->val = val;
	entry->access_time = k_uptime_get();
	sys_slist_append(&data->set_private_reg_history, &entry->node);
}

static int rt1739_emul_read(const struct emul *emul, int reg, uint16_t *val,
			    int bytes, void *unused_data)
{
	struct rt1739_data *data = emul->data;
	uint8_t *regs = data->regs;
	int pos = reg + bytes;

	if (!IN_RANGE(pos, 0, RT1739_REG_MAX)) {
		return -1;
	}
	*val = regs[pos];

	return 0;
}

static int rt1739_emul_write(const struct emul *emul, int reg, uint8_t val,
			     int bytes, void *unused_data)
{
	struct rt1739_data *data = emul->data;
	uint8_t *regs = data->regs;
	int pos = reg + bytes - 1;

	/*
	 * 0xF0, 0xF1, and 0xE0 are registers in hidden mode, and are not
	 * publicly announced registers, so just record the access history and
	 * do not update the register.
	 */
	if (pos == 0xF0 || pos == 0xF1 || pos == 0xE0) {
		add_set_reg_history_entry(data, reg, val);
		return 0;
	}

	if (!IN_RANGE(pos, 0, RT1739_REG_MAX) || !IN_RANGE(val, 0, UINT8_MAX)) {
		return -1;
	}
	regs[pos] = val;
	add_set_reg_history_entry(data, reg, val);

	return 0;
}

static int rt1739_emul_init(const struct emul *emul,
			    const struct device *parent)
{
	struct rt1739_data *data = (struct rt1739_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, rt1739_emul_read, NULL);
	i2c_common_emul_set_write_func(common_data, rt1739_emul_write, NULL);

	rt1739_emul_reset(emul);
	sys_slist_init(&(data->set_private_reg_history));

	return 0;
}

#define INIT_RT1739_EMUL(n)                                        \
	static struct i2c_common_emul_cfg common_cfg_##n;          \
	static struct rt1739_data rt1739_data_##n = {              \
		.common = { .cfg = &common_cfg_##n }               \
	};                                                         \
	static struct i2c_common_emul_cfg common_cfg_##n = {       \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),    \
		.data = &rt1739_data_##n.common,                   \
		.addr = DT_INST_REG_ADDR(n)                        \
	};                                                         \
	EMUL_DT_INST_DEFINE(n, rt1739_emul_init, &rt1739_data_##n, \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_RT1739_EMUL)

#ifdef CONFIG_ZTEST
#define RT1739_EMUL_RESET_RULE_BEFORE(n) \
	rt1739_emul_reset(EMUL_DT_GET(DT_DRV_INST(n)))
static void rt1739_emul_reset_rule_before(const struct ztest_unit_test *test,
					  void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	DT_INST_FOREACH_STATUS_OKAY(RT1739_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(RT1739_emul_reset, rt1739_emul_reset_rule_before, NULL);
#endif /* CONFIG_ZTEST */

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
