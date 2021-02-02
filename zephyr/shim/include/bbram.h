/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_BBRAM_H_
#define ZEPHYR_SHIM_INCLUDE_BBRAM_H_

#include <devicetree.h>

#define BBRAM_SIZE DT_REG_SIZE_BY_NAME(DT_NODELABEL(bbram), memory)
#define BBRAM_ADDR DT_REG_ADDR_BY_NAME(DT_NODELABEL(bbram), memory)
#define BBRAM(offset) REG8(BBRAM_ADDR + offset)
#define BBRAM_BKUP_STS BBRAM(CONFIG_BBRAM_BKUP_STS)

#endif /* ZEPHYR_SHIM_INCLUDE_BBRAM_H_ */
