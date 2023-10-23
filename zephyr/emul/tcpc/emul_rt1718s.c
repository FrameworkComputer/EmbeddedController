/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/rt1718s.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "emul/tcpc/emul_rt1718s.h"
#include "emul/tcpc/emul_tcpci.h"
#include "tcpm/tcpci.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/slist.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT cros_rt1718s_tcpc_emul

LOG_MODULE_REGISTER(rt1718s_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

static bool is_valid_rt1718s_page1_register(int reg)
{
	switch (reg) {
	case RT1718S_SYS_CTRL1:
	case RT1718S_SYS_CTRL2:
	case RT1718S_SYS_CTRL3:
	case RT1718S_RT_MASK6:
	case RT1718S_RT_INT6:
	case RT1718S_VCON_CTRL3:
	case 0xCF: /* FOD function */
	case RT1718S_RT_MASK1:
	case RT1718S_VCONN_CONTROL_2:
	case RT1718S_FRS_CTRL2:
	case RT1718S_VBUS_CTRL_EN:
	case RT1718S_GPIO_CTRL(RT1718S_GPIO1):
	case RT1718S_GPIO_CTRL(RT1718S_GPIO2):
	case RT1718S_GPIO_CTRL(RT1718S_GPIO3):
	case RT1718S_GPIO1_VBUS_CTRL:
	case RT1718S_GPIO2_VBUS_CTRL:
		return true;
	default:
		return false;
	}
}

static bool is_valid_rt1718s_page2_register(int reg)
{
	int combined_reg_address = (RT1718S_RT2 << 8) | reg;

	if (RT1718S_ADC_CHX_VOL_L(RT1718S_ADC_VBUS1) <= combined_reg_address &&
	    combined_reg_address <= RT1718S_ADC_CHX_VOL_H(RT1718S_ADC_CH11)) {
		return true;
	}

	switch (combined_reg_address) {
	case RT1718S_RT2_SBU_CTRL_01:
	case RT1718S_RT2_BC12_SNK_FUNC:
	case RT1718S_RT2_DPDM_CTR1_DPDM_SET:
	case RT1718S_RT2_VBUS_VOL_CTRL:
	case RT1718S_VCON_CTRL4:
	case RT1718S_ADC_CTRL_01:
		return true;
	default:
		return false;
	}
}

static void add_access_history_entry(struct rt1718s_emul_data *rt1718s_data,
				     int reg, uint8_t val)
{
	struct set_reg_entry_t *entry;

	entry = malloc(sizeof(struct set_reg_entry_t));
	entry->reg = reg;
	entry->val = val;
	entry->access_time = k_uptime_get();
	sys_slist_append(&rt1718s_data->set_private_reg_history, &entry->node);
}

/**
 * @brief Function called on reset
 *
 * @param emul Pointer to rt1718s emulator
 */
static void rt1718s_emul_reset(const struct emul *emul)
{
	struct rt1718s_emul_data *rt1718s_data = emul->data;

	tcpci_emul_reset(emul);
	memset(rt1718s_data->reg_page1, 0, sizeof(rt1718s_data->reg_page1));
	memset(rt1718s_data->reg_page2, 0, sizeof(rt1718s_data->reg_page2));
}

int rt1718s_emul_get_reg(const struct emul *emul, int reg, uint16_t *val)
{
	uint8_t reg_addr;
	uint8_t *reference_page;
	struct rt1718s_emul_data *rt1718s_data = emul->data;

	if ((reg >> 8) == RT1718S_RT2) {
		reg_addr = reg & 0xFF;
		reference_page = rt1718s_data->reg_page2;
	} else if (is_valid_rt1718s_page1_register(reg)) {
		reg_addr = reg;
		reference_page = rt1718s_data->reg_page1;
	} else {
		return tcpci_emul_get_reg(emul, reg, val);
	}

	if (val == NULL || reg_addr > RT1718S_EMUL_REG_COUNT_PER_PAGE) {
		return -EINVAL;
	}

	*val = reference_page[reg_addr];
	return EC_SUCCESS;
}

int rt1718s_emul_set_reg(const struct emul *emul, int reg, uint16_t val)
{
	uint8_t reg_addr;
	uint8_t *reference_page;
	struct rt1718s_emul_data *rt1718s_data = emul->data;

	if ((reg >> 8) == RT1718S_RT2) {
		reg_addr = reg & 0xFF;
		reference_page = rt1718s_data->reg_page2;
	} else if (is_valid_rt1718s_page1_register(reg)) {
		reg_addr = reg;
		reference_page = rt1718s_data->reg_page1;
	} else {
		return tcpci_emul_set_reg(emul, reg, val);
	}

	if (reg_addr > RT1718S_EMUL_REG_COUNT_PER_PAGE) {
		return -EINVAL;
	}

	reference_page[reg_addr] = val;
	return EC_SUCCESS;
}

void rt1718s_emul_reset_set_history(const struct emul *emul)
{
	struct _snode *iter_node;
	struct set_reg_entry_t *iter_entry;
	struct rt1718s_emul_data *rt1718s_data = emul->data;

	while (!sys_slist_is_empty(&(rt1718s_data->set_private_reg_history))) {
		iter_node =
			sys_slist_get(&(rt1718s_data->set_private_reg_history));
		iter_entry = SYS_SLIST_CONTAINER(iter_node, iter_entry, node);
		free(iter_entry);
	}
}

void rt1718s_emul_set_device_id(const struct emul *emul, uint16_t device_id)
{
	switch (device_id) {
	case RT1718S_DEVICE_ID_ES1:
	case RT1718S_DEVICE_ID_ES2:
		tcpci_emul_set_reg(emul, TCPC_REG_BCD_DEV, device_id);
		break;
	default:
		break;
	}
}

static int copy_reg_byte(uint8_t *dst, uint8_t src_reg[], int reg,
			 int read_bytes)
{
	if ((reg + read_bytes) > RT1718S_EMUL_REG_COUNT_PER_PAGE ||
	    dst == NULL) {
		return -EIO;
	}
	*dst = src_reg[reg + read_bytes];
	return EC_SUCCESS;
}

/**
 * @brief Function called for each byte of read message from rt1718s emulator
 *
 * @param emul Pointer to I2C rt1718s emulator
 * @param reg First byte of last write message
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already read
 *
 * @return 0 on success
 * @return -EIO on invalid read request
 */
static int rt1718s_emul_read_byte(const struct emul *emul, int reg,
				  uint8_t *val, int read_bytes)
{
	struct rt1718s_emul_data *rt1718s_data = emul->data;
	int current_page = rt1718s_data->current_page;

	if (current_page == 2) {
		if (reg != RT1718S_RT2) {
			LOG_ERR("The page2 register is selected in previous "
				"transaction, but the following read is to reg "
				"%x instead of %x",
				reg, RT1718S_RT2);
			return -EIO;
		}
		return copy_reg_byte(val, rt1718s_data->reg_page2,
				     rt1718s_data->current_page2_register,
				     read_bytes);
	} else if (is_valid_rt1718s_page1_register(reg)) {
		return copy_reg_byte(val, rt1718s_data->reg_page1, reg,
				     read_bytes);
	} else {
		return tcpci_emul_read_byte(emul, reg, val, read_bytes);
	}
}

/**
 * @brief Function called on the end of write message to rt1718s emulator
 *
 * @param emul Pointer to I2C rt1718s emulator
 * @param reg First byte of last write message
 * @param msg_len Length of handled I2C message
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int rt1718s_emul_finish_read(const struct emul *emul, int reg,
				    int msg_len)
{
	struct rt1718s_emul_data *rt1718s_data = emul->data;

	rt1718s_data->current_page = 1;

	return EC_SUCCESS;
}

/**
 * @brief Function called for each byte of read message from rt1718s emulator
 *
 * @param emul Pointer to I2C rt1718s emulator
 * @param reg First byte of last write message
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already read
 *
 * @return 0 on success
 * @return -EIO on invalid read request
 */

static int rt1718s_emul_write_byte_page1(const struct emul *emul, int reg,
					 uint8_t val, int bytes)
{
	struct rt1718s_emul_data *rt1718s_data = emul->data;

	if (bytes == 2) {
		/*
		 * All register in page1 only has 1 byte, so the write should
		 * not more than 2 bytes.
		 */
		return -EIO;
	}
	rt1718s_data->reg_page1[reg] = val;
	add_access_history_entry(rt1718s_data, reg, val);

	/* Software reset is triggered */
	if (reg == RT1718S_SYS_CTRL3 && (val & RT1718S_SWRESET_MASK)) {
		rt1718s_emul_reset(emul);
	}

	return EC_SUCCESS;
}

static int rt1718s_emul_write_byte_page2(const struct emul *emul, int reg,
					 uint8_t val, int bytes)
{
	struct rt1718s_emul_data *rt1718s_data = emul->data;

	if (bytes == 1) {
		rt1718s_data->current_page = 2;

		if (!is_valid_rt1718s_page2_register(val)) {
			return -EIO;
		}
		rt1718s_data->current_page2_register = val;
	} else {
		int pos = rt1718s_data->current_page2_register + bytes - 2;

		rt1718s_data->reg_page2[pos] = val;
		add_access_history_entry(rt1718s_data, (reg << 8) | pos, val);
	}

	return EC_SUCCESS;
}

/**
 * @brief Function called for each byte of write message to rt1718s emulator
 *
 * @param emul Pointer to I2C rt1718s emulator
 * @param reg First byte of write message
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 * @return -EIO on invalid write request
 */
static int rt1718s_emul_write_byte(const struct emul *emul, int reg,
				   uint8_t val, int bytes)
{
	struct rt1718s_emul_data *rt1718s_data = emul->data;

	if (reg == RT1718S_RT2) {
		return rt1718s_emul_write_byte_page2(emul, reg, val, bytes);
	}

	if (rt1718s_data->current_page == 2) {
		return -EIO;
	}

	if (is_valid_rt1718s_page1_register(reg)) {
		return rt1718s_emul_write_byte_page1(emul, reg, val, bytes);
	} else {
		return tcpci_emul_write_byte(emul, reg, val, bytes);
	}
}

/**
 * @brief Wrapper function of rt1718s_emul_write_byte which reset the current
 *        register page if encounter error.
 *
 * @param emul Pointer to I2C rt1718s emulator
 * @param reg First byte of write message
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 * @return -EIO on invalid write request
 */
static int rt1718s_emul_write_byte_wrapper(const struct emul *emul, int reg,
					   uint8_t val, int bytes)
{
	int err;
	struct rt1718s_emul_data *rt1718s_data = emul->data;

	err = rt1718s_emul_write_byte(emul, reg, val, bytes);
	if (err != EC_SUCCESS) {
		rt1718s_data->current_page = 1;
	}
	return err;
}

/**
 * @brief Function called on the end of write message to rt1718s emulator
 *
 * @param emul Pointer to I2C rt1718s emulator
 * @param reg Register which is written
 * @param msg_len Length of handled I2C message
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int rt1718s_emul_finish_write(const struct emul *emul, int reg,
				     int msg_len)
{
	struct rt1718s_emul_data *rt1718s_data = emul->data;

	if (rt1718s_data->current_page == 2) {
		/* msg_len = 2 is selecting the register in page2. */
		if (msg_len != 2) {
			rt1718s_data->current_page = 1;
		}
		return EC_SUCCESS;
	} else if (is_valid_rt1718s_page1_register(reg)) {
		return EC_SUCCESS;
	} else {
		return tcpci_emul_handle_write(emul, reg, msg_len);
	}
}

/**
 * @brief Get currently accessed register.
 *
 * @param emul Pointer to I2C rt1718s emulator
 * @param reg First byte of last write message
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int rt1718s_emul_access_reg(const struct emul *emul, int reg, int bytes,
				   bool read)
{
	struct rt1718s_emul_data *rt1718s_data = emul->data;

	if (rt1718s_data->current_page == 2) {
		return rt1718s_data->current_page2_register;
	} else {
		return reg;
	}
}

/**
 * @brief Set up a new rt1718s emulator
 *
 * This should be called for each rt1718s device that needs to be
 * emulated.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int rt1718s_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct rt1718s_emul_data *rt1718s_data = emul->data;
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;
	const struct device *i2c_dev;

	i2c_dev = parent;

	tcpci_ctx->common.write_byte = rt1718s_emul_write_byte_wrapper;
	tcpci_ctx->common.finish_write = rt1718s_emul_finish_write;
	tcpci_ctx->common.read_byte = rt1718s_emul_read_byte;
	tcpci_ctx->common.finish_read = rt1718s_emul_finish_read;
	tcpci_ctx->common.access_reg = rt1718s_emul_access_reg;

	tcpci_emul_i2c_init(emul, i2c_dev);

	rt1718s_emul_reset(emul);
	sys_slist_init(&(rt1718s_data->set_private_reg_history));

	return 0;
}

static int i2c_rt1718s_emul_transfer(const struct emul *target,
				     struct i2c_msg *msgs, int num_msgs,
				     int addr)
{
	struct tcpc_emul_data *tcpc_data = target->data;
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;

	return i2c_common_emul_transfer_workhorse(target, &tcpci_ctx->common,
						  &tcpc_data->i2c_cfg, msgs,
						  num_msgs, addr);
}

struct i2c_emul_api i2c_rt1718s_emul_api = {
	.transfer = i2c_rt1718s_emul_transfer,
};

#define RT1718S_EMUL(n)                                                   \
	static uint8_t tcpci_emul_tx_buf_##n[128];                        \
	static struct tcpci_emul_msg tcpci_emul_tx_msg_##n = {            \
		.buf = tcpci_emul_tx_buf_##n,                             \
	};                                                                \
	static struct tcpci_ctx tcpci_ctx##n = {                          \
		.tx_msg = &tcpci_emul_tx_msg_##n,                         \
		.error_on_ro_write = true,                                \
		.error_on_rsvd_write = true,                              \
		.irq_gpio = GPIO_DT_SPEC_INST_GET_OR(n, irq_gpios, {}),   \
	};                                                                \
	static struct rt1718s_emul_data rt1718s_emul_data_##n = {       \
		.embedded_tcpc_emul_data = {                            \
			.tcpci_ctx = &tcpci_ctx##n,                     \
			.i2c_cfg = {                                    \
				.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),\
				.data = &tcpci_ctx##n.common,\
				.addr = DT_INST_REG_ADDR(n), \
			}                                    \
		},                                           \
		.current_page = 1,                           \
	}; \
	EMUL_DT_INST_DEFINE(n, rt1718s_emul_init, &rt1718s_emul_data_##n, \
			    NULL, &i2c_rt1718s_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(RT1718S_EMUL)

#ifdef CONFIG_ZTEST
#define RT1718S_EMUL_RESET_RULE_BEFORE(n) \
	rt1718s_emul_reset(EMUL_DT_GET(DT_DRV_INST(n)))
static void rt1718s_emul_reset_rule_before(const struct ztest_unit_test *test,
					   void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	DT_INST_FOREACH_STATUS_OKAY(RT1718S_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(RT1718S_emul_reset, rt1718s_emul_reset_rule_before, NULL);
#endif /* CONFIG_ZTEST */

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
