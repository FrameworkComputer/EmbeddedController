/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_ANX7483_H
#define __EMUL_ANX7483_H

#include "driver/retimer/anx7483.h"
#include "driver/retimer/anx7483_public.h"
#include "emul/emul_common_i2c.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

#define ANX7483_LFPS_TIMER_REG_RESERVED_MASK GENMASK(7, 4)
#define ANX7483_ANALOG_STATUS_CTRL_REG_RESERVED_MASK \
	(GENMASK(7, 6) | GENMASK(3, 3))
#define ANX7483_ENABLE_EQ_FLAT_SWING_REG_RESERVED_MASK GENMASK(7, 1)
#define ANX7483_AUX_SNOOPING_CTRL_REG_RESERVED_MASK GENMASK(7, 3)

#define ANX7483_UTX1_PORT_CFG0_REG_RESERVED_MASK GENMASK(3, 0)
#define ANX7483_UTX2_PORT_CFG0_REG_RESERVED_MASK GENMASK(3, 0)
#define ANX7483_URX1_PORT_CFG0_REG_RESERVED_MASK GENMASK(3, 0)
#define ANX7483_URX2_PORT_CFG0_REG_RESERVED_MASK GENMASK(3, 0)
#define ANX7483_DRX1_PORT_CFG0_REG_RESERVED_MASK GENMASK(3, 0)
#define ANX7483_DRX2_PORT_CFG0_REG_RESERVED_MASK GENMASK(3, 0)

#define ANX7483_UTX1_PORT_CFG2_REG_RESERVED_MASK GENMASK(3, 0)
#define ANX7483_UTX2_PORT_CFG2_REG_RESERVED_MASK GENMASK(3, 0)
#define ANX7483_URX1_PORT_CFG2_REG_RESERVED_MASK GENMASK(3, 0)
#define ANX7483_URX2_PORT_CFG2_REG_RESERVED_MASK GENMASK(3, 0)
#define ANX7483_DRX1_PORT_CFG2_REG_RESERVED_MASK GENMASK(3, 0)
#define ANX7483_DRX2_PORT_CFG2_REG_RESERVED_MASK GENMASK(3, 0)

/*
 * See b/230694492#comment12 for why CFG3 doesn't have any reserved bits,
 * contrary to what the documentation says.
 */

#define ANX7483_UTX1_PORT_CFG4_REG_RESERVED_MASK (GENMASK(7, 5) | GENMASK(3, 2))
#define ANX7483_UTX2_PORT_CFG4_REG_RESERVED_MASK (GENMASK(7, 5) | GENMASK(3, 2))
#define ANX7483_URX1_PORT_CFG4_REG_RESERVED_MASK (GENMASK(7, 5) | GENMASK(3, 2))
#define ANX7483_URX2_PORT_CFG4_REG_RESERVED_MASK (GENMASK(7, 5) | GENMASK(3, 2))
#define ANX7483_DRX1_PORT_CFG4_REG_RESERVED_MASK (GENMASK(7, 5) | GENMASK(3, 2))
#define ANX7483_DRX2_PORT_CFG4_REG_RESERVED_MASK (GENMASK(7, 5) | GENMASK(3, 2))
#define ANX7483_DTX1_PORT_CFG4_REG_RESERVED_MASK (GENMASK(7, 5) | GENMASK(3, 2))
#define ANX7483_DTX2_PORT_CFG4_REG_RESERVED_MASK (GENMASK(7, 5) | GENMASK(3, 2))

#define ANX7483_LFPS_TIMER_REG_DEFAULT 0x00
#define ANX7483_ANALOG_STATUS_CTRL_REG_DEFAULT 0x20
#define ANX7483_ENABLE_EQ_FLAT_SWING_REG_DEFAULT 0x00
#define ANX7483_AUX_SNOOPING_CTRL_REG_DEFAULT ANX7483_AUX_SNOOPING_DEF
#define ANX7483_CHIP_ID_DEFAULT 0x00

#define ANX7483_UTX1_PORT_CFG0_REG_DEFAULT ANX7483_CFG0_DEF
#define ANX7483_UTX2_PORT_CFG0_REG_DEFAULT ANX7483_CFG0_DEF
#define ANX7483_URX1_PORT_CFG0_REG_DEFAULT ANX7483_CFG0_DEF
#define ANX7483_URX2_PORT_CFG0_REG_DEFAULT ANX7483_CFG0_DEF
#define ANX7483_DRX1_PORT_CFG0_REG_DEFAULT ANX7483_CFG0_DEF
#define ANX7483_DRX2_PORT_CFG0_REG_DEFAULT ANX7483_CFG0_DEF
#define ANX7483_AUX_CFG_0_DEFAULT ANX7483_CFG0_DEF

#define ANX7483_UTX1_PORT_CFG1_REG_DEFAULT ANX7483_CFG1_DEF
#define ANX7483_UTX2_PORT_CFG1_REG_DEFAULT ANX7483_CFG1_DEF
#define ANX7483_URX1_PORT_CFG1_REG_DEFAULT ANX7483_CFG1_DEF
#define ANX7483_URX2_PORT_CFG1_REG_DEFAULT ANX7483_CFG1_DEF
#define ANX7483_DRX1_PORT_CFG1_REG_DEFAULT ANX7483_CFG1_DEF
#define ANX7483_DRX2_PORT_CFG1_REG_DEFAULT ANX7483_CFG1_DEF
#define ANX7483_AUX_CFG_1_DEFAULT ANX7483_CFG1_DEF

#define ANX7483_UTX1_PORT_CFG2_REG_DEFAULT ANX7483_CFG2_DEF
#define ANX7483_UTX2_PORT_CFG2_REG_DEFAULT ANX7483_CFG2_DEF
#define ANX7483_URX1_PORT_CFG2_REG_DEFAULT ANX7483_CFG2_DEF
#define ANX7483_URX2_PORT_CFG2_REG_DEFAULT ANX7483_CFG2_DEF
#define ANX7483_DRX1_PORT_CFG2_REG_DEFAULT ANX7483_CFG2_DEF
#define ANX7483_DRX2_PORT_CFG2_REG_DEFAULT ANX7483_CFG2_DEF

#define ANX7483_UTX1_PORT_CFG3_REG_DEFAULT 0x02
#define ANX7483_UTX2_PORT_CFG3_REG_DEFAULT 0x02
#define ANX7483_URX1_PORT_CFG3_REG_DEFAULT 0x02
#define ANX7483_URX2_PORT_CFG3_REG_DEFAULT 0x02
#define ANX7483_DRX1_PORT_CFG3_REG_DEFAULT 0x02
#define ANX7483_DRX2_PORT_CFG3_REG_DEFAULT 0x02
#define ANX7483_DTX1_PORT_CFG3_REG_DEFAULT 0x02
#define ANX7483_DTX2_PORT_CFG3_REG_DEFAULT 0x02

#define ANX7483_UTX1_PORT_CFG4_REG_DEFAULT 0x62
#define ANX7483_UTX2_PORT_CFG4_REG_DEFAULT 0x62
#define ANX7483_URX1_PORT_CFG4_REG_DEFAULT 0x62
#define ANX7483_URX2_PORT_CFG4_REG_DEFAULT 0x62
#define ANX7483_DRX1_PORT_CFG4_REG_DEFAULT 0x62
#define ANX7483_DRX2_PORT_CFG4_REG_DEFAULT 0x62
#define ANX7483_DTX1_PORT_CFG4_REG_DEFAULT 0x62
#define ANX7483_DTX2_PORT_CFG4_REG_DEFAULT 0x62

#define ANX7483_REG_MAX (ANX7483_DRX1_PORT_CFG4_REG + 1)

/* Constant configuration of the emulator */
struct anx7483_emul_cfg {
	const struct i2c_common_emul_cfg common;
};

struct anx7483_register {
	uint8_t reg;
	uint8_t value;
	uint8_t def;
	uint8_t reserved;
};

struct anx7483_emul_data {
	struct i2c_common_emul_data common;

	struct anx7483_register regs[ANX7483_REG_MAX];
};

int anx7483_emul_get_reg(const struct emul *emul, int r, uint8_t *val);
int anx7483_emul_set_reg(const struct emul *emul, int r, uint8_t val);

/*
 * Some bits that are marked as reserved are used for board-specific tuning.
 * This function allows board tests to pass by allowing them to update reserved
 * masks for their use cases.
 */
int anx7483_emul_set_reg_reserved_mask(const struct emul *emul, int r,
				       uint8_t mask, uint8_t def);

int anx7483_emul_get_eq(const struct emul *emul, enum anx7483_tune_pin pin,
			enum anx7483_eq_setting *eq);

int anx7483_emul_get_fg(const struct emul *emul, enum anx7483_tune_pin pin,
			enum anx7483_fg_setting *fg);

void anx7483_emul_reset(const struct emul *emul);

int anx7483_emul_validate_tuning(const struct emul *emul,
				 const struct anx7483_tuning_set *tuning,
				 size_t tuning_count);

#endif /* __EMUL_ANX7483_H */
