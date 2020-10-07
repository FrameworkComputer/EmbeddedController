/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LCT (Long Countdown Timer) module for Chrome EC */
#include "lct_chip.h"
#include "console.h"
#include "hooks.h"
#include "registers.h"
#include "rtc.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define LCT_CLK_ENABLE_DELAY_USEC    150
#define LCT_WEEKS_MAX                 15

#define CPRINTF(format, args...) cprintf(CC_CLOCK, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)

void npcx_lct_sel_power_src(enum NPCX_LCT_PWR_SRC pwr_src)
{
	if (IS_BIT_SET(NPCX_LCTCONT, NPCX_LCTCONT_EN)) {
		CPRINTS("Don't set power source when LCT is enabled");
		return;
	}

	if (pwr_src == NPCX_LCT_PWR_SRC_VSBY)
		SET_BIT(NPCX_LCTCONT, NPCX_LCTCONT_VSBY_PWR);
	else
		CLEAR_BIT(NPCX_LCTCONT, NPCX_LCTCONT_VSBY_PWR);
}

void npcx_lct_enable_clk(uint8_t enable)
{
	if (IS_BIT_SET(NPCX_LCTCONT, NPCX_LCTCONT_EN)) {
		CPRINTS("Don't set/unset clock when LCT is enabled");
		return;
	}

	if (enable) {
		SET_BIT(NPCX_LCTCONT, NPCX_LCTCONT_CLK_EN);
		/*
		 * This bit must be set to 1 at least tLCTCKEN (150 us)
		 * before the LCT is enabled.
		 */
		udelay(LCT_CLK_ENABLE_DELAY_USEC);
	} else {
		CLEAR_BIT(NPCX_LCTCONT, NPCX_LCTCONT_CLK_EN);
	}
}

void npcx_lct_enable(uint8_t enable)
{
	enable = !!enable;
	SET_FIELD(NPCX_LCTCONT, NPCX_LCTCONT_EN_FIELD, enable);
	/* Wait until the bit value equals to what is set */
	while (IS_BIT_SET(NPCX_LCTCONT, NPCX_LCTCONT_EN) != enable)
		;
}

void npcx_lct_config(int seconds, int psl_ena, int int_ena)
{
	if (IS_BIT_SET(NPCX_LCTCONT, NPCX_LCTCONT_EN)) {
		CPRINTS("Don't config LCT when LCT is enabled");
		return;
	}

	/* LCT can count max to (16 weeks - 1 second) */
	if (seconds >= (LCT_WEEKS_MAX + 1) * SECS_PER_WEEK) {
		CPRINTS("LCT time is out of range");
		return;
	}

	/* Clear pending LCT event first */
	NPCX_LCTSTAT = BIT(NPCX_LCTSTAT_EVST);

	NPCX_LCTWEEK = seconds / SECS_PER_WEEK;
	seconds %= SECS_PER_WEEK;
	NPCX_LCTDAY = seconds / SECS_PER_DAY;
	seconds %= SECS_PER_DAY;
	NPCX_LCTHOUR = seconds / SECS_PER_HOUR;
	seconds %= SECS_PER_HOUR;
	NPCX_LCTMINUTE = seconds / SECS_PER_MINUTE;
	NPCX_LCTSECOND = seconds % SECS_PER_MINUTE;

	if (psl_ena) {
		if (IS_BIT_SET(NPCX_LCTCONT, NPCX_LCTCONT_VSBY_PWR))
			SET_BIT(NPCX_LCTCONT, NPCX_LCTCONT_PSL_EN);
		else
			CPRINTS("LCT must source VSBY to support PSL wakeup");
	}

	if (int_ena)
		SET_BIT(NPCX_LCTCONT, NPCX_LCTCONT_EVEN);

}

void npcx_lct_clear_event(void)
{
	NPCX_LCTSTAT = BIT(NPCX_LCTSTAT_EVST);
}

int npcx_lct_is_event_set(void)
{
	return IS_BIT_SET(NPCX_LCTSTAT, NPCX_LCTSTAT_EVST);
}

static void npcx_lct_init(void)
{
	/* Disable LCT */
	npcx_lct_enable(0);
	/* Clear control and status registers */
	NPCX_LCTCONT = 0x0;
	npcx_lct_clear_event();
	/* Clear all timer registers */
	NPCX_LCTSECOND = 0x0;
	NPCX_LCTMINUTE = 0x0;
	NPCX_LCTHOUR = 0x0;
	NPCX_LCTDAY = 0x0;
	NPCX_LCTWEEK = 0x0;
}
DECLARE_HOOK(HOOK_INIT, npcx_lct_init, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_CMD_RTC_ALARM
static int command_lctalarm(int argc, char **argv)
{
	char *e;
	int seconds;

	seconds = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	npcx_lct_enable(0);
	npcx_lct_sel_power_src(NPCX_LCT_PWR_SRC_VSBY);
	npcx_lct_enable_clk(1);
	/* Enable LCT event interrupt and MIWU */
	npcx_lct_config(seconds, 0, 1);
	task_disable_irq(NPCX_IRQ_LCT_WKINTF_2);
	/* Enable wake-up input sources & clear pending bit */
	NPCX_WKPCL(MIWU_TABLE_2, LCT_WUI_GROUP)  |= LCT_WUI_MASK;
	NPCX_WKINEN(MIWU_TABLE_2, LCT_WUI_GROUP) |= LCT_WUI_MASK;
	NPCX_WKEN(MIWU_TABLE_2, LCT_WUI_GROUP)   |= LCT_WUI_MASK;
	task_enable_irq(NPCX_IRQ_LCT_WKINTF_2);
	npcx_lct_enable(1);

	return 0;
}
DECLARE_CONSOLE_COMMAND(lctalarm, command_lctalarm, "", "");
#endif
