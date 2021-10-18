/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"
#include "usb_pd.h"
#include "usb_pd_pdo.h"

#define PDO_FIXED_FLAGS (PDO_FIXED_DATA_SWAP | PDO_FIXED_UNCONSTRAINED |\
			 PDO_FIXED_COMM_CAP)

/* Source PDOs */
const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,  3000, PDO_FIXED_FLAGS),
		PDO_FIXED(12000, 3000, PDO_FIXED_FLAGS),
		PDO_FIXED(20000, 3000, PDO_FIXED_FLAGS),
};

/* Fake PDOs : we just want our pre-defined voltages */
const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000,   500, PDO_FIXED_FLAGS),
		PDO_FIXED(12000,  500, PDO_FIXED_FLAGS),
		PDO_FIXED(20000,  500, PDO_FIXED_FLAGS),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

static const int pd_src_pdo_cnts[] = {
		[SRC_CAP_5V] = 1,
		[SRC_CAP_12V] = 2,
		[SRC_CAP_20V] = 3,
};

static int pd_src_pdo_idx;

void board_set_source_cap(enum board_src_cap cap)
{
	pd_src_pdo_idx = cap;
}

int charge_manager_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	*src_pdo = pd_src_pdo;
	return pd_src_pdo_cnts[pd_src_pdo_idx];
}
