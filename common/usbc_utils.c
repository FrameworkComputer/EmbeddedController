/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB-C utility functions for all PD stacks (TCPMv1, TCPMv2, PDC)
 */

#include "common.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "util.h"

#include <stdint.h>

/* The macro is used to prevent a DBZ exception while decoding PDOs. */
#define PROCESS_ZERO_DIVISOR(x) ((x) == 0 ? 1 : (x))

__overridable int pd_is_valid_input_voltage(int mv)
{
	return 1;
}

int pd_select_best_pdo(uint32_t src_cap_cnt, const uint32_t *const src_caps,
		       int max_mv, uint32_t *selected_pdo)
{
	int i, uw, mv, unused_min_mv, ma;
	int ret = -1;
	int cur_uw = -1;
	int has_preferred_pdo;
	int prefer_cur;
	int __attribute__((unused)) cur_mv = 0;

	/* max voltage is always limited by this boards max request */
	max_mv = MIN(max_mv, CONFIG_USB_PD_MAX_VOLTAGE_MV);

	/* Get max power that is under our max voltage input */
	for (i = 0; i < src_cap_cnt; i++) {
		if (IS_ENABLED(CONFIG_USB_PD_ONLY_FIXED_PDOS) &&
		    (src_caps[i] & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
			continue;

		/* its an unsupported Augmented PDO (PD3.0) */
		if ((src_caps[i] & PDO_TYPE_MASK) == PDO_TYPE_AUGMENTED)
			continue;

		pd_extract_pdo_power(src_caps[i], &ma, &mv, &unused_min_mv);

		/* Skip invalid voltage */
		if (!mv)
			continue;

		/*
		 * It's illegal to have EPR PDO in 1...7.
		 * TODO: This is supposed to be a hard reset (8.3.3.3.8)
		 */
		if (i < 7) {
			if (((src_caps[i] & PDO_TYPE_MASK) == PDO_TYPE_FIXED) &&
			    mv > PD_MAX_SPR_VOLTAGE) {
				continue;
			}
			if (((src_caps[i] & PDO_TYPE_MASK) ==
			     PDO_TYPE_VARIABLE) &&
			    mv > PD_MAX_VARIABLE_VOLTAGE) {
				continue;
			}
		}

		/* Skip any voltage not supported by this board */
		if (!pd_is_valid_input_voltage(mv))
			continue;

		if (mv > max_mv)
			continue;

		uw = ma * mv;
		uw = MIN(uw, CONFIG_USB_PD_MAX_POWER_MW * 1000);
		prefer_cur = 0;

		/* Apply special rules in favor of voltage  */
		if (IS_ENABLED(CONFIG_USB_PD_PREFER_LOW_VOLTAGE)) {
			if (uw == cur_uw && mv < cur_mv)
				prefer_cur = 1;
		} else if (IS_ENABLED(CONFIG_USB_PD_PREFER_HIGH_VOLTAGE)) {
			if (uw == cur_uw && mv > cur_mv)
				prefer_cur = 1;
		}

		/* Prefer higher power, except for tiebreaker */
		has_preferred_pdo = prefer_cur || (uw > cur_uw);

		if (has_preferred_pdo) {
			ret = i;
			cur_uw = uw;
			cur_mv = mv;
		}
	}

	if (ret != -1 && selected_pdo)
		*selected_pdo = src_caps[ret];

	return ret;
}

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

void pd_extract_pdo_power(uint32_t pdo, uint32_t *ma, uint32_t *max_mv,
			  uint32_t *min_mv)
{
	extract_pdo_helper(pdo, ma, max_mv, min_mv);

	if (*max_mv) {
		/* Clamp current to board limits for non-zero-volt PDOs */
		uint32_t board_limit_ma =
			MIN(CONFIG_USB_PD_MAX_CURRENT_MA,
			    CONFIG_USB_PD_MAX_POWER_MW * 1000 /
				    PROCESS_ZERO_DIVISOR(*min_mv));

		*ma = MIN(*ma, board_limit_ma);
	}
}
