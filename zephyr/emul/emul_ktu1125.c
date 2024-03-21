/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "driver/ppc/ktu1125.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_ktu1125.h"
#include "emul/emul_stub_device.h"

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/sys/slist.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT zephyr_ktu1125_emul

#define KTU1125_REG_MAX 0x0c
#define REG_IS_VALID(reg) ((reg) >= 0 && (reg) <= KTU1125_REG_MAX)

struct ktu1125_set_reg_entry_t {
	struct _snode node;
	int reg;
	uint8_t val;
	int64_t access_time;
};

struct ktu1125_data {
	struct i2c_common_emul_data common;
	/** GPIO ports connected to the PPC */
	struct gpio_dt_spec irq_gpio;
	uint8_t regs[KTU1125_REG_MAX + 1];
	struct _slist set_private_reg_history;
};

int ktu1125_emul_set_reg(const struct emul *emul, int reg, int val)
{
	struct ktu1125_data *data = emul->data;

	if (!REG_IS_VALID(reg) || !IN_RANGE(val, 0, UINT8_MAX)) {
		return -EINVAL;
	}

	data->regs[reg] = val;

	return EC_SUCCESS;
}

/**
 * @brief Reset the register setting history on a ktu1125 emulator.
 *
 * @param emul Pointer to I2C ktu1125 emulator
 */
static void ktu1125_emul_reset_set_reg_history(const struct emul *emul)
{
	struct _snode *iter_node;
	struct ktu1125_set_reg_entry_t *iter_entry;
	struct ktu1125_data *data = emul->data;

	while (!sys_slist_is_empty(&(data->set_private_reg_history))) {
		iter_node = sys_slist_get(&(data->set_private_reg_history));
		iter_entry = SYS_SLIST_CONTAINER(iter_node, iter_entry, node);
		free(iter_entry);
	}
}

/* Asserts or deasserts the interrupt signal to the EC. */
static void ktu1125_emul_set_irq_pin(const struct ktu1125_data *data,
				     bool assert_irq)
{
	int res = gpio_emul_input_set(data->irq_gpio.port,
				      /* The signal is inverted. */
				      data->irq_gpio.pin, !assert_irq);
	__ASSERT_NO_MSG(res == 0);
}

void ktu1125_emul_assert_irq(const struct emul *emul, bool assert_irq)
{
	const struct ktu1125_data *data = emul->data;

	ktu1125_emul_set_irq_pin(data, assert_irq);
}

void ktu1125_emul_reset(const struct emul *emul)
{
	struct ktu1125_data *data = emul->data;

	ktu1125_emul_set_irq_pin(data, false);
	memset(data->regs, 0, sizeof(data->regs));
	data->regs[KTU1125_ID] = KTU1125_VENDOR_DIE_IDS;
	ktu1125_emul_reset_set_reg_history(emul);
}

static void add_set_reg_history_entry(struct ktu1125_data *data, int reg,
				      uint16_t val)
{
	struct ktu1125_set_reg_entry_t *entry;

	entry = malloc(sizeof(struct ktu1125_set_reg_entry_t));
	entry->reg = reg;
	entry->val = val;
	entry->access_time = k_uptime_get();
	sys_slist_append(&data->set_private_reg_history, &entry->node);
}

static int ktu1125_emul_read(const struct emul *emul, int reg, uint8_t *val,
			     int bytes, void *unused_data)
{
	struct ktu1125_data *data = emul->data;
	const uint8_t *regs = data->regs;
	int pos = reg + bytes;

	if (!REG_IS_VALID(pos)) {
		return -EINVAL;
	}

	*val = regs[pos];

	return EC_SUCCESS;
}

static int ktu1125_emul_write(const struct emul *emul, int reg, uint8_t val,
			      int bytes, void *unused_data)
{
	struct ktu1125_data *data = emul->data;
	uint8_t *regs = data->regs;
	int pos = reg + bytes - 1;
	int sourcing;

	if (!REG_IS_VALID(pos) || !IN_RANGE(val, 0, UINT8_MAX)) {
		return -EINVAL;
	}

	regs[pos] = val;
	add_set_reg_history_entry(data, reg, val);

	switch (pos) {
	case KTU1125_CTRL_SW_CFG:
		sourcing = (val & KTU1125_SW_AB_EN) && (val & KTU1125_POW_MODE);
		regs[KTU1125_MONITOR_SRC] &= ~KTU1125_VBUS_OK;
		regs[KTU1125_MONITOR_SRC] |= sourcing ? KTU1125_VBUS_OK : 0;
		break;

	case KTU1125_ID:
	case KTU1125_MONITOR_SNK:
	case KTU1125_MONITOR_SRC:
	case KTU1125_MONITOR_DATA:
		/*
		 * Reject read-only registers.
		 */
		return -EINVAL;
	}

	return EC_SUCCESS;
}

static int ktu1125_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	struct ktu1125_data *data = (struct ktu1125_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, ktu1125_emul_read, NULL);
	i2c_common_emul_set_write_func(common_data, ktu1125_emul_write, NULL);

	ktu1125_emul_reset(emul);
	sys_slist_init(&(data->set_private_reg_history));

	return 0;
}

#define INIT_KTU1125_EMUL(n)                                            \
	static struct i2c_common_emul_cfg common_cfg_##n;               \
	static struct ktu1125_data ktu1125_data_##n = {                 \
		.common = { .cfg = &common_cfg_##n },                   \
		.irq_gpio = GPIO_DT_SPEC_INST_GET_OR(n, irq_gpios, {}), \
	};                                                              \
	static struct i2c_common_emul_cfg common_cfg_##n = {            \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),         \
		.data = &ktu1125_data_##n.common,                       \
		.addr = DT_INST_REG_ADDR(n)                             \
	};                                                              \
	EMUL_DT_INST_DEFINE(n, ktu1125_emul_init, &ktu1125_data_##n,    \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_KTU1125_EMUL)

#ifdef CONFIG_ZTEST_NEW_API
#define KTU1125_EMUL_RESET_RULE_BEFORE(n) \
	ktu1125_emul_reset(EMUL_DT_GET(DT_DRV_INST(n)))
static void ktu1125_emul_reset_rule_before(const struct ztest_unit_test *test,
					   void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	DT_INST_FOREACH_STATUS_OKAY(KTU1125_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(KTU1125_emul_reset, ktu1125_emul_reset_rule_before, NULL);
#endif /* CONFIG_ZTEST_NEW_API */

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
