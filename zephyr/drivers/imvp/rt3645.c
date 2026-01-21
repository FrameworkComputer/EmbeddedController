/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "rt3645.h"

#include <ctype.h>
#include <stdlib.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/shell/shell.h>

#define DT_DRV_COMPAT richtek_rt3645

#define RT3645_PAGE_INVALID 0xFF
#define RT3645_LAST_PAGE 0x0D

#define RT3645_LAST_PAGE_REGISTER 0x12

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "only one 'richtek,rt3645' compatible node may be present");

struct rt3645_data_t {
	uint8_t cur_page;
};

struct rt3645_config_t {
	struct i2c_dt_spec i2c;
};

int rt3645_read_reg(const struct device *dev, uint8_t reg, uint8_t *val)
{
	const struct rt3645_config_t *config = dev->config;

	return i2c_reg_read_byte_dt(&config->i2c, reg, val);
}

int rt3645_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct rt3645_config_t *config = dev->config;

	return i2c_reg_write_byte_dt(&config->i2c, reg, val);
}

int rt3645_set_cfg_mode(const struct device *dev, const uint8_t *seq,
			uint8_t seq_len)
{
	int rv = 0;

	for (int i = 0; (i < seq_len) && !rv; i++)
		rv = rt3645_write_reg(dev, CONFIG_MODE_REG, seq[i]);

	return rv;
}

int rt3645_set_page(const struct device *dev, uint8_t page)
{
	struct rt3645_data_t *data = dev->data;
	int rv;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}

	rv = rt3645_write_reg(dev, PAGE_SET_REG, page);
	if (rv) {
		return rv;
	}

	data->cur_page = page;

	return rv;
}

int rt3645_load_config(const struct device *dev)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}
	return rt3645_write_reg(dev, NVM_PRGRM_CTRL_REG, NVM_RESTORE_DAT);
}

int rt3645_store_config(const struct device *dev)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}
	return rt3645_write_reg(dev, NVM_PRGRM_CTRL_REG, NVM_PRGRM_DAT);
}

static struct rt3645_data_t data0 = {
	.cur_page = RT3645_PAGE_INVALID,
};

const static struct rt3645_config_t config0 = {
	.i2c = I2C_DT_SPEC_GET(DT_DRV_INST(0)),
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, &data0, &config0, POST_KERNEL,
		      CONFIG_APPLICATION_INIT_PRIORITY, NULL);

#ifdef CONFIG_IMVP_RT3645_CONSOLE
static const struct device *rt3645_dev = DEVICE_DT_GET(DT_DRV_INST(0));

static int cmd_rt3645_enter_cfg_mode(const struct shell *sh, size_t argc,
				     char **argv)
{
	uint8_t unlock_seq[4];
	uint8_t seq_len;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh,
			    "Can not change regsiters while higher than S5\n");
		return -EINVAL;
	}

	seq_len = 0;
	for (int i = 1; i < argc; i++, seq_len++) {
		unlock_seq[i - 1] = strtol(argv[i], NULL, 0);
	}
	return rt3645_set_cfg_mode(rt3645_dev, unlock_seq, seq_len);
}

static int cmd_rt3645_load(const struct shell *sh, size_t argc, char **argv)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh,
			    "Can not change regsiters while higher than S5");
		return -EINVAL;
	}
	return rt3645_load_config(rt3645_dev);
}

static int cmd_rt3645_store(const struct shell *sh, size_t argc, char **argv)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh,
			    "Can not change regsiters while higher than S5");
		return -EINVAL;
	}
	return rt3645_store_config(rt3645_dev);
}

static int cmd_rt3645_set_page(const struct shell *sh, size_t argc, char **argv)
{
	unsigned int arg;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh,
			    "Can not change regsiters while higher than S5");
		return -EINVAL;
	}
	arg = (unsigned int)strtol(argv[1], NULL, 0);

	if (arg > RT3645_LAST_PAGE) {
		shell_error(sh, "Page number out of range");
		return -EINVAL;
	}

	return rt3645_set_page(rt3645_dev, arg);
}

static void dump_reg_range(const struct shell *sh, int low, int high)
{
	uint8_t reg;
	uint8_t regval;
	int rv;

	for (reg = low; reg <= high; reg++) {
		rv = rt3645_read_reg(rt3645_dev, reg, &regval);
		if (!rv)
			shell_fprintf(sh, SHELL_INFO, "[%02Xh] = 0x%02X\n", reg,
				      regval);
		else
			shell_fprintf(sh, SHELL_INFO, "ERROR [%Xh]\n", reg);
	}
}

static int cmd_rt3645_dump_regs(const struct shell *sh, size_t argc,
				char **argv)
{
	struct rt3645_data_t *data = rt3645_dev->data;

	dump_reg_range(sh, 0xEC, 0xEC);
	dump_reg_range(sh, 0xEF, 0xEF);
	dump_reg_range(sh, 0xF9, 0xFA);
	dump_reg_range(sh, 0xFE, 0xFE);

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_print(sh,
			    "To access paged registers put system on G3/S5");
		return -EINVAL;
	}

	if (data->cur_page == RT3645_PAGE_INVALID) {
		shell_print(sh, "No active page set");
		return 0;
	}

	shell_fprintf(sh, SHELL_INFO, "Page: 0x%X\n", (int)data->cur_page);
	dump_reg_range(sh, 0x00, RT3645_LAST_PAGE_REGISTER);
	if (data->cur_page == RT3645_LAST_PAGE) {
		dump_reg_range(sh, (RT3645_LAST_PAGE_REGISTER + 1),
			       (RT3645_LAST_PAGE_REGISTER + 1));
	}

	return 0;
}

static int cmd_rt3645_set_regs(const struct shell *sh, size_t argc, char **argv)
{
	struct rt3645_data_t *data = rt3645_dev->data;
	uint8_t reg, val, last_page_reg;
	int rv = 0;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh,
			    "Can not change regsiters while higher than S5");
		return -EINVAL;
	}

	if (data->cur_page == RT3645_PAGE_INVALID) {
		shell_error(sh, "Not active page set");
		return -EINVAL;
	}

	reg = strtol(argv[1], NULL, 0);

	/* Last register page has an extra register */
	last_page_reg = data->cur_page < RT3645_LAST_PAGE ?
				RT3645_LAST_PAGE_REGISTER :
				RT3645_LAST_PAGE_REGISTER + 1;
	for (int i = 2; (i < argc) && (reg <= last_page_reg); i++, reg++) {
		if (*argv[i] == '-') {
			continue;
		}

		val = strtol(argv[i], NULL, 0);
		rv = rt3645_write_reg(rt3645_dev, reg, val);
		if (rv)
			break;
	}

	return rv;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_imvp_cmds,
	SHELL_CMD_ARG(cfg_mode, NULL,
		      SHELL_HELP("Enter password to set IMVP chip in config mode"
				 "<Password sequence>",
				 NULL),
		      cmd_rt3645_enter_cfg_mode, 1, 4),
	SHELL_CMD(load_cfg, NULL, SHELL_HELP("Load IMVP config from NVM", NULL),
		  cmd_rt3645_load),
	SHELL_CMD(store_cfg, NULL,
		  SHELL_HELP("Store IMVP config into NVM", NULL),
		  cmd_rt3645_store),
	SHELL_CMD_ARG(set_page, NULL,
		      SHELL_HELP("Sets active page for R/W",
				 "<page number in hex>"),
		      cmd_rt3645_set_page, 2, 0),
	SHELL_CMD_ARG(set_regs, NULL,
		      SHELL_HELP("Sets chip paged registers",
				 "<start_reg> [value | - ]"),
		      cmd_rt3645_set_regs, 2, 13),
	SHELL_CMD(dump_regs, NULL,
		  "Dump registers, content depends on current page\n",
		  cmd_rt3645_dump_regs),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(imvp, &sub_imvp_cmds, "IMVP commands", NULL);
#endif /* CONFIG_IMVP_RT3645_CONSOLE */
