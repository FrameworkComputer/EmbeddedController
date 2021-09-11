/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for LN9310 emulator
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_LN9310_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_LN9310_H_

#include <emul.h>
#include "driver/ln9310.h"

/**
 * @brief Select the current emulator to use.
 *
 * Currently, only a single ln9310 can be instantiated at any given instance due
 * to how the driver was written. Once this restriction is removed, there's
 * still an issue with the board_get_battery_cell_type() function as it doesn't
 * take a device pointer. This function selects the current LN9310 context which
 * will serve the data for that board function.
 *
 * @param emulator The LN9310 emulator to select.
 */
void ln9310_emul_set_context(const struct emul *emulator);

/**
 * @brief Clear all the emulator data.
 *
 * @param emulator The LN9310 emulator to clear.
 */
void ln9310_emul_reset(const struct emul *emulator);

/**
 * @brief Update the emulator's battery cell type.
 *
 * @param emulator The LN9310 emulator to update.
 * @param type The battery type to use.
 */
void ln9310_emul_set_battery_cell_type(const struct emul *emulator,
				       enum battery_cell_type type);

/**
 * @brief Update the emulator's version number.
 *
 * @param emulator The LN9310 emulator to update.
 * @param version The LN9310 chip version number.
 */
void ln9310_emul_set_version(const struct emul *emulator, int version);

/**
 * @brief Update whether or not the LN9310 is currently getting more than 10V.
 *
 * @param emulator The LN9310 emulator to update.
 * @param is_gt_10v Whether or not the chip is currently getting more than 10V.
 */
void ln9310_emul_set_vin_gt_10v(const struct emul *emulator, bool is_gt_10v);

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_LN9310_H_ */
