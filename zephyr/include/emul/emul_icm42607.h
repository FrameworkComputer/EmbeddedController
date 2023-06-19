/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EMUL_ICM42607_H
#define EMUL_ICM42607_H

#include "emul/emul_common_i2c.h"

#include <stdint.h>

#include <zephyr/drivers/emul.h>

void icm42607_emul_reset(const struct emul *emul);

int icm42607_emul_peek_reg(const struct emul *emul, int reg);

int icm42607_emul_write_reg(const struct emul *emul, int reg, int val);

void icm42607_emul_push_fifo(const struct emul *emul, const uint8_t *src,
			     int size);

struct i2c_common_emul_data *
emul_icm42607_get_i2c_common_data(const struct emul *emul);

#endif
