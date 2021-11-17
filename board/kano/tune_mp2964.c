/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Tune the MP2964 IMVP9.1 parameters for kano */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "hooks.h"
#include "mp2964.h"

const static struct mp2964_reg_val rail_a[] = {
	{ MP2964_MFR_ALT_SET,     0xe081 },	/* ALERT_DELAY = 200ns */
};
const static struct mp2964_reg_val rail_b[] = {
	{ MP2964_MFR_ALT_SET,     0xe081 },	/* ALERT_DELAY = 200ns */
};

static void mp2964_on_startup(void)
{
	static int chip_updated;
	int status;

	if (get_board_id() != 1)
		return;

	if (chip_updated)
		return;

	chip_updated = 1;

	ccprintf("%s: attempting to tune PMIC\n", __func__);

	status = mp2964_tune(rail_a, ARRAY_SIZE(rail_a),
			     rail_b, ARRAY_SIZE(rail_b));
	if (status != EC_SUCCESS)
		ccprintf("%s: could not update all settings\n", __func__);
}

DECLARE_HOOK(HOOK_CHIPSET_STARTUP, mp2964_on_startup,
	     HOOK_PRIO_FIRST);
