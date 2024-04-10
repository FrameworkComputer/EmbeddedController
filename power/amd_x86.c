/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* AMD x86 power sequencing module for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/amd_stb.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "lpc.h"
#include "power.h"
#include "power/amd_x86.h"
#include "power_button.h"
#include "registers.h"
#include "system.h"
#include "timer.h"
#include "usb_charge.h"
#include "util.h"
#include "wireless.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

#define IN_S5_PGOOD POWER_SIGNAL_MASK(X86_S5_PGOOD)

static int forcing_shutdown; /* Forced shutdown in progress? */

#ifdef CONFIG_POWERSEQ_FAKE_CONTROL
/* Create fake power states through forcing the SoC SLP signal sequencing */
void power_fake_s0(void)
{
	/* Change the SLP signals to output and drive them */
	gpio_set_flags(GPIO_PCH_SLP_S5_L, GPIO_OUT_HIGH);
	gpio_set_flags(GPIO_PCH_SLP_S3_L, GPIO_OUT_HIGH);
}

void power_fake_disable(void)
{
	/* Pins back to inputs */
	gpio_set_flags(GPIO_PCH_SLP_S5_L, GPIO_INPUT);
	gpio_set_flags(GPIO_PCH_SLP_S3_L, GPIO_INPUT);
}
#endif /* defined(CONFIG_POWERSEQ_FAKE_CONTROL) */

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s()", __func__);

	if (!chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF)) {
		forcing_shutdown = 1;
		power_button_pch_press();
		report_ap_reset(reason);
	}
}

static void chipset_force_g3(void)
{
	/* Disable system power ("*_A" rails) in G3. */
	gpio_set_level(GPIO_EN_PWR_A, 0);
}

void chipset_reset(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s: %d", __func__, reason);

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		CPRINTS("Can't reset: SOC is off");
		return;
	}

	if (IS_ENABLED(CONFIG_PLATFORM_EC_AMD_STB_DUMP) &&
	    amd_stb_dump_in_progress()) {
		CPRINTS("STB dump still in progress during reset");
		amd_stb_dump_finish();
	}

	report_ap_reset(reason);

#ifdef CONFIG_CHIPSET_RESET_HOOK
	hook_notify(HOOK_CHIPSET_RESET);
#endif
	/*
	 * Send a pulse to SYS_RST to trigger a warm reset.
	 */
	gpio_set_level(GPIO_SYS_RESET_L, 0);
	crec_usleep(32 * MSEC);
	gpio_set_level(GPIO_SYS_RESET_L, 1);
}

void chipset_throttle_cpu(int throttle)
{
	CPRINTS("%s(%d)", __func__, throttle);
	if (IS_ENABLED(CONFIG_CPU_PROCHOT_ACTIVE_LOW))
		throttle = !throttle;

	if (!chipset_in_state(CHIPSET_STATE_ON))
		return;

	if (IS_ENABLED(CONFIG_THROTTLE_AP_INTERRUPT_SINGLE)) {
		if (throttle == IS_ENABLED(CONFIG_CPU_PROCHOT_ACTIVE_LOW)) {
			/* Enable interrupt if we're not throttling the AP */
			gpio_set_flags(GPIO_CPU_PROCHOT, GPIO_INPUT);
			gpio_enable_interrupt(GPIO_CPU_PROCHOT);

			if ((!gpio_get_level(GPIO_CPU_PROCHOT)) ==
			    IS_ENABLED(CONFIG_CPU_PROCHOT_ACTIVE_LOW)) {
				CPRINTS("External prochot during throttling");
			}
			return;
		}

		/* Otherwise restore our original GPIO settings */
		gpio_disable_interrupt(GPIO_CPU_PROCHOT);
		gpio_set_flags(GPIO_CPU_PROCHOT,
			       gpio_get_default_flags(GPIO_CPU_PROCHOT));
	}

	gpio_set_level(GPIO_CPU_PROCHOT, throttle);
}

void chipset_handle_espi_reset_assert(void)
{
	/*
	 * eSPI_Reset# pin being asserted without RSMRST# being asserted
	 * means there is an unexpected power loss (global reset event).
	 * In this case, check if the shutdown is forced by the EC (due
	 * to battery, thermal, or console command). The forced shutdown
	 * initiates a power button press that we need to release.
	 *
	 * NOTE: S5_PGOOD input is passed through to the RSMRST# output to
	 * the AP.
	 */
	if ((power_get_signals() & IN_S5_PGOOD) && forcing_shutdown) {
		power_button_pch_release();
		forcing_shutdown = 0;
	}
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
	if (gpio_get_level(GPIO_S0_PGOOD)) {
		/* case #1. Disable idle task deep sleep when in S0. */
		disable_sleep(SLEEP_MASK_AP_RUN);
		CPRINTS("already in S0");
		return POWER_S0;
	}
	if (power_get_signals() & IN_S5_PGOOD) {
		/* case #2 & #3 */
		CPRINTS("already in S5");
		return POWER_S5;
	}
	/* case #4 */
	chipset_force_g3();
	return POWER_G3;
}

static void handle_pass_through(enum gpio_signal pin_in,
				enum gpio_signal pin_out)
{
	/*
	 * Pass through asynchronously, as SOC may not react
	 * immediately to power changes.
	 */
	int in_level = gpio_get_level(pin_in);
	int out_level = gpio_get_level(pin_out);

	/*
	 * Only pass through high S0_PGOOD (S0 power) when S5_PGOOD (S5 power)
	 * is also high (S0_PGOOD is pulled high in G3 when S5_PGOOD is low).
	 */
	if ((pin_in == GPIO_S0_PGOOD) && !gpio_get_level(GPIO_S5_PGOOD))
		in_level = 0;

	/* Nothing to do. */
	if (in_level == out_level)
		return;

	/*
	 * SOC requires a delay of 1ms with stable power before
	 * asserting PWR_GOOD.
	 */
	if ((pin_in == GPIO_S0_PGOOD) && in_level)
		crec_msleep(1);

	if (IS_ENABLED(CONFIG_CHIPSET_X86_RSMRST_DELAY) &&
	    (pin_out == GPIO_PCH_RSMRST_L) && in_level)
		crec_msleep(10);

	gpio_set_level(pin_out, in_level);

	CPRINTS("Pass through %s: %d", gpio_get_name(pin_in), in_level);
}

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
	if (IS_ENABLED(CONFIG_PLATFORM_EC_AMD_STB_DUMP))
		amd_stb_dump_trigger();
}

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
		 * Clear event mask for SMI and SCI first to avoid host being
		 * interrupted while suspending.
		 */
		lpc_s0ix_suspend_clear_masks();
		/*
		 * Indicate to power state machine that a new host event for
		 * s0ix/s3 suspend has been received and so chipset suspend
		 * notification needs to be sent to listeners.
		 */
		sleep_set_notify(SLEEP_NOTIFY_SUSPEND);

		sleep_start_suspend(ctx);
	} else if (state == HOST_SLEEP_EVENT_S0IX_RESUME) {
		/*
		 * Wake up chipset task and indicate to power state machine that
		 * listeners need to be notified of chipset resume.
		 */
		sleep_set_notify(SLEEP_NOTIFY_RESUME);
		task_wake(TASK_ID_CHIPSET);
		lpc_s0ix_resume_restore_masks();
		sleep_complete_resume(ctx);
		/*
		 * If the sleep signal timed out and never transitioned, then
		 * the wake mask was modified to its suspend state (S0ix), so
		 * that the event wakes the system. Explicitly restore the wake
		 * mask to its S0 state now.
		 */
		power_update_wake_mask();
	}
#endif /* CONFIG_POWER_S0IX */
}
#endif /* CONFIG_POWER_TRACK_HOST_SLEEP_STATE */

enum power_state power_handle_state(enum power_state state)
{
	handle_pass_through(GPIO_S5_PGOOD, GPIO_PCH_RSMRST_L);

	handle_pass_through(GPIO_S0_PGOOD, GPIO_PCH_SYS_PWROK);

	if (state == POWER_S5 && forcing_shutdown) {
		power_button_pch_release();
		forcing_shutdown = 0;
	}

	switch (state) {
	case POWER_G3:
		break;

	case POWER_G3S5:
		/* Exit SOC G3 */
		/* Enable system power ("*_A" rails) in S5. */
		gpio_set_level(GPIO_EN_PWR_A, 1);

		/*
		 * Callback to do pre-initialization within the context of
		 * chipset task.
		 */
		if (IS_ENABLED(CONFIG_CHIPSET_HAS_PRE_INIT_CALLBACK))
			chipset_pre_init_callback();

		if (power_wait_signals(IN_S5_PGOOD)) {
			chipset_force_g3();
			return POWER_G3;
		}

		CPRINTS("Exit SOC G3");

		return POWER_S5;

	case POWER_S5:
		if (!power_has_signals(IN_S5_PGOOD)) {
			/* Required rail went away */
			return POWER_S5G3;
		} else if (gpio_get_level(GPIO_PCH_SLP_S5_L) == 1) {
			/* Power up to next state */
			return POWER_S5S3;
		}
		break;

	case POWER_S5S3:
		if (!power_has_signals(IN_S5_PGOOD)) {
			/* Required rail went away */
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

	case POWER_S3:
		if (!power_has_signals(IN_S5_PGOOD)) {
			/* Required rail went away */
			return POWER_S5G3;
		} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 1) {
			/* Power up to next state */
			return POWER_S3S0;
		} else if (gpio_get_level(GPIO_PCH_SLP_S5_L) == 0) {
			/* Power down to next state */
			return POWER_S3S5;
		}
		break;

	case POWER_S3S0:
		if (!power_has_signals(IN_S5_PGOOD)) {
			/* Required rail went away */
			return POWER_S5G3;
		}

		/* Enable wireless */
		wireless_set_state(WIRELESS_ON);

		lpc_s3_resume_clear_masks();

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		return POWER_S0;

	case POWER_S0:
		if (!power_has_signals(IN_S5_PGOOD)) {
			/* Required rail went away */
			return POWER_S5G3;
		}
#ifdef CONFIG_POWER_S0IX
		/*
		 * SLP_S0 may assert in system idle scenario without a kernel
		 * freeze call. This may cause interrupt storm since there is
		 * no freeze/unfreeze of threads/process in the idle scenario.
		 * Ignore the SLP_S0 assertions in idle scenario by checking
		 * the host sleep state.
		 */
		else if (power_get_host_sleep_state() ==
				 HOST_SLEEP_EVENT_S0IX_SUSPEND &&
			 gpio_get_level(GPIO_PCH_SLP_S0_L) == 0) {
			return POWER_S0S0ix;
		}
#endif
		else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 0) {
			/* Power down to next state */
			return POWER_S0S3;
		}
#ifdef CONFIG_POWER_S0IX
		/*
		 * Call hooks only if we haven't notified listeners of S0ix
		 * resume.
		 */
		sleep_notify_transition(SLEEP_NOTIFY_RESUME,
					HOOK_CHIPSET_RESUME);
#endif
		break;

	case POWER_S0S3:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);

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

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		/* Disable wireless */
		wireless_set_state(WIRELESS_OFF);

		/* Call hooks after we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);

		return POWER_S5;

	case POWER_S5G3:
		chipset_force_g3();

		return POWER_G3;

#ifdef CONFIG_POWER_S0IX
	case POWER_S0ix:
		/* System in S0 only if SLP_S0 and SLP_S3 are de-asserted */
		if ((gpio_get_level(GPIO_PCH_SLP_S0_L) == 1) &&
		    (gpio_get_level(GPIO_PCH_SLP_S3_L) == 1)) {
			return POWER_S0ixS0;
		} else if (!power_has_signals(IN_S5_PGOOD) ||
			   (gpio_get_level(GPIO_PCH_SLP_S5_L) == 0)) {
			/* Lost power or AP shutdown, start transition to G3 */
			power_reset_host_sleep_state();
			return POWER_S0;
		}
		break;

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
		return POWER_S0ix;

	case POWER_S0ixS0:
		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		sleep_resume_transition();
		return POWER_S0;
#endif /* CONFIG_POWER_S0IX */
	default:
		break;
	}
	return state;
}
