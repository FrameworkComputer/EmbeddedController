/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"
#include "usb_pd.h"
#include "usb_pd_pdo.h"

/* Power Delivery Objects */
const uint32_t pd_src_pdo[] = {
	[PDO_IDX_5V]  = PDO_FIXED(5000,  RATED_CURRENT, PDO_FIXED_FLAGS),
	[PDO_IDX_12V] = PDO_FIXED(12000, RATED_CURRENT, PDO_FIXED_FLAGS),
	[PDO_IDX_20V] = PDO_FIXED(20000, RATED_CURRENT, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);
BUILD_ASSERT(ARRAY_SIZE(pd_src_pdo) == PDO_IDX_COUNT);
