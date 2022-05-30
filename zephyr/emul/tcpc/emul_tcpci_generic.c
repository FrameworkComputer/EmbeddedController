/* Copyright 2022 The ChromiumOS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_tcpci_generic_emul

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tcpci_generic_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <ztest.h>

#include "tcpm/tcpci.h"

#include "emul/emul_common_i2c.h"
#include "emul/tcpc/emul_tcpci.h"

/**
 * @brief Function called for each byte of read message from TCPCI emulator
 *
 * @param i2c_emul Pointer to I2C TCPCI emulator
 * @param reg First byte of last write message
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already read
 *
 * @return 0 on success
 * @return -EIO on invalid read request
 */
static int tcpci_generic_emul_read_byte(struct i2c_emul *i2c_emul, int reg,
					uint8_t *val, int bytes)
{
	const struct emul *emul;

	LOG_DBG("TCPCI Generic 0x%x: read reg 0x%x", i2c_emul->addr, reg);

	emul = i2c_emul->parent;

	return tcpci_emul_read_byte(emul, reg, val, bytes);
}

/**
 * @brief Function called for each byte of write message to TCPCI emulator
 *
 * @param i2c_emul Pointer to I2C TCPCI emulator
 * @param reg First byte of write message
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 * @return -EIO on invalid write request
 */
static int tcpci_generic_emul_write_byte(struct i2c_emul *i2c_emul, int reg,
					 uint8_t val, int bytes)
{
	const struct emul *emul;

	LOG_DBG("TCPCI Generic 0x%x: write reg 0x%x", i2c_emul->addr, reg);

	emul = i2c_emul->parent;

	return tcpci_emul_write_byte(emul, reg, val, bytes);
}

/**
 * @brief Function called on the end of write message to TCPCI emulator
 *
 * @param i2c_emul Pointer to I2C TCPCI emulator
 * @param reg Register which is written
 * @param msg_len Length of handled I2C message
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int tcpci_generic_emul_finish_write(struct i2c_emul *i2c_emul, int reg,
					   int msg_len)
{
	const struct emul *emul;

	LOG_DBG("TCPCI Generic 0x%x: finish write reg 0x%x", i2c_emul->addr,
		reg);

	emul = i2c_emul->parent;

	return tcpci_emul_handle_write(emul, reg, msg_len);
}

/**
 * @brief Get currently accessed register, which always equals to selected
 *        register from TCPCI emulator.
 *
 * @param i2c_emul Pointer to I2C TCPCI emulator
 * @param reg First byte of last write message
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int tcpci_generic_emul_access_reg(struct i2c_emul *i2c_emul, int reg,
					 int bytes, bool read)
{
	return reg;
}

/**
 * @brief Function called on reset
 *
 * @param emul Pointer to TCPC emulator
 */
static void tcpci_generic_emul_reset(const struct emul *emul)
{
	tcpci_emul_reset(emul);
}

/**
 * @brief Set up a new TCPCI generic emulator
 *
 * This should be called for each TCPCI Generic device that needs to be
 * emulated.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int tcpci_generic_emul_init(const struct emul *emul,
				   const struct device *parent)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;
	const struct device *i2c_dev;
	int ret;

	i2c_dev = parent;

	tcpci_ctx->common.write_byte = tcpci_generic_emul_write_byte;
	tcpci_ctx->common.finish_write = tcpci_generic_emul_finish_write;
	tcpci_ctx->common.read_byte = tcpci_generic_emul_read_byte;
	tcpci_ctx->common.access_reg = tcpci_generic_emul_access_reg;

	tcpci_emul_i2c_init(emul, i2c_dev);

	ret = i2c_emul_register(i2c_dev, emul->dev_label,
				&tcpci_ctx->common.emul);

	tcpci_generic_emul_reset(emul);

	return ret;
}

#define TCPCI_GENERIC_EMUL(n) \
	TCPCI_EMUL_DEFINE(n, tcpci_generic_emul_init, NULL, NULL)

DT_INST_FOREACH_STATUS_OKAY(TCPCI_GENERIC_EMUL)

#ifdef CONFIG_ZTEST_NEW_API
#define TCPCI_GENERIC_EMUL_RESET_RULE_BEFORE(n) \
	tcpci_generic_emul_reset(&EMUL_REG_NAME(DT_DRV_INST(n)));
static void
tcpci_generic_emul_reset_rule_before(const struct ztest_unit_test *test,
				     void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	DT_INST_FOREACH_STATUS_OKAY(TCPCI_GENERIC_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(tcpci_generic_emul_reset, tcpci_generic_emul_reset_rule_before,
	   NULL);
#endif /* CONFIG_ZTEST_NEW_API */
