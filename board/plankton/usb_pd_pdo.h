/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_PLANKTON_USB_PD_PDO_H
#define __CROS_EC_BOARD_PLANKTON_USB_PD_PDO_H

#include "stdint.h"

extern const uint32_t pd_src_pdo[3];

extern const uint32_t pd_snk_pdo[3];
extern const int pd_snk_pdo_cnt;

void board_set_source_cap(enum board_src_cap cap);
int charge_manager_get_source_pdo(const uint32_t **src_pdo, const int port);

#endif /* __CROS_EC_BOARD_PLANKTON_USB_PD_PDO_H */
