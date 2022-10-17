/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EMUL_TUSB1064_H
#define EMUL_TUSB1064_H

#include <zephyr/drivers/emul.h>

int tusb1064_emul_peek_reg(const struct emul *emul, int reg);

#endif
