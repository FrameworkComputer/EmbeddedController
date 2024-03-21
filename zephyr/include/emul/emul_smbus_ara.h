/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_SMBUS_ARA_H

#include <zephyr/drivers/emul.h>

/**
 * @brief Set ARA address to respond with when ARA occurs
 *
 * @param emul Pointer to SMBus ARA emulator
 * @param address  Device address
 */
int emul_smbus_ara_set_address(const struct emul *emul, uint8_t address);

#endif /* __EMUL_SMBUS_ARA_H */
