/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EMUL_PS8743_H
#define EMUL_PS8743_H

#include "emul/emul_common_i2c.h"

#include <zephyr/drivers/emul.h>

int ps8743_emul_peek_reg(const struct emul *emul, int reg);
void ps8743_emul_set_reg(const struct emul *emul, int reg, int val);
void ps8743_emul_reset_regs(const struct emul *emul);
struct i2c_common_emul_data *
ps8743_get_i2c_common_data(const struct emul *emul);
#endif
