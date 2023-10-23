/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "emul/tcpc/emul_tcpci.h"
#include "tcpm/tcpci.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT cros_anx7447_tcpc_emul

LOG_MODULE_REGISTER(anx7447_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

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

/**
 * @brief Function called on reset
 *
 * @param emul Pointer to anx7447 emulator
 */
static void anx7447_emul_reset(const struct emul *emul)
{
	tcpci_emul_reset(emul);
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
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;
	const struct device *i2c_dev;

	i2c_dev = parent;

	tcpci_ctx->common.write_byte = anx7447_emul_write_byte;
	tcpci_ctx->common.finish_write = anx7447_emul_finish_write;
	tcpci_ctx->common.read_byte = anx7447_emul_read_byte;
	tcpci_ctx->common.access_reg = anx7447_emul_access_reg;

	tcpci_emul_i2c_init(emul, i2c_dev);

	anx7447_emul_reset(emul);

	return 0;
}

static int i2c_anx7447_emul_transfer(const struct emul *target,
				     struct i2c_msg *msgs, int num_msgs,
				     int addr)
{
	struct tcpc_emul_data *tcpc_data = target->data;
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;

	return i2c_common_emul_transfer_workhorse(target, &tcpci_ctx->common,
						  &tcpc_data->i2c_cfg, msgs,
						  num_msgs, addr);
}

struct i2c_emul_api i2c_anx7447_emul_api = {
	.transfer = i2c_anx7447_emul_transfer,
};

#define ANX7447_EMUL(n)                                     \
	TCPCI_EMUL_DEFINE(n, anx7447_emul_init, NULL, NULL, \
			  &i2c_anx7447_emul_api, NULL)

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
