/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"
#include "usb_pd.h"
#include "usb_pd_pdo.h"

#define PDO_FIXED_FLAGS (PDO_FIXED_UNCONSTRAINED | \
			 PDO_FIXED_DATA_SWAP | \
			 PDO_FIXED_COMM_CAP)

const uint32_t pd_src_pdo[] = {
	PDO_FIXED(5000, 3000, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);
