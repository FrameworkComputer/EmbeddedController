/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "debug.h"
#include "stdbool.h"

bool debugger_is_connected(void)
{
	return CPU_DHCSR & DHCSR_C_DEBUGEN;
}
