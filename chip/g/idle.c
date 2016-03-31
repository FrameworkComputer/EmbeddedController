/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "pmu.h"
#include "system.h"
#include "task.h"
#include "util.h"

/* This function is assumed to exist, but we don't use it */
void clock_refresh_console_in_use(void)
{
}

/* What to do when we're just waiting */
static enum {
	IDLE_WFI,				/* default */
	IDLE_SLEEP,
	IDLE_DEEP_SLEEP,
	NUM_CHOICES
} idle_action;

static const char const *idle_name[] = {
	"wfi",
	"sleep",
	"deep sleep",
};
BUILD_ASSERT(ARRAY_SIZE(idle_name) == NUM_CHOICES);

static int command_idle(int argc, char **argv)
{
	int c, i;

	if (argc > 1) {
		c = tolower(argv[1][0]);
		for (i = 0; i < ARRAY_SIZE(idle_name); i++)
			if (idle_name[i][0] == c) {
				idle_action = i;
				break;
			}
	}

	ccprintf("idle action: %s\n", idle_name[idle_action]);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(idle, command_idle,
			"[w|s|d]",
			"Set or show the idle action: wfi, sleep, deep sleep",
			NULL);

static void prepare_to_deep_sleep(void)
{
	/* No task switching! */
	interrupt_disable();

	/*
	 * Preserve some state prior to deep sleep. Pretty much all we need is
	 * the device address, since everything else can be reinitialized on
	 * resume.
	 */
	GREG32(PMU, PWRDN_SCRATCH18) = GR_USB_DCFG;
	/* And the idle action */
	GREG32(PMU, PWRDN_SCRATCH17) = idle_action;

	/* Latch the pinmux values */
	GREG32(PINMUX, HOLD) = 1;

	/*
	 * Specify the PINMUX pads that can wake us.
	 * A1 is UART RX. Idle is high, so wake on low level
	 * A12 is SPS_CS_L. Also wake on low.
	 * HEY: Use something in gpio.inc to identify these!
	 */
	GREG32(PINMUX, EXITEN0) =
		GC_PINMUX_EXITEN0_DIOA1_MASK |
		GC_PINMUX_EXITEN0_DIOA12_MASK;

	GREG32(PINMUX, EXITEDGE0) = 0;		/* level sensitive */

	GREG32(PINMUX, EXITINV0) =		/* low or falling */
		GC_PINMUX_EXITINV0_DIOA1_MASK |
		GC_PINMUX_EXITINV0_DIOA12_MASK;

	/* Enable all possible internal wake sources */
	GR_PMU_EXITPD_MASK =
		GC_PMU_EXITPD_MASK_PIN_PD_EXIT_MASK |
		GC_PMU_EXITPD_MASK_UTMI_SUSPEND_N_MASK |
		GC_PMU_EXITPD_MASK_RDD0_PD_EXIT_TIMER_MASK |
		GC_PMU_EXITPD_MASK_TIMELS0_PD_EXIT_TIMER0_MASK |
		GC_PMU_EXITPD_MASK_TIMELS0_PD_EXIT_TIMER1_MASK;

	/* Clamp the USB pins and shut the PHY down. We have to do this in
	 * three separate steps, or Bad Things happen. */
	GWRITE_FIELD(USB, PCGCCTL, PWRCLMP, 1);
	GWRITE_FIELD(USB, PCGCCTL, RSTPDWNMODULE, 1);
	GWRITE_FIELD(USB, PCGCCTL, STOPPCLK, 1);

	/* Get ready... */
	GR_PMU_LOW_POWER_DIS =
		/* The next "wfi" will trigger it */
		GC_PMU_LOW_POWER_DIS_START_MASK |
		/* ... with these rails off */
		GC_PMU_LOW_POWER_DIS_VDDL_MASK | /* <= this means deep sleep */
		GC_PMU_LOW_POWER_DIS_VDDIOF_MASK |
		GC_PMU_LOW_POWER_DIS_VDDXO_MASK |
		GC_PMU_LOW_POWER_DIS_JTR_RC_MASK;
}

/* The time in the future at which sleeping will be allowed. */
static timestamp_t next_sleep_time;

/* Update the future sleep time. */
void delay_sleep_by(uint32_t us)
{
	timestamp_t tmp = get_time();

	tmp.val += us;
	if (tmp.val > next_sleep_time.val)
		next_sleep_time = tmp;
}

/* Custom idle task, executed when no tasks are ready to be scheduled. */
void __idle(void)
{
	int sleep_ok, sleep_delay_passed, prev_ok = 0;

	/* Preserved across soft reboots, but not hard */
	idle_action = GREG32(PMU, PWRDN_SCRATCH17);
	if (idle_action >= NUM_CHOICES)
		idle_action = IDLE_WFI;

	while (1) {

		/* Anyone still busy? */
		sleep_ok = DEEP_SLEEP_ALLOWED;

		/*
		 * We'll always wait a little bit before sleeping no matter
		 * what. This is more likely to let any console output finish
		 * than calling clock_refresh_console_in_use(), because that
		 * function is called BEFORE waking the console task, not after
		 * it runs. We can't call cflush() here because that wakes a
		 * task to do it and so we're not idle any more.
		 */
		if (sleep_ok && !prev_ok)
			delay_sleep_by(200 * MSEC);

		prev_ok = sleep_ok;
		sleep_delay_passed = timestamp_expired(next_sleep_time, 0);

		/* If it hasn't yet been long enough, check again when it is */
		if (!sleep_delay_passed)
			timer_arm(next_sleep_time, TASK_ID_IDLE);

		/* We're allowed to deep sleep, so set it up. */
		if (sleep_ok && sleep_delay_passed)
			if (idle_action == IDLE_DEEP_SLEEP)
				prepare_to_deep_sleep();
		/* Normal sleep is not yet implemented */

		/* Wait for the next irq event. This stops the CPU clock and
		 * may trigger sleep or deep sleep if enabled. */
		asm("wfi");

		/*
		 * TODO: Normal sleep resumes by handling the interrupt, but we
		 * need to clear PMU_LOW_POWER_DIS right away or we might sleep
		 * again by accident. We can't do that here because we don't
		 * get here until the next idle, so we'll have to do it in the
		 * interrupt handler or when task switching. Deep sleep resumes
		 * with a warm boot, which handles it differently.
		 */
	}
}
