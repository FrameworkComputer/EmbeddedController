/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush family-specific configuration */

#include "gpio.h"
#include "power.h"

/* Wake Sources */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

/* Power Signal Input List */
const struct power_signal_info power_signal_list[] = {
	[X86_SLP_S0_N] = {
		.gpio = GPIO_PCH_SLP_S0_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S0_DEASSERTED",
	},
	[X86_SLP_S3_N] = {
		.gpio = GPIO_PCH_SLP_S3_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S3_DEASSERTED",
	},
	[X86_SLP_S5_N] = {
		.gpio = GPIO_PCH_SLP_S5_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S5_DEASSERTED",
	},
	[X86_S0_PGOOD] = {
		.gpio = GPIO_S0_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S0_PGOOD",
	},
	[X86_S5_PGOOD] = {
		.gpio = GPIO_S5_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S5_PGOOD",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

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
