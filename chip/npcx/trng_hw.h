/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_NPCX_TRNG_HW_H
#define __EC_CHIP_NPCX_TRNG_HW_H

#include <stddef.h>
#include <stdint.h>

#include <common.h>

void npcx_trng_hw_init(void);
void npcx_trng_hw_off(void);

#endif /* __EC_CHIP_NPCX_TRNG_HW_H */
