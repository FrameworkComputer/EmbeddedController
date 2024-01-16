/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "emul/tcpc/emul_anx7447.h"
#include "emul/tcpc/emul_tcpci.h"
#include "tcpm/tcpci.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT cros_anx7447_tcpc_emul
#define TCPCI_VENDOR_REGS_BASE 0x7E
#define REG_MAX 255

LOG_MODULE_REGISTER(anx7447_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

static uint8_t spi_regs_default[REG_MAX] = { 0x0 };
static uint8_t tcpci_extra_regs_default[REG_MAX] = { 0x0 };

struct anx7447_emul_data {
	struct i2c_common_emul_data spi_data;
	uint8_t spi_regs[REG_MAX];
	uint8_t tcpci_extra_regs[REG_MAX];
};

/** Constant configuration of the emulator */
struct anx7447_emul_cfg {
	/** Common I2C configuration used by "hidden" ports */
	const struct i2c_common_emul_cfg spi_cfg;
};

/**
 * @brief Function called for each byte of read message from anx7447 emulator
 *
 * @param emul Pointer to I2C anx7447 emulator
 * @param reg First byte of last write message
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already read
 *
 * @return 0 on success
 * @return -EIO on invalid read request
 */
static int anx7447_emul_read_byte(const struct emul *emul, int reg,
				  uint8_t *val, int bytes)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct anx7447_emul_data *anx7447_data = tcpc_data->chip_data;

	if (reg >= TCPCI_VENDOR_REGS_BASE) {
		*val = anx7447_data->tcpci_extra_regs[reg + bytes];
		return 0;
	}

	return tcpci_emul_read_byte(emul, reg, val, bytes);
}

/**
 * @brief Function called for each byte of write message to anx7447 emulator
 *
 * @param emul Pointer to I2C anx7447 emulator
 * @param reg First byte of write message
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 * @return -EIO on invalid write request
 */
static int anx7447_emul_write_byte(const struct emul *emul, int reg,
				   uint8_t val, int bytes)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct anx7447_emul_data *anx7447_data = tcpc_data->chip_data;

	if (reg >= TCPCI_VENDOR_REGS_BASE) {
		if (bytes == 0) {
			return -EIO;
		}
		anx7447_data->tcpci_extra_regs[reg + bytes - 1] = val;
		return 0;
	}
	return tcpci_emul_write_byte(emul, reg, val, bytes);
}

/**
 * @brief Function called on the end of write message to anx7447 emulator
 *
 * @param emul Pointer to I2C anx7447 emulator
 * @param reg Register which is written
 * @param msg_len Length of handled I2C message
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int anx7447_emul_finish_write(const struct emul *emul, int reg,
				     int msg_len)
{
	if (reg >= TCPCI_VENDOR_REGS_BASE) {
		return 0;
	}
	return tcpci_emul_handle_write(emul, reg, msg_len);
}

/**
 * @brief Get currently accessed register, which always equals to selected
 *        register from anx7447 emulator.
 *
 * @param emul Pointer to I2C anx7447 emulator
 * @param reg First byte of last write message
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int anx7447_emul_access_reg(const struct emul *emul, int reg, int bytes,
				   bool read)
{
	return reg;
}

static int i2c_anx7447_emul_transfer(const struct emul *target,
				     struct i2c_msg *msgs, int num_msgs,
				     int addr)
{
	struct tcpc_emul_data *tcpc_data = target->data;
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;
	struct anx7447_emul_data *anx7447_data = tcpc_data->chip_data;
	const struct anx7447_emul_cfg *anx7447_cfg = target->cfg;
	struct i2c_common_emul_data *common_data;

	if (addr == tcpc_data->i2c_cfg.addr) {
		return i2c_common_emul_transfer_workhorse(target,
							  &tcpci_ctx->common,
							  &tcpc_data->i2c_cfg,
							  msgs, num_msgs, addr);
	} else if (addr == anx7447_cfg->spi_cfg.addr) {
		const struct i2c_common_emul_cfg *common_cfg =
			&anx7447_cfg->spi_cfg;
		common_data = &anx7447_data->spi_data;

		return i2c_common_emul_transfer_workhorse(
			target, common_data, common_cfg, msgs, num_msgs, addr);
	}

	LOG_ERR("Cannot map address %02x", addr);
	return -EIO;
}

struct i2c_emul_api i2c_anx7447_emul_api = {
	.transfer = i2c_anx7447_emul_transfer,
};
/**
 * @brief Function called on reset
 *
 * @param emul Pointer to anx7447 emulator
 */
void anx7447_emul_reset(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct anx7447_emul_data *data = tcpc_data->chip_data;

	tcpci_emul_reset(emul);

	for (int i = 0; i < ARRAY_SIZE(spi_regs_default); i++) {
		data->spi_regs[i] = spi_regs_default[i];
	}

	for (int i = TCPCI_VENDOR_REGS_BASE;
	     i < ARRAY_SIZE(tcpci_extra_regs_default); i++) {
		data->tcpci_extra_regs[i] = tcpci_extra_regs_default[i];
	}
}

int anx7447_emul_peek_spi_reg(const struct emul *emul, int reg)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct anx7447_emul_data *data = tcpc_data->chip_data;

	if (!IN_RANGE(reg, 0, REG_MAX - 1)) {
		return -1;
	}

	return data->spi_regs[reg];
}

void anx7447_emul_set_spi_reg(const struct emul *emul, int reg, int val)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct anx7447_emul_data *data = tcpc_data->chip_data;

	if (!IN_RANGE(reg, 0, REG_MAX - 1)) {
		return;
	}

	data->spi_regs[reg] = val;
}

int anx7447_emul_peek_tcpci_extra_reg(const struct emul *emul, int reg)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct anx7447_emul_data *data = tcpc_data->chip_data;

	if (!IN_RANGE(reg, 0, REG_MAX - 1)) {
		return -1;
	}

	return data->tcpci_extra_regs[reg];
}

void anx7447_emul_set_tcpci_extra_reg(const struct emul *emul, int reg, int val)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct anx7447_emul_data *data = tcpc_data->chip_data;

	if (!IN_RANGE(reg, 0, REG_MAX - 1)) {
		return;
	}

	data->tcpci_extra_regs[reg] = val;
}

/**
 * @brief Set up a new anx7447 emulator
 *
 * This should be called for each anx7447 device that needs to be
 * emulated.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int anx7447_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct anx7447_emul_data *data = tcpc_data->chip_data;
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;
	const struct anx7447_emul_cfg *cfg = emul->cfg;
	const struct device *i2c_dev;
	int ret = 0;

	i2c_dev = parent;

	tcpci_ctx->common.write_byte = anx7447_emul_write_byte;
	tcpci_ctx->common.finish_write = anx7447_emul_finish_write;
	tcpci_ctx->common.read_byte = anx7447_emul_read_byte;
	tcpci_ctx->common.access_reg = anx7447_emul_access_reg;

	tcpci_emul_i2c_init(emul, i2c_dev);

	data->spi_data.emul.api = &i2c_anx7447_emul_api;
	data->spi_data.emul.addr = cfg->spi_cfg.addr;
	data->spi_data.emul.target = emul;
	data->spi_data.i2c = i2c_dev;
	data->spi_data.cfg = &cfg->spi_cfg;
	i2c_common_emul_init(&data->spi_data);

	ret |= i2c_emul_register(i2c_dev, &data->spi_data.emul);

	anx7447_emul_reset(emul);

	return ret;
}

static int anx7447_emul_spi_write_byte(const struct emul *emul, int reg,
				       uint8_t val, int bytes)
{
	struct tcpc_emul_data *tcpc_data;
	struct anx7447_emul_data *data;
	struct i2c_emul *i2c_emul = emul->bus.i2c;

	LOG_DBG("ANX7447 0x%x: read reg 0x%x", i2c_emul->addr, reg);

	tcpc_data = emul->data;
	data = tcpc_data->chip_data;

	if (bytes != 1) {
		LOG_ERR("Writing more than one byte at once");
		return -EIO;
	}

	data->spi_regs[reg] = val;

	return 0;
}

static int anx7447_emul_spi_read_byte(const struct emul *emul, int reg,
				      uint8_t *val, int bytes)
{
	struct tcpc_emul_data *tcpc_data;
	struct anx7447_emul_data *data;
	struct i2c_emul *i2c_emul = emul->bus.i2c;

	LOG_DBG("ANX7447 0x%x: write reg 0x%x", i2c_emul->addr, reg);

	tcpc_data = emul->data;
	data = tcpc_data->chip_data;

	if (bytes != 0) {
		LOG_ERR("Reading more than one byte at once");
		return -EIO;
	}

	*val = data->spi_regs[reg];

	return 0;
}

/** Check description in emul_ps8xxx.h */
struct i2c_common_emul_data *
anx7447_emul_get_i2c_common_data(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct anx7447_emul_data *data = tcpc_data->chip_data;

	return &data->spi_data;
}

#define ANX7447_EMUL(n)                                                   \
	static struct anx7447_emul_data anx7447_emul_data_##n = {       \
		.spi_data = {                                           \
			.write_byte = anx7447_emul_spi_write_byte,      \
			.read_byte = anx7447_emul_spi_read_byte,        \
		},                                                      \
	}; \
	static const struct anx7447_emul_cfg anx7447_emul_cfg_##n = {   \
		.spi_cfg = {                                            \
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)), \
			.data = &anx7447_emul_data_##n.spi_data,        \
			.addr = DT_INST_PROP(n, spi_addr),              \
		},                                                      \
	}; \
	TCPCI_EMUL_DEFINE(n, anx7447_emul_init, &anx7447_emul_cfg_##n,    \
			  &anx7447_emul_data_##n, &i2c_anx7447_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(ANX7447_EMUL)

#ifdef CONFIG_ZTEST
#define ANX7447_EMUL_RESET_RULE_BEFORE(n) \
	anx7447_emul_reset(EMUL_DT_GET(DT_DRV_INST(n)))
static void anx7447_emul_reset_rule_before(const struct ztest_unit_test *test,
					   void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	DT_INST_FOREACH_STATUS_OKAY(ANX7447_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(ANX7447_emul_reset, anx7447_emul_reset_rule_before, NULL);
#endif /* CONFIG_ZTEST */

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
