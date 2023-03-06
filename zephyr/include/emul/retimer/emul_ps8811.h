/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_PS8811_H
#define __EMUL_PS8811_H

#include "driver/retimer/ps8811.h"
#include "emul/emul_common_i2c.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

#define PS811_REG1_MAX (PS8811_REG1_USB_CHAN_B_DE_PS_MSB + 1)

#define PS8811_REG1_USB_CHAN_A_SWING_RESERVED_MASK \
	(GENMASK(7, 7) | GENMASK(3, 0))
#define PS8811_REG1_50OHM_ADJUST_CHAN_B_RESERVED_MASK GENMASK(7, 4)
#define PS8811_REG1_USB_CHAN_B_SWING_RESERVED_MASK GENMASK(7, 3)
#define PS8811_REG1_USB_CHAN_B_DE_PS_LSB_RESERVED_MASK GENMASK(7, 3)
#define PS8811_REG1_USB_CHAN_B_DE_PS_MSB_RESERVED_MASK GENMASK(7, 5)

#define PS8811_REG1_USB_AEQ_LEVEL_DEFAULT 0x37
#define PS8811_REG1_USB_ADE_CONFIG_DEFAULT 0x80
#define PS8811_REG1_USB_BEQ_LEVEL_DEFAULT 0x26
#define PS8811_REG1_USB_BDE_CONFIG_DEFAULT 0x80
#define PS8811_REG1_USB_CHAN_A_SWING_DEFAULT 0x00
#define PS8811_REG1_50OHM_ADJUST_CHAN_B_DEFAULT 0x00
#define PS8811_REG1_USB_CHAN_B_SWING_DEFAULT 0x02
#define PS8811_REG1_USB_CHAN_B_DE_PS_LSB_DEFAULT 0x82
#define PS8811_REG1_USB_CHAN_B_DE_PS_MSB_DEFAULT 0x13

/* Constant configuration of the emulator */
struct ps8811_emul_cfg {
	/*
	 * Each page of the PS8811's register map is located at a different I2C
	 * address.
	 */
	const struct i2c_common_emul_cfg p0_cfg;
	const struct i2c_common_emul_cfg p1_cfg;
};

/*  Run-time data used by the emulator */
struct ps8811_emul_data {
	/* I2C data for each page of the register map. */
	struct i2c_common_emul_data p0_data;
	struct i2c_common_emul_data p1_data;

	/* Page 1 registers */
	uint8_t p1_regs[PS811_REG1_MAX];
};

int ps8811_emul_get_reg0(const struct emul *emulator, int reg, uint8_t *val);
int ps8811_emul_set_reg0(const struct emul *emulator, int reg, uint8_t val);

int ps8811_emul_get_reg1(const struct emul *emulator, int reg, uint8_t *val);
int ps8811_emul_set_reg1(const struct emul *emulator, int reg, uint8_t val);

void ps8811_emul_reset(const struct emul *emul);

#endif /* __EMUL_PS8811_H */
