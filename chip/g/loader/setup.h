/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_G_LOADER_SETUP_H
#define __EC_CHIP_G_LOADER_SETUP_H

#include <stddef.h>
#include <stdint.h>

void checkBuildVersion(void);
void disarmRAMGuards(void);
void halt(void);
void tryLaunch(uint32_t adr, size_t max_size);
void unlockFlashForRW(void);

#endif  /* __EC_CHIP_G_LOADER_SETUP_H */
