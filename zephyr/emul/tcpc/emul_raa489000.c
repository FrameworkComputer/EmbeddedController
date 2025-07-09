/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "emul/tcpc/emul_raa489000.h"
#include "emul/tcpc/emul_tcpci.h"
#include "tcpm/tcpci.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT renesas_raa489000

LOG_MODULE_REGISTER(raa489000_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

static int raa489000_emul_read_byte(const struct emul *emul, int reg,
				    uint8_t *val, int bytes)
{
	return tcpci_emul_read_byte(emul, reg, val, bytes);
}

static int raa489000_emul_write_byte(const struct emul *emul, int reg,
				     uint8_t val, int bytes)
{
	return tcpci_emul_write_byte(emul, reg, val, bytes);
}

static int raa489000_emul_handle_write(const struct emul *emul, int reg,
				       int msg_len)
{
	return tcpci_emul_handle_write(emul, reg, msg_len);
}

void raa489000_emul_reset(const struct emul *emul)
{
	tcpci_emul_reset(emul);
}

static int raa489000_emul_tcpc_access_reg(const struct emul *emul, int reg,
					  int bytes, bool read)
{
	return reg;
}

static const struct {
	uint16_t reg;
	uint16_t val;
} raa489000_rv[] = {
	{ TCPC_REG_VENDOR_ID, 0x45b },
	{ TCPC_REG_PRODUCT_ID, 0x256 },
	{ TCPC_REG_BCD_DEV, 0x105 },
	/*
	 * TCPC_REG_PD_INT_REV is set automatically by
	 * tcpci_emul_set_rev() called as a ZTEST_RULE.
	 */
};

static int raa489000_emul_init(const struct emul *emul,
			       const struct device *parent)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;
	const struct device *i2c_dev = parent;

	tcpci_ctx->common.write_byte = raa489000_emul_write_byte;
	tcpci_ctx->common.finish_write = raa489000_emul_handle_write;
	tcpci_ctx->common.read_byte = raa489000_emul_read_byte;
	tcpci_ctx->common.access_reg = raa489000_emul_tcpc_access_reg;

	tcpci_emul_i2c_init(emul, i2c_dev);
	raa489000_emul_reset(emul);

	for (int i = 0; i < ARRAY_SIZE(raa489000_rv); ++i) {
		zassert_ok(tcpci_emul_set_reg(emul, raa489000_rv[i].reg,
					      raa489000_rv[i].val));
	}

	return 0;
}

static int i2c_raa489000_emul_transfer(const struct emul *target,
				       struct i2c_msg *msgs, int num_msgs,
				       int addr)
{
	struct tcpc_emul_data *tcpc_data = target->data;
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;

	return i2c_common_emul_transfer_workhorse(target, &tcpci_ctx->common,
						  &tcpc_data->i2c_cfg, msgs,
						  num_msgs, addr);
}

static struct i2c_emul_api i2c_raa489000_emul_api = {
	.transfer = i2c_raa489000_emul_transfer,
};

#define RAA489000_EMUL(n)                                     \
	TCPCI_EMUL_DEFINE(n, raa489000_emul_init, NULL, NULL, \
			  &i2c_raa489000_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(RAA489000_EMUL)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
