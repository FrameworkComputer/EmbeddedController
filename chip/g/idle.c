/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "init_chip.h"
#include "rdd.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

/* What to do when we're just waiting */
static enum {
	DONT_KNOW,
	IDLE_WFI,
	IDLE_SLEEP,
	IDLE_DEEP_SLEEP,
	NUM_CHOICES
} idle_action;

#define EVENT_MIN 500

static int idle_default;

static const char const *idle_name[] = {
	"invalid",
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
		for (i = 1; i < ARRAY_SIZE(idle_name); i++)
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
			"Set or show the idle action: wfi, sleep, deep sleep");

static int utmi_wakeup_is_enabled(void)
{
#ifdef CONFIG_RDD
	return is_utmi_wakeup_allowed();
#endif
	return 1;
}

static void prepare_to_sleep(void)
{
	/* No task switching! */
	interrupt_disable();

	/* Enable all possible internal wake sources */
	GR_PMU_EXITPD_MASK =
		GC_PMU_EXITPD_MASK_PIN_PD_EXIT_MASK |
		GC_PMU_EXITPD_MASK_RDD0_PD_EXIT_TIMER_MASK |
		GC_PMU_EXITPD_MASK_RBOX_WAKEUP_MASK |
		GC_PMU_EXITPD_MASK_TIMELS0_PD_EXIT_TIMER0_MASK |
		GC_PMU_EXITPD_MASK_TIMELS0_PD_EXIT_TIMER1_MASK;

	/* Clear the RBOX wakeup signal and status bits */
	GREG32(RBOX, WAKEUP) = GC_RBOX_WAKEUP_CLEAR_MASK;
	/* Wake on RBOX interrupts */
	GREG32(RBOX, WAKEUP) = GC_RBOX_WAKEUP_ENABLE_MASK;

	if (utmi_wakeup_is_enabled())
		GR_PMU_EXITPD_MASK |=
			GC_PMU_EXITPD_MASK_UTMI_SUSPEND_N_MASK;

	/* Which rails should we turn off? */
	GR_PMU_LOW_POWER_DIS =
		GC_PMU_LOW_POWER_DIS_VDDIOF_MASK |
		GC_PMU_LOW_POWER_DIS_VDDXO_MASK |
		GC_PMU_LOW_POWER_DIS_JTR_RC_MASK;

	/*
	 * Deep sleep should only be enabled when the AP is off otherwise the
	 * TPM state will lost.
	 *
	 * TODO(crosbug.com/p/55747): Enable deep sleep when the AP is shut
	 * down. Currently deep sleep is only enabled through the console.
	 */
	if (idle_action == IDLE_DEEP_SLEEP) {
		/* Clear upcoming events. They don't matter in deep sleep */
		__hw_clock_event_clear();

		/* Configure pins for deep sleep */
		board_configure_deep_sleep_wakepins();

		/* Make sure the usb clock is enabled */
		clock_enable_module(MODULE_USB, 1);
		/*
		 * Preserve some state prior to deep sleep. Pretty much all we
		 * need is the device address, since everything else can be
		 * reinitialized on resume.
		 */
		GREG32(PMU, PWRDN_SCRATCH18) = GR_USB_DCFG;

		/* Latch the pinmux values */
		GREG32(PINMUX, HOLD) = 1;

		/* Clamp the USB pins and shut the PHY down. We have to do this
		 * in three separate steps, or Bad Things happen. */
		GWRITE_FIELD(USB, PCGCCTL, PWRCLMP, 1);
		GWRITE_FIELD(USB, PCGCCTL, RSTPDWNMODULE, 1);
		GWRITE_FIELD(USB, PCGCCTL, STOPPCLK, 1);

		/* Shut down one more power rail for deep sleep */
		GR_PMU_LOW_POWER_DIS |=
			GC_PMU_LOW_POWER_DIS_VDDL_MASK;
	}

	/* The next "wfi" will trigger it */
	GR_PMU_LOW_POWER_DIS |= GC_PMU_LOW_POWER_DIS_START_MASK;
}

/* This is for normal sleep only. Deep sleep resumes with a warm boot. */
static void resume_from_sleep(void)
{
	/* Prevent accidental reentry */
	GR_PMU_LOW_POWER_DIS = 0;

	/* Allow task switching again */
	interrupt_enable();
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

/* Wait a good long time after any console input, in case there's more. */
void clock_refresh_console_in_use(void)
{
	delay_sleep_by(10 * SECOND);
}

void disable_deep_sleep(void)
{
	idle_action = idle_default;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, disable_deep_sleep, HOOK_PRIO_DEFAULT);

void enable_deep_sleep(void)
{
	idle_action = IDLE_DEEP_SLEEP;
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, enable_deep_sleep, HOOK_PRIO_DEFAULT);

static void idle_init(void)
{
	/*
	 * If bus obfuscation is enabled disable sleep.
	 */
	if ((GR_FUSE(OBFUSCATION_EN) == 5) ||
	    (GR_FUSE(FW_DEFINED_BROM_APPLYSEC) & (1 << 3)) ||
	    (runlevel_is_high() && GREAD(GLOBALSEC, OBFS_SW_EN))) {
		CPRINTS("bus obfuscation enabled disabling sleep");
		idle_default = IDLE_WFI;
	} else {
		idle_default = IDLE_SLEEP;
	}
}
DECLARE_HOOK(HOOK_INIT, idle_init, HOOK_PRIO_DEFAULT - 1);

/* Custom idle task, executed when no tasks are ready to be scheduled. */
void __idle(void)
{
	int sleep_ok, sleep_delay_passed, next_evt_us;

	/*
	 * On init or resume from deep sleep set the idle action to default. If
	 * it should be something else it will be determined during runtime.
	 *
	 * Before changing idle_action check that it is not already set. It is
	 * possible that HOOK_CHIPSET_RESUME or SHUTDOWN were triggered before
	 * this and set the idle_action.
	 */
	if (!idle_action)
		idle_action = idle_default;

	/* Disable sleep until 3 minutes after init */
	delay_sleep_by(3 * MINUTE);

	while (1) {

		/* Anyone still busy? */
		sleep_ok = DEEP_SLEEP_ALLOWED;

		/* Wait a bit, just in case */
		sleep_delay_passed = timestamp_expired(next_sleep_time, 0);

		/* Don't enable sleep if there is about to be an event */
		next_evt_us = __hw_clock_event_get() - __hw_clock_source_read();

		/* If it hasn't yet been long enough, check again when it is */
		if (!sleep_delay_passed)
			timer_arm(next_sleep_time, TASK_ID_IDLE);

		/* We're allowed to sleep now, so set it up. */
		if (sleep_ok && sleep_delay_passed && next_evt_us > EVENT_MIN)
			if (idle_action != IDLE_WFI)
				prepare_to_sleep();

		/* Wait for the next irq event. This stops the CPU clock and
		 * may trigger sleep or deep sleep if enabled. */
		asm("wfi");

		/*
		 * Note: After resuming from normal sleep we should clear
		 * PMU_LOW_POWER_DIS to prevent sleeping again by accident.
		 * Normal sleep eventually resumes here after the waking
		 * interrupt has been handled, but since all the other tasks
		 * will get a chance to run first it might be some time before
		 * that happens. If we find ourselves going back into sleep
		 * unexpectedly, that might be why.
		 */
		resume_from_sleep();
	}
}
