/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush family-specific configuration */

#include "gpio.h"

void sbu_fault_interrupt(enum ioex_signal signal)
{
	/* TODO */
}

void tcpc_alert_event(enum gpio_signal signal)
{
	/* TODO */
}

void ppc_interrupt(enum gpio_signal signal)
{
	/* TODO */
}

void bc12_interrupt(enum gpio_signal signal)
{
	/* TODO */
}
