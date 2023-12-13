/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "charger.h"

int extpower_is_present(void)
{
	/* TODO: b/305014156 - Brox: configure charger
	 * Update with PDC specific calls to get the AC state on all ports.
	 */

	return 1;
}
