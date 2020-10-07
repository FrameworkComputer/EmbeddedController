/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "ec_commands.h"
#include "gpio_chip.h"
#include "hooks.h"
#include "host_command.h"
#include "lpc_chip.h"
#include "registers.h"
#include "task.h"

/*
 * List of GPIO IRQs to enable. Don't automatically enable interrupts for
 * the keyboard input GPIO bank - that's handled separately. Of course the
 * bank is different for different systems.
 */
static void gpio_init(void)
{
	/* Enable IRQs now that pins are set up */
	task_enable_irq(NPCX_IRQ_MTC_WKINTAD_0);
	task_enable_irq(NPCX_IRQ_WKINTEFGH_0);
	task_enable_irq(NPCX_IRQ_WKINTC_0);
	task_enable_irq(NPCX_IRQ_TWD_WKINTB_0);
	task_enable_irq(NPCX_IRQ_WKINTA_1);
	task_enable_irq(NPCX_IRQ_WKINTB_1);
#ifndef HAS_TASK_KEYSCAN
	task_enable_irq(NPCX_IRQ_KSI_WKINTC_1);
#endif
	task_enable_irq(NPCX_IRQ_WKINTD_1);
	task_enable_irq(NPCX_IRQ_WKINTE_1);
	task_enable_irq(NPCX_IRQ_WKINTF_1);
	task_enable_irq(NPCX_IRQ_WKINTG_1);
	task_enable_irq(NPCX_IRQ_WKINTH_1);
#if defined(CHIP_FAMILY_NPCX7)
	task_enable_irq(NPCX_IRQ_WKINTFG_2);
#endif
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);

/**
 * Handlers for each GPIO port.  These read and clear the interrupt bits for
 * the port, then call the master handler above.
 */

#define GPIO_IRQ_FUNC(_irq_func, wui_int)		\
void _irq_func(void)					\
{							\
	gpio_interrupt(wui_int);			\
}

/* If we need to handle the other type interrupts except GPIO, add code here */
void __gpio_wk0efgh_interrupt(void)
{
	if (IS_ENABLED(CONFIG_HOSTCMD_X86)) {
		/* Pending bit 7 or 6 or 5? */
		if (IS_BIT_SET(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_5), 6) &&
			IS_BIT_SET(NPCX_WKPND(MIWU_TABLE_0, MIWU_GROUP_5), 6)) {
			/* Disable host wake-up */
			CLEAR_BIT(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_5), 6);
			/* Clear pending bit of WUI */
			SET_BIT(NPCX_WKPCL(MIWU_TABLE_0, MIWU_GROUP_5), 6);
			return;
		}
		if (IS_ENABLED(CONFIG_HOSTCMD_ESPI)) {
			if (IS_BIT_SET(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_5), 5)
				&&
			IS_BIT_SET(NPCX_WKPND(MIWU_TABLE_0, MIWU_GROUP_5), 5)) {
				espi_espirst_handler();
				return;
			}
		} else {
			if (IS_BIT_SET(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_5), 7)
				&&
			IS_BIT_SET(NPCX_WKPND(MIWU_TABLE_0, MIWU_GROUP_5), 7)) {
				lpc_lreset_pltrst_handler();
				return;
			}
		}
	}
	gpio_interrupt(WUI_INT(MIWU_TABLE_0, MIWU_GROUP_5));
	gpio_interrupt(WUI_INT(MIWU_TABLE_0, MIWU_GROUP_6));
	gpio_interrupt(WUI_INT(MIWU_TABLE_0, MIWU_GROUP_7));
	gpio_interrupt(WUI_INT(MIWU_TABLE_0, MIWU_GROUP_8));
}

#ifdef CONFIG_HOSTCMD_RTC
static void set_rtc_host_event(void)
{
	host_set_single_event(EC_HOST_EVENT_RTC);
}
DECLARE_DEFERRED(set_rtc_host_event);
#endif

void __gpio_rtc_interrupt(void)
{
	/* Check pending bit 7 */
#ifdef CONFIG_HOSTCMD_RTC
	if (NPCX_WKPND(MIWU_TABLE_0, MIWU_GROUP_4) & 0x80) {
		/* Clear pending bit for WUI */
		SET_BIT(NPCX_WKPCL(MIWU_TABLE_0, MIWU_GROUP_4), 7);
		hook_call_deferred(&set_rtc_host_event_data, 0);
		return;
	}
#endif
#if defined(CHIP_FAMILY_NPCX7) && defined(CONFIG_LOW_POWER_IDLE) && \
	(CONFIG_CONSOLE_UART == 1)
	/* Handle the interrupt from UART wakeup event */
	if (IS_BIT_SET(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_1), 6) &&
	    IS_BIT_SET(NPCX_WKPND(MIWU_TABLE_0, MIWU_GROUP_1), 6)) {
		/*
		 * Disable WKEN bit to avoid the other unnecessary interrupts
		 * from the coming data bits after the start bit. (Pending bit
		 * of CR_SIN is set when a high-to-low transaction occurs.)
		 */
		CLEAR_BIT(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_1), 6);
		/* Clear pending bit for WUI */
		SET_BIT(NPCX_WKPCL(MIWU_TABLE_0, MIWU_GROUP_1), 6);
		/* Notify the clock module that the console is in use. */
		clock_refresh_console_in_use();
		return;
	}
#endif
	gpio_interrupt(WUI_INT(MIWU_TABLE_0, MIWU_GROUP_1));
	gpio_interrupt(WUI_INT(MIWU_TABLE_0, MIWU_GROUP_4));
}

void __gpio_wk1h_interrupt(void)
{
#if defined(CHIP_FAMILY_NPCX7) && defined(CONFIG_LOW_POWER_IDLE) && \
	(CONFIG_CONSOLE_UART == 0)
	/* Handle the interrupt from UART wakeup event */
	if (IS_BIT_SET(NPCX_WKEN(MIWU_TABLE_1, MIWU_GROUP_8), 7) &&
	    IS_BIT_SET(NPCX_WKPND(MIWU_TABLE_1, MIWU_GROUP_8), 7)) {
		/*
		 * Disable WKEN bit to avoid the other unnecessary interrupts
		 * from the coming data bits after the start bit. (Pending bit
		 * of CR_SIN is set when a high-to-low transaction occurs.)
		 */
		CLEAR_BIT(NPCX_WKEN(MIWU_TABLE_1, MIWU_GROUP_8), 7);
		/* Clear pending bit for WUI */
		SET_BIT(NPCX_WKPCL(MIWU_TABLE_1, MIWU_GROUP_8), 7);
		/* Notify the clock module that the console is in use. */
		clock_refresh_console_in_use();
	} else
#endif
		gpio_interrupt(WUI_INT(MIWU_TABLE_1, MIWU_GROUP_8));
}

GPIO_IRQ_FUNC(__gpio_wk0b_interrupt, WUI_INT(MIWU_TABLE_0, MIWU_GROUP_2));
GPIO_IRQ_FUNC(__gpio_wk0c_interrupt, WUI_INT(MIWU_TABLE_0, MIWU_GROUP_3));
GPIO_IRQ_FUNC(__gpio_wk1a_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_1));
GPIO_IRQ_FUNC(__gpio_wk1b_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_2));
#ifndef HAS_TASK_KEYSCAN
/* Declare GPIO irq functions for KSI pins if there's no keyboard scan task, */
GPIO_IRQ_FUNC(__gpio_wk1c_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_3));
#endif
GPIO_IRQ_FUNC(__gpio_wk1d_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_4));
GPIO_IRQ_FUNC(__gpio_wk1e_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_5));
GPIO_IRQ_FUNC(__gpio_wk1f_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_6));
GPIO_IRQ_FUNC(__gpio_wk1g_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_7));
#if defined(CHIP_FAMILY_NPCX7)
GPIO_IRQ_FUNC(__gpio_wk2fg_interrupt, WUI_INT(MIWU_TABLE_2, MIWU_GROUP_6));
#endif

DECLARE_IRQ(NPCX_IRQ_MTC_WKINTAD_0, __gpio_rtc_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_TWD_WKINTB_0,  __gpio_wk0b_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_WKINTC_0,      __gpio_wk0c_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_WKINTEFGH_0,   __gpio_wk0efgh_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_WKINTA_1,      __gpio_wk1a_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_WKINTB_1,      __gpio_wk1b_interrupt, 3);
#ifndef HAS_TASK_KEYSCAN
DECLARE_IRQ(NPCX_IRQ_KSI_WKINTC_1,  __gpio_wk1c_interrupt, 3);
#endif
DECLARE_IRQ(NPCX_IRQ_WKINTD_1,      __gpio_wk1d_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_WKINTE_1,      __gpio_wk1e_interrupt, 3);
#ifdef CONFIG_HOSTCMD_SPS
/*
 * HACK: Make CS GPIO P2 to improve SHI reliability.
 * TODO: Increase CS-assertion-to-transaction-start delay on host to
 * accommodate P3 CS interrupt.
 */
DECLARE_IRQ(NPCX_IRQ_WKINTF_1,      __gpio_wk1f_interrupt, 2);
#else
DECLARE_IRQ(NPCX_IRQ_WKINTF_1,      __gpio_wk1f_interrupt, 3);
#endif
DECLARE_IRQ(NPCX_IRQ_WKINTG_1,      __gpio_wk1g_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_WKINTH_1,      __gpio_wk1h_interrupt, 3);
#if defined(CHIP_FAMILY_NPCX7)
DECLARE_IRQ(NPCX_IRQ_WKINTFG_2,     __gpio_wk2fg_interrupt, 3);
#endif

#undef GPIO_IRQ_FUNC
