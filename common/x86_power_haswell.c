/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* X86 chipset power control module for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "x86_power.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ## args)


/*****************************************************************************/
/* Chipset interface */

void chipset_force_shutdown(void)
{
	CPRINTF("[%T %s()]\n", __func__);

	/*
	 * Force x86 off. This condition will reset once the state machine
	 * transitions to G3.
	 */
	gpio_set_level(GPIO_PCH_DPWROK, 0);
	gpio_set_level(GPIO_PCH_RSMRST_L, 0);
}

void chipset_reset(int cold_reset)
{
	CPRINTF("[%T %s(%d)]\n", __func__, cold_reset);
	if (cold_reset) {
		/*
		 * Drop and restore PWROK.  This causes the PCH to reboot,
		 * regardless of its after-G3 setting.  This type of reboot
		 * causes the PCH to assert PLTRST#, SLP_S3#, and SLP_S5#, so
		 * we actually drop power to the rest of the system (hence, a
		 * "cold" reboot).
		 */

		/* Ignore if PWROK is already low */
		if (gpio_get_level(GPIO_PCH_PWROK) == 0)
			return;

		/* PWROK must deassert for at least 3 RTC clocks = 91 us */
		gpio_set_level(GPIO_PCH_PWROK, 0);
		udelay(100);
		gpio_set_level(GPIO_PCH_PWROK, 1);

	} else {
		/*
		 * Send a RCIN# pulse to the PCH.  This just causes it to
		 * assert INIT# to the CPU without dropping power or asserting
		 * PLTRST# to reset the rest of the system.
		 */

		/* Pulse must be at least 16 PCI clocks long = 500 ns */
		gpio_set_level(GPIO_PCH_RCIN_L, 0);
		udelay(10);
		gpio_set_level(GPIO_PCH_RCIN_L, 1);
	}
}

int chipset_in_state(int state_mask)
{
	/* HEY - Hard code to off for now. Perhaps use a console command to
	 * manually specify certain states for the other tasks to look at. */
	return CHIPSET_STATE_HARD_OFF;
}

void chipset_exit_hard_off(void)
{
	CPRINTF("[%T %s()]\n", __func__);
}

void chipset_throttle_cpu(int throttle)
{
	CPRINTF("[%T %s(%d)]\n", __func__, throttle);
}

/*****************************************************************************/
/* Hooks */

static void x86_lid_change(void)
{
	CPRINTF("[%T %s()]\n", __func__);
}
DECLARE_HOOK(HOOK_LID_CHANGE, x86_lid_change, HOOK_PRIO_DEFAULT);

static void x86_power_ac_change(void)
{
	if (extpower_is_present()) {
		CPRINTF("[%T x86 AC on]\n");
	} else {
		CPRINTF("[%T x86 AC off]\n");
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, x86_power_ac_change, HOOK_PRIO_DEFAULT);

static void x86_power_init(void)
{
	CPRINTF("[%T %s()]\n", __func__);

	/* Enable interrupts for our GPIOs */
	gpio_enable_interrupt(GPIO_POWER_BUTTON_L);
	gpio_enable_interrupt(GPIO_LID_OPEN);
	gpio_enable_interrupt(GPIO_AC_PRESENT);
	gpio_enable_interrupt(GPIO_PCH_BKLTEN);
	gpio_enable_interrupt(GPIO_PCH_SLP_S0_L);
	gpio_enable_interrupt(GPIO_PCH_SLP_S3_L);
	gpio_enable_interrupt(GPIO_PCH_SLP_S5_L);
	gpio_enable_interrupt(GPIO_PCH_SLP_SUS_L);
	gpio_enable_interrupt(GPIO_PCH_SUSWARN_L);
	gpio_enable_interrupt(GPIO_PP1050_PGOOD);
	gpio_enable_interrupt(GPIO_PP1350_PGOOD);
	gpio_enable_interrupt(GPIO_PP5000_PGOOD);
	gpio_enable_interrupt(GPIO_VCORE_PGOOD);
	gpio_enable_interrupt(GPIO_CPU_PGOOD);
	gpio_enable_interrupt(GPIO_PCH_EDP_VDD_EN);
	gpio_enable_interrupt(GPIO_RECOVERY_L);
	gpio_enable_interrupt(GPIO_WRITE_PROTECT);
}
DECLARE_HOOK(HOOK_INIT, x86_power_init, HOOK_PRIO_INIT_CHIPSET);

/*****************************************************************************/
/* Interrupts */

void x86_power_interrupt(enum gpio_signal signal)
{
	CPRINTF("[%T %s(%d)]\n", __func__, signal);
}

/*****************************************************************************/
/* Task function */

void chipset_task(void)
{
	while (1) {
		CPRINTF("[%T %s()]\n", __func__);
		/* do NOTHING until we know what we should do. */
		task_wait_event(-1);
	}
}

/*****************************************************************************/
/* Console commands */

static int command_power(int argc, char **argv)
{
	ccprintf("No commands defined yet. Add some for bringup.\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(power, command_power,
			NULL,
			"Manually drive power states for bringup",
			NULL);

