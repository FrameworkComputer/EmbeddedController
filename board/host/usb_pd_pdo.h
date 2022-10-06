/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_HOST_USB_PD_PDO_H
#define __CROS_EC_BOARD_HOST_USB_PD_PDO_H

#include "stdint.h"

extern const uint32_t pd_src_pdo[2];
extern const int pd_src_pdo_cnt;

extern const uint32_t pd_snk_pdo[3];
extern const int pd_snk_pdo_cnt;

#endif /* __CROS_EC_BOARD_HOST_USB_PD_PDO_H */
