/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C arbitration for Chrome EC */

#ifndef __CROS_EC_I2C_ARBITRATION_H
#define __CROS_EC_I2C_ARBITRATION_H

#include "common.h"

#ifdef CONFIG_I2C_ARBITRATION

/**
 * Claim an I2C port for use in master mode.
 *
 * If this function succeeds, you must later call i2c_release() to release the
 * claim.
 *
 * This function must not be called to claim an already-claimed port.
 *
 * @param port	Port to claim (0 for first, 1 for second, etc.)
 * @return 0 if claimed successfully, -1 if it is in use
 */
int i2c_claim(int port);

/**
 * Release an I2C port (after previously being claimed)
 *
 * This function must not be called to release an already-released port.
 *
 * @param port	Port to claim (0 for first, 1 for second, etc.)
 */
void i2c_release(int port);

#else

static inline int i2c_claim(int port) { return EC_SUCCESS; }
static inline void i2c_release(int port) {}

#endif

#endif  /* __CROS_EC_I2C_ARBITRATION_H */
