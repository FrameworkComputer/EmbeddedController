/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EMUL_PS8743_H
#define EMUL_PS8743_H

#include <zephyr/drivers/emul.h>

int ps8743_emul_peek_reg(const struct emul *emul, int reg);

#endif
