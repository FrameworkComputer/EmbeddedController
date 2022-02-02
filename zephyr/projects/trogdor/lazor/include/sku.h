/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lazor board-specific SKU configuration */

#ifndef __ZEPHYR_LAZOR_SKU_H
#define __ZEPHYR_LAZOR_SKU_H

int board_get_version(void);
int board_is_clamshell(void);
int board_has_da9313(void);
int board_has_ln9310(void);
int board_has_buck_ic(void);

#endif /* __ZEPHYR_LAZOR_SKU_H */
