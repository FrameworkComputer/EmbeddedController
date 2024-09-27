/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB-C utility functions for all PD stacks (TCPMv1, TCPMv2, PDC)
 */

#include "usb_common.h"
#include "usb_pd.h"
#include "util.h"

#include <stdint.h>

/* The macro is used to prevent a DBZ exception while decoding PDOs. */
#define PROCESS_ZERO_DIVISOR(x) ((x) == 0 ? 1 : (x))

static void extract_pdo_helper(uint32_t pdo, uint32_t *ma, uint32_t *max_mv,
			       uint32_t *min_mv)
{
	int mw;

	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_FIXED) {
		*max_mv = PDO_FIXED_VOLTAGE(pdo);
		*min_mv = *max_mv;
	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_AUGMENTED) {
		*max_mv = PDO_AUG_MAX_VOLTAGE(pdo);
		*min_mv = PDO_AUG_MIN_VOLTAGE(pdo);
	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_VARIABLE) {
		*max_mv = PDO_VAR_MAX_VOLTAGE(pdo);
		*min_mv = PDO_VAR_MIN_VOLTAGE(pdo);
	} else {
		*max_mv = PDO_BATT_MAX_VOLTAGE(pdo);
		*min_mv = PDO_BATT_MIN_VOLTAGE(pdo);
	}

	if (*max_mv == 0) {
		*ma = 0;
		*min_mv = 0;
		return;
	}

	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_FIXED) {
		*ma = PDO_FIXED_CURRENT(pdo);
	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_AUGMENTED) {
		*ma = PDO_AUG_MAX_CURRENT(pdo);
	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_VARIABLE) {
		*ma = PDO_VAR_MAX_CURRENT(pdo);
	} else {
		mw = PDO_BATT_MAX_POWER(pdo);
		*ma = 1000 * mw / PROCESS_ZERO_DIVISOR(*min_mv);
	}
}

void pd_extract_pdo_power_unclamped(uint32_t pdo, uint32_t *ma,
				    uint32_t *max_mv, uint32_t *min_mv)
{
	extract_pdo_helper(pdo, ma, max_mv, min_mv);
}

#if defined(PD_MAX_POWER_MW) && defined(PD_MAX_CURRENT_MA)

void pd_extract_pdo_power(uint32_t pdo, uint32_t *ma, uint32_t *max_mv,
			  uint32_t *min_mv)
{
	extract_pdo_helper(pdo, ma, max_mv, min_mv);

	if (*max_mv) {
		/* Clamp current to board limits for non-zero-volt PDOs */
		uint32_t board_limit_ma = MIN(
			PD_MAX_CURRENT_MA,
			PD_MAX_POWER_MW * 1000 / PROCESS_ZERO_DIVISOR(*min_mv));

		*ma = MIN(*ma, board_limit_ma);
	}
}

#endif
