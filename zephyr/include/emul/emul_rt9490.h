/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EMUL_RT9490_H
#define EMUL_RT9490_H

#include <zephyr/drivers/emul.h>

void rt9490_emul_reset_regs(const struct emul *emul);

int rt9490_emul_peek_reg(const struct emul *emul, int reg);

int rt9490_emul_write_reg(const struct emul *emul, int reg, int val);

#endif
