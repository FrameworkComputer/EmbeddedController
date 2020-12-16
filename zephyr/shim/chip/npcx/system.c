/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"

void system_reset(int flags)
{
	/*
	 * TODO(b/176523207): Reset the system. NPCX uses Watchdog & BBRAM for
	 * system reset & reset flag saving. The function could be implemented
	 * after Watchdog & BBRAM land the zephyr repository.
	 */

	while (1)
		;
}
