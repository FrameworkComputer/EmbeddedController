/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for Cros flash emulator
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_FLASH_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_FLASH_H_

/**
 * @brief Reset the protection.
 */
void cros_flash_emul_protect_reset(void);

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_FLASH_H_ */
