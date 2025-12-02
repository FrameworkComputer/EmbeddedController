/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "emul/tcpc/emul_tcpci.h"
#include "tcpm/tcpci.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT cros_it8xxx2_tcpc_emul

LOG_MODULE_REGISTER(it8xxx2_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

static int it8xxx2_emul_read_byte(const struct emul *emul, int reg,
				  uint8_t *val, int bytes)
{
	return tcpci_emul_read_byte(emul, reg, val, bytes);
}

static int it8xxx2_emul_write_byte(const struct emul *emul, int reg,
				   uint8_t val, int bytes)
{
	return tcpci_emul_write_byte(emul, reg, val, bytes);
}

static int it8xxx2_emul_handle_write(const struct emul *emul, int reg,
				     int msg_len)
{
	return tcpci_emul_handle_write(emul, reg, msg_len);
}

void it8xxx2_emul_reset(const struct emul *emul)
{
	tcpci_emul_reset(emul);
}

static int it8xxx2_emul_tcpc_access_reg(const struct emul *emul, int reg,
					int bytes, bool read)
{
	return reg;
}

static int it8xxx2_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;
	const struct device *i2c_dev = parent;

	tcpci_ctx->common.write_byte = it8xxx2_emul_write_byte;
	tcpci_ctx->common.finish_write = it8xxx2_emul_handle_write;
	tcpci_ctx->common.read_byte = it8xxx2_emul_read_byte;
	tcpci_ctx->common.access_reg = it8xxx2_emul_tcpc_access_reg;

	tcpci_emul_i2c_init(emul, i2c_dev);

	it8xxx2_emul_reset(emul);

	return 0;
}

static int i2c_it8xxx2_emul_transfer(const struct emul *target,
				     struct i2c_msg *msgs, int num_msgs,
				     int addr)
{
	struct tcpc_emul_data *tcpc_data = target->data;
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;

	return i2c_common_emul_transfer_workhorse(target, &tcpci_ctx->common,
						  &tcpc_data->i2c_cfg, msgs,
						  num_msgs, addr);
}

static struct i2c_emul_api i2c_it8xxx2_emul_api = {
	.transfer = i2c_it8xxx2_emul_transfer,
};

#define it8xxx2_EMUL(n)                                     \
	TCPCI_EMUL_DEFINE(n, it8xxx2_emul_init, NULL, NULL, \
			  &i2c_it8xxx2_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(it8xxx2_EMUL)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
