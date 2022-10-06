/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_COFFEECAKE_USB_PD_PDO_H
#define __CROS_EC_BOARD_COFFEECAKE_USB_PD_PDO_H

#include "stdint.h"

/* Voltage indexes for the PDOs */
enum volt_idx {
	PDO_IDX_5V = 0,
	PDO_IDX_9V = 1,
	/* TODO: add PPS support */
	PDO_IDX_COUNT
};

extern const uint32_t pd_src_pdo[2];
extern const int pd_src_pdo_cnt;

extern const uint32_t pd_snk_pdo[1];
extern const int pd_snk_pdo_cnt;

#endif /* __CROS_EC_BOARD_COFFEECAKE_USB_PD_PDO_H */
