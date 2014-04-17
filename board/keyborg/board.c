/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Keyborg board-specific configuration */

#include "board.h"
#include "common.h"
#include "debug.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"

int main(void)
{
	int i = 0;
	hardware_init();
	debug_printf("Keyborg starting...\n");

	while (1) {
		i++;
		task_wait_event(SECOND);
		debug_printf("Timer check - %d seconds\n", i);
	}
}
