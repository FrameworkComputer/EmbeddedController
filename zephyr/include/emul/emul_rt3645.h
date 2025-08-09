/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EMUL_RT3645_H
#define EMUL_RT3645_H

#include <zephyr/drivers/emul.h>
#include <zephyr/sys/slist.h>

/**
 * @brief Read register byte from rt3645 emulator
 *
 * @param emul Pointer to I2C rt3645 emulator
 * @param reg Address of register
 * @param val Pointer where byte to read should be stored
 *
 * @return 0 on success
 * @return -EINVAL when register is out of range defined in rt3645 private
 *                 register or val is NULL
 */
int rt3645_emul_read_reg(const struct emul *emul, int reg, uint8_t *val);

/**
 * @brief Resetting each byte of registers from rt3645 emulator
 *
 * @param emul Pointer to I2C rt3645 emulator
 *
 */
void rt3645_emul_reset_regs(const struct emul *emul);

/**
 * @brief Set rt3645 emulator in config mode. Emulator needs to be in config
 *        mode in order to modify paged registers.
 *
 * @param emul Pointer to I2C rt3645 emulator
 *
 * @return true If emulator is in config mode, false otherwise.
 */
bool rt3645_emul_in_config_mode(const struct emul *emul);

#endif /* EMUL_RT3645_H */
