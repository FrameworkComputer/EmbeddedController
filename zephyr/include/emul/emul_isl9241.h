/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EMUL_ISL9241_H
#define EMUL_ISL9241_H

#include "emul/emul_common_i2c.h"

#include <zephyr/drivers/emul.h>

/**
 * Get the I2C common data, e.g. to inject register read failures
 *
 * @param emul - ISL9241 emulator data
 * @return I2C common emulator data
 */
struct i2c_common_emul_data *
isl9241_emul_get_i2c_common_data(const struct emul *emul);

/**
 * Peek an internal register value
 *
 * @param emul - ISL9241 emulator data
 * @param reg - which register to peek
 * @return register contents
 */
uint16_t isl9241_emul_peek(const struct emul *emul, int reg);

/**
 * Fake a Vbus voltage presence
 *
 * @param emul - ISL9241 emulator data
 * @param vbus_mv - desired Vbus mV to set
 */
void isl9241_emul_set_vbus(const struct emul *emul, int vbus_mv);

/**
 * Fake a specific Vsys voltage
 *
 * @param emul - ISL9241 emulator data
 * @param vsys_mv - desired Vsys mV to set
 */
void isl9241_emul_set_vsys(const struct emul *emul, int vsys_mv);

#endif
