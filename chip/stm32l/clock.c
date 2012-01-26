/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include <stdint.h>

#include "clock.h"
#include "common.h"

/**
 * Idle task
 * executed when no task are ready to be scheduled
 */
void __idle(void)
{
	while (1) {
		/* wait for the irq event */
		asm("wfi");
		/* TODO more power management here */
	}
}

int clock_init(void)
{
	return EC_SUCCESS;
}
