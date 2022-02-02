/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for kb raw emulator
 */

#ifndef __EMUL_KB_RAW_H
#define __EMUL_KB_RAW_H

struct device;

/**
 * @brief Raw keyboard emulator backend API
 * @defgroup kb_raw_emul raw keyboard emulator
 * @{
 *
 * The raw keyboard emulator allows for emulating a physical keyboard.
 */

/**
 * @brief Sets or clears a key.
 *
 * @param dev Pointer to kb_raw emulator device.
 * @param row The keyboard scan row.
 * @param col The keyboard scan column.
 * @param pressed If non-zero, set that key row/col as pressed, if zero it is
 *                unpressed.
 * @return 0 for success, -EINVAL for an invalid param.
 */
int emul_kb_raw_set_kbstate(const struct device *dev, uint8_t row, uint8_t col,
			    int pressed);

/**
 * @}
 */

#endif /* __EMUL_KB_RAW_H */
