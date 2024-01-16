/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "charger.h"
#include "charger/isl923x_public.h"

int extpower_is_present(void)
{
	int rv;
	bool acok;

	/* raa489000_is_acok() is part of the common isl923x driver. */
	rv = raa489000_is_acok(0, &acok);
	if ((rv == EC_SUCCESS) && acok) {
		return 1;
	}

	return 0;
}
