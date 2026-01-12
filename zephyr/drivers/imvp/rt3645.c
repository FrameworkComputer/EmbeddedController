/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "rt3645.h"

#ifdef CONFIG_AP_PWRSEQ_DRIVER
#include <ap_power/ap_pwrseq_sm.h>
#endif

#include <ctype.h>
#include <stdlib.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(imvp_rt3645, LOG_LEVEL_INF);

#define DT_DRV_COMPAT richtek_rt3645

#define RT3645_PAGE_INVALID 0xFF
#define RT3645_LAST_PAGE 0x0D

#define RT3645_LAST_PAGE_REGISTER 0x12

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "only one 'richtek,rt3645' compatible node may be present");

/* Get raw data from devicetree */
static const uint32_t raw_data[] = DT_PROP(DT_DRV_INST(0), update_data);
static const uint8_t expected_crc = DT_PROP(DT_DRV_INST(0), update_crc);

struct rt3645_data_t {
	uint8_t cur_page;
};

struct rt3645_config_t {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec enable_gpio;
};

const struct rt3645_config_t *config;

static struct rt3645_data_t data0 = {
	.cur_page = RT3645_PAGE_INVALID,
};

static const struct rt3645_config_t config0 = {
	.i2c = I2C_DT_SPEC_INST_GET(0),
	.enable_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), enable_gpios),
};

#ifdef CONFIG_IMVP_FACTORY_UPDATE
static void imvp_init_cb(const struct device *dev,
			 const enum ap_pwrseq_state entry,
			 const enum ap_pwrseq_state exit);
#endif

static int rt3645_update(const struct device *dev);

static int rt3645_init(const struct device *dev)
{
	config = dev->config;

#if defined(CONFIG_IMVP_FACTORY_UPDATE)
	static struct ap_pwrseq_state_callback ap_pwrseq_imvp_cb;
	const struct device *ap_pwrseq_dev = ap_pwrseq_get_instance();

	ap_pwrseq_imvp_cb.cb = imvp_init_cb;
	ap_pwrseq_imvp_cb.states_bit_mask = BIT(AP_POWER_STATE_G3);
	ap_pwrseq_register_state_exit_callback(ap_pwrseq_dev,
					       &ap_pwrseq_imvp_cb);
#endif

	return 0;
}

DEVICE_DT_INST_DEFINE(0, rt3645_init, NULL, &data0, &config0, POST_KERNEL,
		      CONFIG_APPLICATION_INIT_PRIORITY, NULL);

static const struct device *rt3645_dev = DEVICE_DT_GET(DT_DRV_INST(0));

static int rt3645_apply_update_data(const struct device *dev)
{
	int rv = 0;
	int prev_page = -1;
	struct rt3645_info update_entries;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}

	/* Apply each register update from the raw_data array */
	for (size_t i = 0; i < ARRAY_SIZE(raw_data); i++) {
		/* Unpack each entry */
		update_entries.page = (raw_data[i] >> 16) & 0xFF;
		update_entries.reg = (raw_data[i] >> 8) & 0xFF;
		update_entries.val = raw_data[i] & 0xFF;

		if (prev_page != update_entries.page) {
			/* Any new page write will need a delay of 1msec */
			k_msleep(1);

			/* Set the correct page */
			rv = rt3645_set_page(dev, update_entries.page);
			/* Return upon set page failure */
			if (rv)
				return rv;
			prev_page = update_entries.page;
		}

		/* Write the register value */
		rv = rt3645_write_reg(dev, update_entries.reg,
				      update_entries.val);
		/* Return upon write register failure */
		if (rv)
			return rv;

		LOG_DBG("page:0x%x Reg:0x%x Data:0x%x ", update_entries.page,
			update_entries.reg, update_entries.val);
	}

	return rv;
}

static int rt3645_crc_check(const struct device *dev)
{
	uint8_t crc_val;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}

	rt3645_set_page(dev, RT3645_PAGE_D);
	rt3645_read_reg(dev, CRC_REG, &crc_val);

	if (crc_val == expected_crc) {
		LOG_DBG("Good CRC value - 0x%0x", crc_val);
		return EC_SUCCESS;
	}

	LOG_ERR("CRC value - 0x%0x  not matching!", crc_val);
	return -EINVAL;
}

static int rt3645_lock_nvm(const struct device *dev)
{
	int rv = 0;
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}

	rt3645_set_page(dev, RT3645_PAGE_GLOBAL);

	rv = rt3645_write_reg(dev, CONFIG_MODE_REG, LOCK_CODE1);

	rv = rt3645_write_reg(dev, CONFIG_MODE_REG, LOCK_CODE2);

	LOG_DBG("NVM Locked!");
	return rv;
}

static int rt3645_nvm_program_status(const struct device *dev, int stat_bit)
{
	int retry_cnt, retry = 0;
	int rv = 1;
	uint8_t status_reg;

	if ((stat_bit == NVM_RELOAD_STAT_BIT) || (stat_bit == NVM_STAT_BITS))
		retry_cnt = 4;
	if (stat_bit == NVM_PRGRM_FINISH_STAT_BIT)
		retry_cnt = 20;
	while (retry < retry_cnt) {
		rt3645_set_page(dev, RT3645_PAGE_GLOBAL);
		rv = rt3645_read_reg(dev, NVM_STAT_REG, &status_reg);

		if (!rv) {
			if (stat_bit == NVM_STAT_BITS) {
				if (status_reg == 0xE0) {
					LOG_DBG("Match to NVM_STAT");
					return EC_SUCCESS;
				}
			} else {
				if (status_reg & BIT(stat_bit)) {
					LOG_DBG("NVM_Reload/Programming Done");
					return EC_SUCCESS;
				}
			}
			/* Status bit value is not as expected */
			rv = -EINVAL;
		}
		retry++;

		/* Delay before next read retrial */
		k_msleep(1);
	}

	return rv;
}

static int rt3645_update(const struct device *dev)
{
	int rv = 0;
	uint8_t reg_val = 0;

	/* unlock configuration */
	const uint8_t config_seq[] = { 0x24, 0x54, 0x02 };

	/* Check NVM status and decide to progress */
	if (rt3645_nvm_program_status(dev, NVM_RELOAD_STAT_BIT) != EC_SUCCESS)
		return -EINVAL;

	if (rt3645_set_cfg_mode(dev, config_seq, 3) != EC_SUCCESS) {
		LOG_ERR("Unlock IMVP Failure");
		return -EINVAL;
	}

	/* Delay of 1ms after setting config */
	k_msleep(1);

	/* Read ID */
	rv = rt3645_read_reg(dev, PRODUCT_ID_REG, &reg_val);
	if (rv) {
		LOG_ERR("Read product id Failure");
		goto lock_imvp;
	}
	if (reg_val != PRODUCT_ID) {
		LOG_ERR("Wrong Product Id");
		goto lock_imvp;
	}
	/* Delay of 1ms after reading product id */
	k_msleep(1);

	rv = rt3645_crc_check(dev);
	if (!rv) {
		LOG_INF(" No IMVP update needed");
		goto lock_imvp;
	}

	rv = rt3645_apply_update_data(dev);
	if (rv) {
		LOG_ERR("Data update Failed");
		goto lock_imvp;
	}

	/* Program  NVM */
	rt3645_store_config(dev);
	/* Wait for 800ms */
	k_msleep(800);
	rv = rt3645_nvm_program_status(dev, NVM_PRGRM_FINISH_STAT_BIT);
	if (rv) {
		LOG_ERR("NVM Programming Failed");
		goto lock_imvp;
	}

	/* Restore NVM */
	rt3645_load_config(dev);
	/* Wait for 100ms */
	k_msleep(100);
	rv = rt3645_nvm_program_status(dev, NVM_STAT_BITS);
	if (rv) {
		LOG_ERR("NVM Relaoding Failed");
		goto lock_imvp;
	}

	rv = rt3645_crc_check(dev);
	if (rv)
		LOG_ERR("CRC Match Failed");

lock_imvp:
	if (rt3645_lock_nvm(dev))
		LOG_ERR("Lock_Faied");

	return rv;
}

static void rt3645_initiate_update(const struct device *dev)
{
	gpio_pin_configure_dt(&config->enable_gpio, GPIO_OUTPUT);
	gpio_pin_set_dt(&config->enable_gpio, 1);

	/* Reasonable delay for voltage to stabilize */
	k_msleep(30);

	if (gpio_pin_get_dt(&config->enable_gpio) == 1) {
		/* Start Updation */
		if (!rt3645_update(rt3645_dev))
			LOG_INF("IMVP Update Success! ");
		else
			LOG_ERR("IMVP update Failed! ");
	}
}

#if defined(CONFIG_IMVP_FACTORY_UPDATE)
static void imvp_init_cb(const struct device *dev,
			 const enum ap_pwrseq_state entry,
			 const enum ap_pwrseq_state exit)
{
	if (entry == AP_POWER_STATE_S5) {
		rt3645_initiate_update(rt3645_dev);
	}
}
#endif

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

	rt3645_set_page(dev, RT3645_PAGE_GLOBAL);
	return rt3645_write_reg(dev, NVM_PRGRM_CTRL_REG, NVM_PRGRM_DAT);
}

#ifdef CONFIG_IMVP_RT3645_CONSOLE

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

static int cmd_rt3645_update(const struct shell *sh, size_t argc, char **argv)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);
	LOG_INF("System shutting_down for IMVP update");

	/* Reasonable delay for system to shutdown */
	k_msleep(100);

	rt3645_initiate_update(rt3645_dev);

	gpio_pin_set_dt(&config->enable_gpio, 0);

	LOG_INF("Press powerbutton/enter 'powerbtn' to boot system");

	return EC_SUCCESS;
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
	SHELL_CMD(update, NULL, "Update IMVP controller\n", cmd_rt3645_update),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(imvp, &sub_imvp_cmds, "IMVP commands", NULL);
#endif /* CONFIG_IMVP_RT3645_CONSOLE */
