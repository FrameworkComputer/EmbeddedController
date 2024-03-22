/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for ANX7447 emulator
 */

#ifndef __EMUL_ANX7447_H
#define __EMUL_ANX7447_H

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

void anx7447_emul_reset(const struct emul *emul);
int anx7447_emul_peek_spi_reg(const struct emul *emul, int reg);
void anx7447_emul_set_spi_reg(const struct emul *emul, int reg, int val);
int anx7447_emul_peek_tcpci_extra_reg(const struct emul *emul, int reg);
void anx7447_emul_set_tcpci_extra_reg(const struct emul *emul, int reg,
				      int val);
struct i2c_common_emul_data *
anx7447_emul_get_i2c_common_data(const struct emul *emul);
#endif /* __EMUL_ANX7447_H */
