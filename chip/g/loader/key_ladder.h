/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_G_LOADER_KEY_LADDER_H
#define __EC_CHIP_G_LOADER_KEY_LADDER_H

#include <stdint.h>
#include <stddef.h>

void key_ladder_step(uint32_t certificate, const uint32_t *input);

#endif  /* ! __EC_CHIP_G_LOADER_KEY_LADDER_H */
