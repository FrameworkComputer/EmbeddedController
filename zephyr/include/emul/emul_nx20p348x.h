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

#endif
