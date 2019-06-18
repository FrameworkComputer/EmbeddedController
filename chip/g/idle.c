/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "case_closed_debug.h"
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
#include "usb_api.h"
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

static const char *const idle_name[] = {
	"invalid",
	"wfi",
	"sleep",
	"deep sleep",
};
BUILD_ASSERT(ARRAY_SIZE(idle_name) == NUM_CHOICES);

static int command_idle(int argc, char **argv)
{
	int i;

	if (argc > 1) {
		if (!strncasecmp("c", argv[1], 1)) {
			GREG32(PMU, PWRDN_SCRATCH17) = 0;
		} else if (console_is_restricted()) {
			ccprintf("Console is locked, cannot set idle state\n");
			return EC_ERROR_INVAL;
		} else {
			for (i = 1; i < ARRAY_SIZE(idle_name); i++)
				if (!strncasecmp(idle_name[i], argv[1], 1)) {
					idle_action = i;
					break;
				}
		}
	}

	ccprintf("idle action: %s\n", idle_name[idle_action]);
	ccprintf("deep sleep count: %u\n", GREG32(PMU, PWRDN_SCRATCH17));

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(idle, command_idle,
			     "[w|s|d|c]",
			     "Set idle action: wfi, sleep, deep sleep or "
			     "Clear the deep sleep count");

static int utmi_wakeup_is_enabled(void)
{
#ifdef CONFIG_RDD
	/*
	 * USB is only used for CCD, so only enable UTMI wakeups when RDD
	 * detects that a debug accessory is attached.
	 */
	return ccd_ext_is_enabled();
#else
	/* USB is used for the host interface, so always enable UTMI wakeups */
	return 1;
#endif
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

#ifdef CONFIG_RBOX_WAKEUP
	/*
	 * Enable RBOX wakeup. It will immediately be disabled on resume in
	 * rbox_init or pmu_wakeup_interrupt.
	 */
	GREG32(RBOX, WAKEUP) = GC_RBOX_WAKEUP_ENABLE_MASK;
#endif

	if (utmi_wakeup_is_enabled() && idle_action != IDLE_DEEP_SLEEP)
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
	 */
	if (idle_action == IDLE_DEEP_SLEEP) {
		/* Clear upcoming events. They don't matter in deep sleep */
		__hw_clock_event_clear();

		/* Configure pins for deep sleep */
		board_configure_deep_sleep_wakepins();

		/* Make sure the usb clock is enabled */
		clock_enable_module(MODULE_USB, 1);
		/* Preserve some state from USB hardware prior to deep sleep. */
		if (!GREAD_FIELD(USB, PCGCCTL, RSTPDWNMODULE))
			usb_save_suspended_state();

		/* Increment the deep sleep count */
		GREG32(PMU, PWRDN_SCRATCH17) =
			GREG32(PMU, PWRDN_SCRATCH17) + 1;

#ifndef CONFIG_NO_PINHOLD
		/* Latch the pinmux values */
		GREG32(PINMUX, HOLD) = 1;
#endif

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

void enable_deep_sleep(void)
{
	idle_action = IDLE_DEEP_SLEEP;
}

static void idle_init(void)
{
	/*
	 * If bus obfuscation is enabled disable sleep.
	 */
	if ((GR_FUSE(OBFUSCATION_EN) == 5) ||
	    (GR_FUSE(FW_DEFINED_BROM_APPLYSEC) & BIT(3)) ||
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

	/* Disable sleep for 20 seconds after init */
	delay_sleep_by(20 * SECOND);

	while (1) {

		/* Anyone still busy?  (this checks sleep_mask) */
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
