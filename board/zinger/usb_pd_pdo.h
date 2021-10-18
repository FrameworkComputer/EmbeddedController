/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_ZINGER_USB_PD_PDO_H
#define __CROS_EC_BOARD_ZINGER_USB_PD_PDO_H

/* Max current */
#if defined(BOARD_ZINGER)
#define RATED_CURRENT 3000
#elif defined(BOARD_MINIMUFFIN)
#define RATED_CURRENT 2250
#endif

/* Voltage indexes for the PDOs */
enum volt_idx {
	PDO_IDX_5V = 0,
	PDO_IDX_12V = 1,
	PDO_IDX_20V = 2,

	PDO_IDX_COUNT
};

#define PDO_FIXED_FLAGS (PDO_FIXED_UNCONSTRAINED | PDO_FIXED_DATA_SWAP)

extern const uint32_t pd_src_pdo[3];
extern const int pd_src_pdo_cnt;

#endif /* __CROS_EC_BOARD_ZINGER_USB_PD_PDO_H */
