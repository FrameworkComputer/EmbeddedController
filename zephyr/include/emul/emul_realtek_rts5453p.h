/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for Realtek RTS5453P Type-C Power Delivery Controller
 * emulator
 */

#ifndef __EMUL_REALTEK_RTS5453P_H
#define __EMUL_REALTEK_RTS5453P_H

#include "emul/emul_common_i2c.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

/** @brief Emulated properties */
struct rts5453p_emul_pdc_data {
	uint8_t tbd; /* TBD fill with emulated PD controller data */
};

/**
 * @brief Returns pointer to i2c_common_emul_data for argument emul
 *
 * @param emul Pointer to rts5453p emulator
 * @return Pointer to i2c_common_emul_data from argument emul
 */
struct i2c_common_emul_data *
rts5453p_emul_get_i2c_common_data(const struct emul *emul);

#endif /* __EMUL_REALTEK_RTS5453P_H */
