/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "driver/tcpm/nct38xx.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "emul/tcpc/emul_nct38xx.h"
#include "emul/tcpc/emul_tcpci.h"
#include "tcpm/tcpci.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

/*
 * Note, compatible is for the parent multi-function device. The TCPC device
 * is a child to the MFD.
 *
 * TODO(b/295024023): nct38xx - Add MFD emulator upstream
 */
#define DT_DRV_COMPAT nuvoton_nct38xx

#define NCT38XX_VENDOR_REG_START 0xC0
#define NCT38XX_VENDOR_REG_END 0xDE
#define NCT38XX_VENDOR_REG_COUNT \
	(NCT38XX_VENDOR_REG_END - NCT38XX_VENDOR_REG_START)

LOG_MODULE_REGISTER(nct38xx_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

/* For vendor specific registers. */
struct nct38xx_register {
	uint8_t reg;
	uint8_t def;
	uint8_t value;
	uint8_t reserved;
};

struct nct38xx_emul_data {
	struct nct38xx_register regs[NCT38XX_VENDOR_REG_COUNT];
};

const static struct nct38xx_register
	default_reg_configs[NCT38XX_VENDOR_REG_COUNT] = {
		{
			.reg = NCT38XX_REG_CTRL_OUT_EN,
			.def = NCT38XX_REG_CTRL_OUT_EN_DEFAULT,
			.reserved = NCT38XX_REG_CTRL_OUT_EN_RESERVED_MASK,
		},
		{
			.reg = NCT38XX_REG_VBC_FAULT_CTL,
			.def = NCT38XX_REG_VBC_FAULT_CTL_DEFAULT,
			.value = NCT38XX_REG_VBC_FAULT_CTL_DEFAULT,
			.reserved = NCT38XX_REG_VBC_FAULT_CTL_RESERVED_MASK,
		}
	};

static struct nct38xx_register *get_register_mut(const struct emul *emul,
						 int reg)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct nct38xx_emul_data *nct38xx = tcpc_data->chip_data;

	for (size_t i = 0; i < ARRAY_SIZE(nct38xx->regs); i++) {
		if (nct38xx->regs[i].reg && nct38xx->regs[i].reg == reg)
			return &nct38xx->regs[i];
	}

	return NULL;
}

static const struct nct38xx_register *
get_register_const(const struct emul *emul, int reg)
{
	return get_register_mut(emul, reg);
}

int nct38xx_emul_get_reg(const struct emul *emul, int r, uint16_t *val)
{
	const struct nct38xx_register *reg = get_register_const(emul, r);

	if (reg) {
		*val = reg->value;
		return 0;
	}

	return tcpci_emul_get_reg(emul, r, val);
}

static int nct38xx_set_vendor_reg_raw(struct nct38xx_register *reg,
				      uint16_t val)
{
	if ((val & reg->reserved) != (reg->def & reg->reserved)) {
		LOG_DBG("Reserved bits modified for reg %02x, val: %02x, \
			default: %02x, reserved: %02x",
			reg->reg, val, reg->def, reg->reserved);
		return -EINVAL;
	}

	reg->value = val;
	return 0;
}

int nct38xx_emul_set_reg(const struct emul *emul, int r, uint16_t val)
{
	struct nct38xx_register *reg = get_register_mut(emul, r);

	if (reg)
		return nct38xx_set_vendor_reg_raw(reg, val);

	return tcpci_emul_set_reg(emul, r, val);
}

static int i2c_nct38xx_emul_transfer(const struct emul *target,
				     struct i2c_msg *msgs, int num_msgs,
				     int addr)
{
	struct tcpc_emul_data *tcpc_data = target->data;
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;

	return i2c_common_emul_transfer_workhorse(target, &tcpci_ctx->common,
						  &tcpc_data->i2c_cfg, msgs,
						  num_msgs, addr);
}

struct i2c_emul_api i2c_nct38xx_emul_api = {
	.transfer = i2c_nct38xx_emul_transfer,
};

static int nct38xx_emul_tcpc_write_byte(const struct emul *emul, int r,
					uint8_t val, int bytes)
{
	struct nct38xx_register *reg = get_register_mut(emul, r);

	if (reg) {
		/* Process vendor-defined register write. */
		if (bytes != 1) {
			LOG_DBG("Write %d bytes to single-byte register %x\n",
				r);
			return -EIO;
		}

		return nct38xx_set_vendor_reg_raw(reg, val);
	}

	return tcpci_emul_write_byte(emul, r, val, bytes);
}

static int nct38xx_emul_tcpc_read_byte(const struct emul *emul, int r,
				       uint8_t *val, int bytes)
{
	const struct nct38xx_register *reg = get_register_const(emul, r);

	if (reg) {
		/* Process vendor-defined register read. */
		if (bytes != 0) {
			LOG_DBG("Read %d bytes from single-byte register %x\n",
				r);
			return -EIO;
		}

		*val = reg->value;
		return 0;
	}

	return tcpci_emul_read_byte(emul, r, val, bytes);
}

static int nct38xx_emul_finish_write(const struct emul *emul, int reg,
				     int msg_len)
{
	/* Always report success for our vendor-specific registers. */
	if (get_register_const(emul, reg))
		return 0;

	return tcpci_emul_handle_write(emul, reg, msg_len);
}

static int nct38xx_emul_access_reg(const struct emul *emul, int reg, int bytes,
				   bool read)
{
	return reg;
}

void nct38xx_emul_reset(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct nct38xx_emul_data *nct38xx = tcpc_data->chip_data;

	memcpy(&nct38xx->regs, default_reg_configs,
	       NCT38XX_VENDOR_REG_COUNT * sizeof(struct nct38xx_register));
	/* Using the setter helps catch any default misconfigs. */
	for (size_t i = 0; i < ARRAY_SIZE(nct38xx->regs); i++) {
		if (nct38xx->regs[i].reg == 0)
			continue;

		nct38xx_emul_set_reg(emul, nct38xx->regs[i].reg,
				     nct38xx->regs[i].def);
	}

	tcpci_emul_reset(emul);
}

static int nct38xx_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;

	tcpci_ctx->common.access_reg = nct38xx_emul_access_reg;
	tcpci_ctx->common.read_byte = nct38xx_emul_tcpc_read_byte;
	tcpci_ctx->common.finish_write = nct38xx_emul_finish_write;
	tcpci_ctx->common.write_byte = nct38xx_emul_tcpc_write_byte;

	tcpci_emul_i2c_init(emul, parent);
	nct38xx_emul_reset(emul);
	return 0;
}

#define NCT38XX_EMUL(n)                                                       \
	static struct nct38xx_emul_data nct38xx_emul_data_##n;                \
	TCPCI_EMUL_DEFINE(n, nct38xx_emul_init, NULL, &nct38xx_emul_data_##n, \
			  &i2c_nct38xx_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(NCT38XX_EMUL);

#define NCT38XX_EMUL_RESET_RULE_AFTER(n) \
	nct38xx_emul_reset(EMUL_DT_GET(DT_DRV_INST(n)));

static void nct38xx_emul_test_reset(const struct ztest_unit_test *test,
				    void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	DT_INST_FOREACH_STATUS_OKAY(NCT38XX_EMUL_RESET_RULE_AFTER)
}

ZTEST_RULE(emul_nct38xx_reset, NULL, nct38xx_emul_test_reset);

#ifndef CONFIG_MFD_NCT38XX
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
#endif
