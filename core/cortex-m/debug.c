/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "debug.h"
#include "debug_regs.h"
#include "stdbool.h"

__override bool debugger_is_connected(void)
{
	return CPU_DHCSR & DHCSR_C_DEBUGEN;
}
