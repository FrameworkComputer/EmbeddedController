/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EMUL_NX20P348X_H
#define EMUL_NX20P348X_H

#include <zephyr/drivers/emul.h>

/**
 * Peek an internal register value
 *
 * @param emul - NX20P383X emulator data
 * @param reg - which register to peek
 * @return register contents
 */
uint8_t nx20p348x_emul_peek(const struct emul *emul, int reg);

/**
 * Set an interrupt in the first interrupt register
 *
 * @param emul - NX20P383X emulator data
 * @param val - value for interrupt register
 */
void nx20p348x_emul_set_interrupt1(const struct emul *emul, uint8_t val);

/**
 * Enable/Disable interact with the TCPC
 *
 * This is used for pretending no TCPC connected on the port.
 * Interaction is default enabled.
 *
 * @param emul - NX20P383X emulator data
 * @param val - value for interrupt register
 */
void nx20p348x_emul_set_tcpc_interact(const struct emul *emul, bool en);

#endif
