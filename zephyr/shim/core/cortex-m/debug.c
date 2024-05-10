/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

#include <cmsis_core.h>

__override bool debugger_is_connected(void)
{
	return CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk;
}
