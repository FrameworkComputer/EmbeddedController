/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for pi3usb9201 emulator
 */

#ifndef __EMUL_PI3USB9201_H
#define __EMUL_PI3USB9201_H

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

#define PI3USB9201_REG_CTRL_1 0x0
#define PI3USB9201_REG_CTRL_2 0x1
#define PI3USB9201_REG_CLIENT_STS 0x2
#define PI3USB9201_REG_HOST_STS 0x3

/**
 * @brief Set value of given register of pi3usb9201
 *
 * @param emul Pointer to pi3usb9201 emulator
 * @param reg Register address
 * @param val New value of the register
 *
 * @return 0 on success or error
 */
int pi3usb9201_emul_set_reg(const struct emul *emul, int reg, uint8_t val);

/**
 * @brief Get value of given register of pi3usb9201
 *
 * @param emul Pointer to pi3usb9201 emulator
 * @param reg Register address
 * @param val Pointer to write current value of register
 *
 * @return 0 on success or error
 */
int pi3usb9201_emul_get_reg(const struct emul *emul, int reg, uint8_t *val);

#endif /* __EMUL_PI3USB9201_H */
