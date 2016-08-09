/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "registers.h"

void jtag_pre_init(void)
{
	/*
	 * We don't need to do anything for JTAG, but if we're resuming from
	 * deep sleep we need to undo some stuff as soon as possible and this
	 * is the first init function that's called.
	 *
	 * It doesn't hurt anything if this setup is not needed, but we don't
	 * investigate the reset cause until much later (and doing so is
	 * destructive), so we'll just do the post-deep-sleep setup every time.
	 */

	/* Disable the deep sleep triggers */
	GR_PMU_LOW_POWER_DIS = 0;
	GR_PMU_EXITPD_MASK = 0;

	/* Unfreeze the USB module */
	GWRITE_FIELD(USB, PCGCCTL, STOPPCLK, 0);
	GWRITE_FIELD(USB, PCGCCTL, RSTPDWNMODULE, 0);
	GWRITE_FIELD(USB, PCGCCTL, PWRCLMP, 0);


}
