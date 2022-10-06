/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lazor board-specific SKU configuration */

#ifndef __CROS_EC_SKU_H
#define __CROS_EC_SKU_H

int board_get_version(void);
int board_is_clamshell(void);
int board_has_da9313(void);
int board_has_ln9310(void);
int board_has_buck_ic(void);
int board_has_side_volume_buttons(void);

#endif /* __CROS_EC_SKU_H */
