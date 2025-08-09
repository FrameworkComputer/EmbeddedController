/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_rt3645.h"
#include "util.h"
#include "zephyr/drivers/imvp/rt3645.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul_stub_device.h>

#define DT_DRV_COMPAT richtek_rt3645

#define RT3645_PAGE_MAX 0x0D
#define RT3645_PAGE_REGS_MAX 0x12

#define RT3645_CONFIG_MODE_KEY_MAX 4

#define RT3645_CONFIG_MODE_KEY { 0x24, 0x25, 0x26, 0x27 }

const static uint8_t rt3645_config_mode_key[] = RT3645_CONFIG_MODE_KEY;

struct rt3645_page_regs {
	uint8_t data[RT3645_PAGE_MAX + 1][RT3645_PAGE_REGS_MAX + 1];
};

struct rt3645_page_regs stored_page_regs;

struct rt3645_regs {
	uint8_t nvm_program_ctrl;
	uint8_t page;
	uint8_t product_id;
	struct rt3645_page_regs page_regs;
};

struct rt3645_data {
	struct i2c_common_emul_data common;
	uint8_t config_mode_wr_pos;
	bool in_config_mode;
	uint8_t config_mode_data[RT3645_CONFIG_MODE_KEY_MAX];
	struct rt3645_regs regs;
};

void rt3645_emul_reset_regs(const struct emul *emul)
{
	struct rt3645_data *data = emul->data;
	struct rt3645_regs *regs = &data->regs;

	regs->product_id = 0x45;
	regs->page = 0;
	data->in_config_mode = false;
	data->config_mode_wr_pos = 0;
	memcpy(&regs->page_regs, &stored_page_regs,
	       sizeof(struct rt3645_page_regs));
}

int rt3645_emul_read_reg(const struct emul *emul, int reg, uint8_t *val)
{
	struct rt3645_data *data = emul->data;
	struct rt3645_regs *regs = &data->regs;

	if (val == NULL) {
		return -EINVAL;
	}
	switch (reg) {
	case PAGE_SET_REG:
		*val = regs->page;
		return 0;
	case PRODUCT_ID_REG:
		*val = regs->product_id;
		return 0;
	default:
		break;
	}

	if (reg <= RT3645_PAGE_REGS_MAX) {
		*val = regs->page_regs.data[regs->page][reg];
		return 0;
	}
	return -EINVAL;
}

bool rt3645_emul_in_config_mode(const struct emul *emul)
{
	struct rt3645_data *data = emul->data;

	return data->in_config_mode;
}

static int rt3645_emul_read(const struct emul *emul, int reg, uint8_t *val,
			    int bytes, void *unused_data)
{
	struct rt3645_data *data = emul->data;
	struct rt3645_regs *regs = &data->regs;

	if (val == NULL) {
		return -EINVAL;
	}
	switch (reg) {
	case NVM_STAT_REG:
		break;
	case PAGE_SET_REG:
		*val = regs->page;
		return 0;
	case PRODUCT_ID_REG:
		*val = regs->product_id;
		return 0;
	default:
		break;
	}

	if (reg <= RT3645_PAGE_REGS_MAX) {
		*val = regs->page_regs.data[regs->page][reg];
		return 0;
	}

	return -EINVAL;
}

static int rt3645_emul_write(const struct emul *emul, int reg, uint8_t val,
			     int bytes, void *unused_data)
{
	struct rt3645_data *data = emul->data;
	struct rt3645_regs *regs = &data->regs;

	if (reg != CONFIG_MODE_REG) {
		/* Only kept for continuous register write's */
		data->config_mode_wr_pos = 0;
	}
	switch (reg) {
	case NVM_PRGRM_CTRL_REG:
		if (!data->in_config_mode) {
			return 0;
		}
		if (val == NVM_PRGRM_DAT) {
			memcpy(&stored_page_regs, &regs->page_regs,
			       sizeof(struct rt3645_page_regs));
		} else if (val == NVM_RESTORE_DAT) {
			memcpy(&regs->page_regs, &stored_page_regs,
			       sizeof(struct rt3645_page_regs));
		}
		return 0;
	case PAGE_SET_REG:
		if (data->in_config_mode && val <= RT3645_PAGE_MAX)
			regs->page = val;
		return 0;
	case CONFIG_MODE_REG:
		/* Test that write sequence matches before enabling config mode
		 */
		if (!data->in_config_mode &&
		    data->config_mode_wr_pos < RT3645_CONFIG_MODE_KEY_MAX) {
			data->config_mode_data[data->config_mode_wr_pos++] =
				(uint8_t)val;
			if (data->config_mode_wr_pos ==
				    RT3645_CONFIG_MODE_KEY_MAX &&
			    (memcmp(data->config_mode_data,
				    rt3645_config_mode_key,
				    RT3645_CONFIG_MODE_KEY_MAX) == 0)) {
				data->in_config_mode = true;
			}
		}
		return 0;
	default:
		break;
	}

	if (reg <= RT3645_PAGE_REGS_MAX) {
		/* Emul needs to be in config mode before modifying paged
		 * registers */
		if (data->in_config_mode) {
			regs->page_regs.data[regs->page][reg] = val;
		}
		return 0;
	}

	return -EINVAL;
}

static int rt3645_emul_init(const struct emul *emul,
			    const struct device *parent)
{
	struct rt3645_data *data = (struct rt3645_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, rt3645_emul_read, NULL);
	i2c_common_emul_set_write_func(common_data, rt3645_emul_write, NULL);

	rt3645_emul_reset_regs(emul);

	return 0;
}

#define INIT_RT3645_EMUL(n)                                        \
	static struct i2c_common_emul_cfg common_cfg_##n;          \
	static struct rt3645_data rt3645_data_##n;                 \
	static struct i2c_common_emul_cfg common_cfg_##n = {       \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),    \
		.data = &rt3645_data_##n.common,                   \
		.addr = DT_INST_REG_ADDR(n)                        \
	};                                                         \
	static struct rt3645_data rt3645_data_##n = {              \
		.common = { .cfg = &common_cfg_##n }               \
	};                                                         \
	EMUL_DT_INST_DEFINE(n, rt3645_emul_init, &rt3645_data_##n, \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_RT3645_EMUL)
