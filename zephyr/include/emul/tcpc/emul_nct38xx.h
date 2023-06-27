/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_NCT38XX_H
#define __EMUL_NCT38XX_H

#include <zephyr/drivers/emul.h>

#define NCT38XX_REG_CTRL_OUT_EN_DEFAULT 0x00
#define NCT38XX_REG_CTRL_OUT_EN_RESERVED_MASK GENMASK(7, 7)

#define NCT38XX_REG_VBC_FAULT_CTL_DEFAULT 0x01
#define NCT38XX_REG_VBC_FAULT_CTL_RESERVED_MASK (GENMASK(7, 6) | GENMASK(2, 2))

int nct38xx_emul_get_reg(const struct emul *emul, int r, uint16_t *val);
int nct38xx_emul_set_reg(const struct emul *emul, int r, uint16_t val);

void nct38xx_emul_reset(const struct emul *emul);

#endif /* __EMUL_NCT38XX_H */
