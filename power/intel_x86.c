/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel X86 chipset power control module for Chrome EC */

#include "board_config.h"
#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "lpc.h"
#include "power.h"
#include "power/intel_x86.h"
#include "power_button.h"
#include "system.h"
#include "system_boot_time.h"
#include "task.h"
#include "util.h"
#include "vboot.h"
#include "wireless.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ##args)

enum sys_sleep_state {
	SYS_SLEEP_S3,
	SYS_SLEEP_S4,
	SYS_SLEEP_S5,
#ifdef CONFIG_POWER_S0IX
	SYS_SLEEP_S0IX,
#endif
};

static const int sleep_sig[] = {
	[SYS_SLEEP_S3] = SLP_S3_SIGNAL_L,
	[SYS_SLEEP_S4] = SLP_S4_SIGNAL_L,
	[SYS_SLEEP_S5] = SLP_S5_SIGNAL_L,
#ifdef CONFIG_POWER_S0IX
	[SYS_SLEEP_S0IX] = GPIO_PCH_SLP_S0_L,
#endif
};

static int power_s5_up; /* Chipset is sequencing up or down */

#ifdef CONFIG_CHARGER
/* Flag to indicate if power up was inhibited due to low battery SOC level. */
static int power_up_inhibited;

/*
 * Check if AP power up should be inhibited.
 * 0 = Ok to boot up AP
 * 1 = AP power up is inhibited.
 */
static int is_power_up_inhibited(void)
{
	/* Defaulting to power button not pressed. */
	const int power_button_pressed = 0;

	return charge_prevent_power_on(power_button_pressed) ||
	       charge_want_shutdown();
}

static void power_up_inhibited_cb(void)
{
	if (!power_up_inhibited)
		return;

	if (is_power_up_inhibited()) {
		CPRINTS("power-up still inhibited");
		return;
	}

	CPRINTS("Battery SOC ok to boot AP!");
	power_up_inhibited = 0;

	chipset_exit_hard_off();
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, power_up_inhibited_cb, HOOK_PRIO_DEFAULT);
#endif

/* Get system sleep state through GPIOs or VWs */
static inline int chipset_get_sleep_signal(enum sys_sleep_state state)
{
	return power_signal_get_level(sleep_sig[state]);
}

#ifdef CONFIG_BOARD_HAS_RTC_RESET
static void intel_x86_rtc_reset(void)
{
	CPRINTS("Asserting RTCRST# to PCH");
	gpio_set_level(GPIO_PCH_RTCRST, 1);
	udelay(100);
	gpio_set_level(GPIO_PCH_RTCRST, 0);
}

static enum power_state power_wait_s5_rtc_reset(void)
{
	static int s5_exit_tries;

	/* Wait for S5 exit and then attempt RTC reset */
	while ((power_get_signals() & IN_PCH_SLP_S4_DEASSERTED) == 0) {
		/* Handle RSMRST passthru event while waiting */
		common_intel_x86_handle_rsmrst(POWER_S5);
		if (task_wait_event(SECOND * CONFIG_S5_EXIT_WAIT) ==
		    TASK_EVENT_TIMER) {
			CPRINTS("timeout waiting for S5 exit");
			chipset_force_g3();

			/* Assert RTCRST# and retry 5 times */
			intel_x86_rtc_reset();

			if (++s5_exit_tries > 4) {
				s5_exit_tries = 0;
				return POWER_G3; /* Stay off */
			}

			udelay(10 * MSEC);
			return POWER_G3S5; /* Power up again */
		}
	}

	s5_exit_tries = 0;
	return POWER_S5S4; /* Power up to next state */
}
#endif

#ifdef CONFIG_POWER_S0IX
/*
 * Backup copies of SCI and SMI mask to preserve across S0ix suspend/resume
 * cycle. If the host uses S0ix, BIOS is not involved during suspend and resume
 * operations and hence SCI/SMI masks are programmed only once during boot-up.
 *
 * These backup variables are set whenever host expresses its interest to
 * enter S0ix and then lpc_host_event_mask for SCI and SMI are cleared. When
 * host resumes from S0ix, masks from backup variables are copied over to
 * lpc_host_event_mask for SCI and SMI.
 */
static host_event_t backup_sci_mask;
static host_event_t backup_smi_mask;

/*
 * Clear host event masks for SMI and SCI when host is entering S0ix. This is
 * done to prevent any SCI/SMI interrupts when the host is in suspend. Since
 * BIOS is not involved in the suspend path, EC needs to take care of clearing
 * these masks.
 */
static void lpc_s0ix_suspend_clear_masks(void)
{
	backup_sci_mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SCI);
	backup_smi_mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SMI);

	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, 0);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, 0);
}

/*
 * Restore host event masks for SMI and SCI when host exits S0ix. This is done
 * because BIOS is not involved in the resume path and so EC needs to restore
 * the masks from backup variables.
 */
static void lpc_s0ix_resume_restore_masks(void)
{
	/*
	 * No need to restore SCI/SMI masks if both backup_sci_mask and
	 * backup_smi_mask are zero. This indicates that there was a failure to
	 * enter S0ix(SLP_S0# assertion) and hence SCI/SMI masks were never
	 * backed up.
	 */
	if (!backup_sci_mask && !backup_smi_mask)
		return;

	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, backup_sci_mask);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, backup_smi_mask);

	backup_sci_mask = backup_smi_mask = 0;
}

__override void power_chipset_handle_sleep_hang(enum sleep_hang_type hang_type)
{
	/*
	 * Wake up the AP so they don't just chill in a non-suspended state and
	 * burn power. Overload a vaguely related event bit since event bits are
	 * at a premium. If the system never entered S0ix, then manually set the
	 * wake mask to pretend it did, so that the hang detect event wakes the
	 * system.
	 */
	if (power_get_state() == POWER_S0) {
		host_event_t sleep_wake_mask;

		get_lazy_wake_mask(POWER_S0ix, &sleep_wake_mask);
		lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, sleep_wake_mask);
	}

	CPRINTS("Warning: Detected sleep hang! Waking host up!");
	host_set_single_event(EC_HOST_EVENT_HANG_DETECT);
}

static void handle_chipset_suspend(void)
{
	/* Clear masks before any hooks are run for suspend. */
	lpc_s0ix_suspend_clear_masks();
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, handle_chipset_suspend, HOOK_PRIO_FIRST);

static void handle_chipset_reset(void)
{
	if (chipset_in_state(CHIPSET_STATE_STANDBY)) {
		CPRINTS("chipset reset: exit s0ix");
		power_reset_host_sleep_state();
		task_wake(TASK_ID_CHIPSET);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, handle_chipset_reset, HOOK_PRIO_FIRST);

void power_reset_host_sleep_state(void)
{
	power_set_host_sleep_state(HOST_SLEEP_EVENT_DEFAULT_RESET);
	sleep_reset_tracking();
	power_chipset_handle_host_sleep_event(HOST_SLEEP_EVENT_DEFAULT_RESET,
					      NULL);
}

#endif /* CONFIG_POWER_S0IX */

void chipset_throttle_cpu(int throttle)
{
#ifdef CONFIG_CPU_PROCHOT_ACTIVE_LOW
	throttle = !throttle;
#endif /* CONFIG_CPU_PROCHOT_ACTIVE_LOW */
	if (chipset_in_state(CHIPSET_STATE_ON))
		gpio_set_level(GPIO_CPU_PROCHOT, throttle);
}

enum power_state power_chipset_init(void)
{
	CPRINTS("%s: power_signal=0x%x", __func__, power_get_signals());

	if (!system_jumped_to_this_image())
		return POWER_G3;
	/*
	 * We are here as RW. We need to handle the following cases:
	 *
	 * 1. Late sysjump by software sync. AP is in S0.
	 * 2. Shutting down in recovery mode then sysjump by EFS2. AP is in S5
	 *    and expected to sequence down.
	 * 3. Rebooting from recovery mode then sysjump by EFS2. AP is in S5
	 *    and expected to sequence up.
	 * 4. RO jumps to RW from main() by EFS2. (a.k.a. power on reset, cold
	 *    reset). AP is in G3.
	 */
	if ((power_get_signals() & IN_ALL_S0) == IN_ALL_S0) {
		/* case #1. Disable idle task deep sleep when in S0. */
		disable_sleep(SLEEP_MASK_AP_RUN);
		CPRINTS("already in S0");
		return POWER_S0;
	}
	if ((power_get_signals() & CHIPSET_G3S5_POWERUP_SIGNAL) ==
	    CHIPSET_G3S5_POWERUP_SIGNAL) {
		/* case #2 & #3 */
		CPRINTS("already in S5");
		return POWER_S5;
	}
	/* case #4 */
	chipset_force_g3();
	return POWER_G3;
}

enum power_state common_intel_x86_power_handle_state(enum power_state state)
{
	switch (state) {
	case POWER_G3:
		break;

	case POWER_S5:
#ifdef CONFIG_BOARD_HAS_RTC_RESET
		/* Wait for S5 exit and attempt RTC reset if supported */
		if (power_s5_up)
			return power_wait_s5_rtc_reset();
#endif

		if (chipset_get_sleep_signal(SYS_SLEEP_S5) == 1)
			return POWER_S5S4; /* Power up to next state */
		break;

	case POWER_S4:
		if (chipset_get_sleep_signal(SYS_SLEEP_S5) == 0) {
			/* Power down to next state */
			return POWER_S4S5;
		} else if (chipset_get_sleep_signal(SYS_SLEEP_S4) == 1) {
			/* Power up to the next level */
			return POWER_S4S3;
		}

		break;

	case POWER_S3:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away, go straight to S5 */
			chipset_force_shutdown(CHIPSET_SHUTDOWN_POWERFAIL);
			return POWER_S3S5;
		} else if (chipset_get_sleep_signal(SYS_SLEEP_S3) == 1) {
			/* Power up to next state */
			return POWER_S3S0;
		} else if (chipset_get_sleep_signal(SYS_SLEEP_S4) == 0) {
			/* Power down to the next state */
			return POWER_S3S4;
		}

		break;

	case POWER_S0:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			chipset_force_shutdown(CHIPSET_SHUTDOWN_POWERFAIL);
			return POWER_S0S3;
		} else if (chipset_get_sleep_signal(SYS_SLEEP_S3) == 0) {
			/* Power down to next state */
			return POWER_S0S3;
#ifdef CONFIG_POWER_S0IX
			/*
			 * SLP_S0 may assert in system idle scenario without a
			 * kernel freeze call. This may cause interrupt storm
			 * since there is no freeze/unfreeze of threads/process
			 * in the idle scenario. Ignore the SLP_S0 assertions in
			 * idle scenario by checking the host sleep state.
			 */
		} else if (power_get_host_sleep_state() ==
				   HOST_SLEEP_EVENT_S0IX_SUSPEND &&
			   chipset_get_sleep_signal(SYS_SLEEP_S0IX) == 0) {
			return POWER_S0S0ix;
		} else {
			sleep_notify_transition(SLEEP_NOTIFY_RESUME,
						HOOK_CHIPSET_RESUME);
#endif
		}

		break;

#ifdef CONFIG_POWER_S0IX
	case POWER_S0ix:
		/* System in S0 only if SLP_S0 and SLP_S3 are de-asserted */
		if ((chipset_get_sleep_signal(SYS_SLEEP_S0IX) == 1) &&
		    (chipset_get_sleep_signal(SYS_SLEEP_S3) == 1)) {
			return POWER_S0ixS0;
		} else if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			return POWER_S0;
		}

		break;
#endif

	case POWER_G3S5:
		if (intel_x86_wait_power_up_ok() != EC_SUCCESS) {
			chipset_force_shutdown(
				CHIPSET_SHUTDOWN_BATTERY_INHIBIT);
			return POWER_G3;
		}
#ifdef CONFIG_CHIPSET_HAS_PRE_INIT_CALLBACK
		/*
		 * Callback to do pre-initialization within the context of
		 * chipset task.
		 */
		chipset_pre_init_callback();
#endif

		if (power_wait_signals(CHIPSET_G3S5_POWERUP_SIGNAL)) {
			chipset_force_shutdown(CHIPSET_SHUTDOWN_WAIT);
			return POWER_G3;
		}

		power_s5_up = 1;
		return POWER_S5;

	case POWER_S5S4:
		return POWER_S4; /* Power up to next state */

	case POWER_S3S4:
		return POWER_S4; /* Power down to the next state */

	case POWER_S5S3:
		__fallthrough;
	case POWER_S4S3:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away */
			chipset_force_shutdown(CHIPSET_SHUTDOWN_POWERFAIL);
			return POWER_S5G3;
		}

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);

#ifdef CONFIG_POWER_S0IX
		/*
		 * Clearing the S0ix flag on the path to S0
		 * to handle any reset conditions.
		 */
		power_reset_host_sleep_state();
#endif
		return POWER_S3;

	case POWER_S3S0:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away, go straight back to S5 */
			chipset_force_shutdown(CHIPSET_SHUTDOWN_POWERFAIL);
			return POWER_S3S5;
		}

		/* Enable wireless */
		wireless_set_state(WIRELESS_ON);

		lpc_s3_resume_clear_masks();

#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
		/* Call hooks prior to chipset resume */
		hook_notify(HOOK_CHIPSET_RESUME_INIT);
#endif
		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		/*
		 * Throttle CPU if necessary.  This should only be asserted
		 * when +VCCP is powered (it is by now).
		 */
#ifdef CONFIG_CPU_PROCHOT_ACTIVE_LOW
		gpio_set_level(GPIO_CPU_PROCHOT, 1);
#else
		gpio_set_level(GPIO_CPU_PROCHOT, 0);
#endif /* CONFIG_CPU_PROCHOT_ACTIVE_LOW */

		return POWER_S0;

	case POWER_S0S3:

		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
		/* Call hooks after chipset suspend */
		hook_notify(HOOK_CHIPSET_SUSPEND_COMPLETE);
#endif

		/* Suspend wireless */
		wireless_set_state(WIRELESS_SUSPEND);

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S3 or lower.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

#ifdef CONFIG_POWER_S0IX
		/* re-init S0ix flag */
		power_reset_host_sleep_state();
#endif
		return POWER_S3;

#ifdef CONFIG_POWER_S0IX
	case POWER_S0S0ix:
		/*
		 * Call hooks only if we haven't notified listeners of S0ix
		 * suspend.
		 */
		sleep_notify_transition(SLEEP_NOTIFY_SUSPEND,
					HOOK_CHIPSET_SUSPEND);
		sleep_suspend_transition();

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S0ix.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
		hook_notify(HOOK_CHIPSET_SUSPEND_COMPLETE);
#endif

		return POWER_S0ix;

	case POWER_S0ixS0:
		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
		hook_notify(HOOK_CHIPSET_RESUME_INIT);
#endif

		sleep_resume_transition();
		return POWER_S0;
#endif

	case POWER_S3S5:
		/* fallthrough */
	case POWER_S4S5:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		/* Disable wireless */
		wireless_set_state(WIRELESS_OFF);

		/* Call hooks after we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);

		/* Always enter into S5 state. The S5 state is required to
		 * correctly handle global resets which have a bit of delay
		 * while the SLP_Sx_L signals are asserted then deasserted.
		 */
		power_s5_up = 0;
		return POWER_S5;

	case POWER_S5G3:
		return chipset_force_g3();

	default:
		break;
	}

	return state;
}

void intel_x86_rsmrst_signal_interrupt(enum gpio_signal signal)
{
	int rsmrst_in = gpio_get_level(GPIO_PG_EC_RSMRST_ODL);
	int rsmrst_out = gpio_get_level(GPIO_PCH_RSMRST_L);

	/*
	 * This function is called when rsmrst changes state. If rsmrst
	 * has been asserted (high -> low) then pass this new state to PCH.
	 */
	if (!rsmrst_in && (rsmrst_in != rsmrst_out))
		gpio_set_level(GPIO_PCH_RSMRST_L, rsmrst_in);

	/*
	 * Call the main power signal interrupt handler to wake up the chipset
	 * task which handles low->high rsmrst pass through.
	 */
	power_signal_interrupt(signal);
}

__overridable void board_before_rsmrst(int rsmrst)
{
}

__overridable void board_after_rsmrst(int rsmrst)
{
}

void common_intel_x86_handle_rsmrst(enum power_state state)
{
	/*
	 * Pass through RSMRST asynchronously, as PCH may not react
	 * immediately to power changes.
	 */
	int rsmrst_in = gpio_get_level(GPIO_PG_EC_RSMRST_ODL);
	int rsmrst_out = gpio_get_level(GPIO_PCH_RSMRST_L);

	/* Nothing to do. */
	if (rsmrst_in == rsmrst_out)
		return;

	board_before_rsmrst(rsmrst_in);

	/* Only passthrough RSMRST_L de-assertion on power up */
	if (IS_ENABLED(CONFIG_CHIPSET_X86_RSMRST_AFTER_S5) && rsmrst_in &&
	    !power_s5_up)
		return;
	/*
	 * Wait at least 10ms between power signals going high
	 * and deasserting RSMRST to PCH.
	 */
	if (IS_ENABLED(CONFIG_CHIPSET_X86_RSMRST_DELAY) && rsmrst_in)
		msleep(10);

	gpio_set_level(GPIO_PCH_RSMRST_L, rsmrst_in);

	update_ap_boot_time(RSMRST);

	CPRINTS("Pass through GPIO_PG_EC_RSMRST_ODL: %d", rsmrst_in);

	board_after_rsmrst(rsmrst_in);
}

#ifdef CONFIG_POWER_TRACK_HOST_SLEEP_STATE

__overridable void
power_board_handle_host_sleep_event(enum host_sleep_event state)
{
	/* Default weak implementation -- no action required. */
}

__override void
power_chipset_handle_host_sleep_event(enum host_sleep_event state,
				      struct host_sleep_event_context *ctx)
{
	power_board_handle_host_sleep_event(state);

#ifdef CONFIG_POWER_S0IX
	if (state == HOST_SLEEP_EVENT_S0IX_SUSPEND) {
		/*
		 * Indicate to power state machine that a new host event for
		 * s0ix/s3 suspend has been received and so chipset suspend
		 * notification needs to be sent to listeners.
		 */
		sleep_set_notify(SLEEP_NOTIFY_SUSPEND);

		sleep_start_suspend(ctx);
		power_signal_enable_interrupt(sleep_sig[SYS_SLEEP_S0IX]);
	} else if (state == HOST_SLEEP_EVENT_S0IX_RESUME) {
		/*
		 * Wake up chipset task and indicate to power state machine that
		 * listeners need to be notified of chipset resume.
		 */
		sleep_set_notify(SLEEP_NOTIFY_RESUME);
		task_wake(TASK_ID_CHIPSET);
		lpc_s0ix_resume_restore_masks();
		power_signal_disable_interrupt(sleep_sig[SYS_SLEEP_S0IX]);
		sleep_complete_resume(ctx);
		/*
		 * If the sleep signal timed out and never transitioned, then
		 * the wake mask was modified to its suspend state (S0ix), so
		 * that the event wakes the system. Explicitly restore the wake
		 * mask to its S0 state now.
		 */
		power_update_wake_mask();
	} else if (state == HOST_SLEEP_EVENT_DEFAULT_RESET) {
		power_signal_disable_interrupt(sleep_sig[SYS_SLEEP_S0IX]);
	}
#endif
}

#endif

__overridable void intel_x86_sys_reset_delay(void)
{
	/*
	 * Debounce time for SYS_RESET_L is 16 ms. Wait twice that period
	 * to be safe.
	 */
	udelay(32 * MSEC);
}

void chipset_reset(enum chipset_shutdown_reason reason)
{
	/*
	 * Irrespective of cold_reset value, always toggle SYS_RESET_L to
	 * perform a chipset reset. RCIN# which was used earlier to trigger
	 * a warm reset is known to not work in certain cases where the CPU
	 * is in a bad state (crbug.com/721853).
	 *
	 * The EC cannot control warm vs cold reset of the chipset using
	 * SYS_RESET_L; it's more of a request.
	 */
	CPRINTS("%s: %d", __func__, reason);

	/*
	 * Toggling SYS_RESET_L will not have any impact when it's already
	 * low (i,e. Chipset is in reset state).
	 */
	if (gpio_get_level(GPIO_SYS_RESET_L) == 0) {
		CPRINTS("Chipset is in reset state");
		return;
	}

	report_ap_reset(reason);

	gpio_set_level(GPIO_SYS_RESET_L, 0);
	intel_x86_sys_reset_delay();
	gpio_set_level(GPIO_SYS_RESET_L, 1);
}

enum ec_error_list intel_x86_wait_power_up_ok(void)
{
#ifdef CONFIG_CHARGER
	int tries = 0;

	/*
	 * Allow charger to be initialized for up to defined tries,
	 * in case we're trying to boot the AP with no battery.
	 */
	while ((tries < CHARGER_INITIALIZED_TRIES) && is_power_up_inhibited()) {
		msleep(CHARGER_INITIALIZED_DELAY_MS);
		tries++;
	}

	/*
	 * Return to G3 if battery level is too low. Set
	 * power_up_inhibited in order to check the eligibility to boot
	 * AP up after battery SOC changes.
	 */
	if (tries == CHARGER_INITIALIZED_TRIES) {
		CPRINTS("power-up inhibited");
		power_up_inhibited = 1;
		return EC_ERROR_TIMEOUT;
	}

	power_up_inhibited = 0;
#endif

#if defined(CONFIG_VBOOT_EFS) || defined(CONFIG_VBOOT_EFS2)
	/*
	 * We have to test power readiness here (instead of S5->S3)
	 * because when entering S5, EC enables EC_ROP_SLP_SUS pin
	 * which causes (short-powered) system to brown out.
	 */
	while (!system_can_boot_ap())
		msleep(200);
#endif

	return EC_SUCCESS;
}
