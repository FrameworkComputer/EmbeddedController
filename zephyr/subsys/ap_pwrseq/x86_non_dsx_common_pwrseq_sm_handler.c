/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_reset_log.h"
#include "system_boot_time.h"
#include "zephyr_console_shim.h"

#include <zephyr/init.h>

#include <atomic.h>
#ifndef CONFIG_AP_PWRSEQ_DRIVER
#include <x86_non_dsx_common_pwrseq_sm_handler.h>
#else
#include "ap_power/ap_pwrseq.h"
#include "ap_power/ap_pwrseq_sm.h"
#include "x86_non_dsx_common_pwrseq_sm_handler.h"
#include "zephyr_console_shim.h"
#endif

/* Delay in ms when starting from G3 */
static uint32_t start_from_g3_delay_ms;

#ifdef CONFIG_AP_PWRSEQ_DEBUG_MODE_COMMAND
static bool in_debug_mode;
#endif

/*
 * Flags, may be set/cleared from other threads.
 */
enum {
	S5_INACTIVE_TIMER_RUNNING,
	START_FROM_G3,
	FLAGS_MAX,
};
static ATOMIC_DEFINE(flags, FLAGS_MAX);

#ifndef CONFIG_AP_PWRSEQ_DRIVER
static K_KERNEL_STACK_DEFINE(pwrseq_thread_stack, CONFIG_AP_PWRSEQ_STACK_SIZE);
static struct k_thread pwrseq_thread_data;
static struct pwrseq_context pwrseq_ctx = {
	.power_state = SYS_POWER_STATE_UNINIT,
};
static struct k_sem pwrseq_sem;

static void s5_inactive_timer_handler(struct k_timer *timer);
/* S5 inactive timer*/
K_TIMER_DEFINE(s5_inactive_timer, s5_inactive_timer_handler, NULL);

LOG_MODULE_REGISTER(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

/**
 * @brief power_state names for debug
 */
static const char *const pwrsm_dbg[] = {
	[SYS_POWER_STATE_UNINIT] = "Unknown",
	[SYS_POWER_STATE_G3] = "G3",
	[SYS_POWER_STATE_S5] = "S5",
	[SYS_POWER_STATE_S4] = "S4",
	[SYS_POWER_STATE_S3] = "S3",
#if CONFIG_AP_PWRSEQ_S0IX
	[SYS_POWER_STATE_S0ix] = "S0ix",
#endif
	[SYS_POWER_STATE_S0] = "S0",
	[SYS_POWER_STATE_G3S5] = "G3S5",
	[SYS_POWER_STATE_S5S4] = "S5S4",
	[SYS_POWER_STATE_S4S3] = "S4S3",
	[SYS_POWER_STATE_S3S0] = "S3S0",
	[SYS_POWER_STATE_S5G3] = "S5G3",
	[SYS_POWER_STATE_S4S5] = "S4S5",
	[SYS_POWER_STATE_S3S4] = "S3S4",
	[SYS_POWER_STATE_S0S3] = "S0S3",
#if CONFIG_AP_PWRSEQ_S0IX
	[SYS_POWER_STATE_S0ixS0] = "S0ixS0",
	[SYS_POWER_STATE_S0S0ix] = "S0S0ix",
#endif
};
#else
static void x86_non_dsx_timer_handler(struct k_timer *timer);

K_TIMER_DEFINE(x86_non_dsx_timer, x86_non_dsx_timer_handler, NULL);

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

/*
 * Returns true if all signals in mask are valid.
 * This is only done for virtual wire signals.
 */
static inline bool signals_valid(power_signal_mask_t signals)
{
#if defined(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI_VW_SLP_S3)
	if ((signals & POWER_SIGNAL_MASK(PWR_SLP_S3)) &&
	    power_signal_get(PWR_SLP_S3) < 0)
		return false;
#endif
#if defined(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI_VW_SLP_S4)
	if ((signals & POWER_SIGNAL_MASK(PWR_SLP_S4)) &&
	    power_signal_get(PWR_SLP_S4) < 0)
		return false;
#endif
#if defined(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI_VW_SLP_S5)
	if ((signals & POWER_SIGNAL_MASK(PWR_SLP_S5)) &&
	    power_signal_get(PWR_SLP_S5) < 0)
		return false;
#endif
	return true;
}

static inline bool signals_valid_and_on(power_signal_mask_t signals)
{
	return signals_valid(signals) && power_signals_on(signals);
}

static inline bool signals_valid_and_off(power_signal_mask_t signals)
{
	return signals_valid(signals) && power_signals_off(signals);
}

#ifndef CONFIG_AP_PWRSEQ_DRIVER
enum power_states_ndsx pwr_sm_get_state(void)
{
	return pwrseq_ctx.power_state;
}

const char *const pwr_sm_get_state_name(enum power_states_ndsx state)
{
	return pwrsm_dbg[state];
}

void pwr_sm_set_state(enum power_states_ndsx new_state)
{
	/* Add locking mechanism if multiple thread can update it */
	LOG_DBG("Power state: %s --> %s",
		pwr_sm_get_state_name(pwrseq_ctx.power_state),
		pwr_sm_get_state_name(new_state));
	pwrseq_ctx.power_state = new_state;
}

void ap_pwrseq_wake(void)
{
	k_sem_give(&pwrseq_sem);
}

/*
 * Set a flag to enable starting the AP once it is in G3.
 * This is called from ap_power_exit_hardoff() which checks
 * to ensure that the AP is in S5 or G3 state before calling
 * this function.
 * It can also be called via a hostcmd, which allows the flag
 * to be set in any AP state.
 */
void request_start_from_g3(void)
{
	LOG_INF("Request start from G3");
	atomic_set_bit(flags, START_FROM_G3);
	/*
	 * If in S5, restart the timer to give the CPU more time
	 * to respond to a power button press (which is presumably
	 * why we are being called). This avoids having the S5
	 * inactivity timer expiring before the AP can process
	 * the power button press and start up.
	 */
	if (pwr_sm_get_state() == SYS_POWER_STATE_S5) {
		atomic_clear_bit(flags, S5_INACTIVE_TIMER_RUNNING);
	}
	ap_pwrseq_wake();
}

static void s5_inactive_timer_handler(struct k_timer *timer)
{
	ap_pwrseq_wake();
}

static void shutdown_and_notify(enum ap_power_shutdown_reason reason)
{
	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
	ap_power_force_shutdown(reason);
	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN_COMPLETE);
}

void apshutdown(void)
{
	if (pwr_sm_get_state() != SYS_POWER_STATE_G3) {
		shutdown_and_notify(AP_POWER_SHUTDOWN_G3);
		pwr_sm_set_state(SYS_POWER_STATE_G3);
	}
}
#else
const char *const pwr_sm_get_state_name(enum ap_pwrseq_state state)
{
	return ap_pwrseq_get_state_str(state);
}

static void x86_non_dsx_timer_handler(struct k_timer *timer)
{
	if (atomic_test_bit(flags, S5_INACTIVE_TIMER_RUNNING)) {
		ap_pwrseq_post_event(ap_pwrseq_get_instance(),
				     AP_PWRSEQ_EVENT_POWER_TIMEOUT);
	} else if (atomic_test_bit(flags, START_FROM_G3)) {
		ap_pwrseq_post_event(ap_pwrseq_get_instance(),
				     AP_PWRSEQ_EVENT_POWER_STARTUP);
	}
}

void request_start_from_g3(void)
{
	const struct device *dev = ap_pwrseq_get_instance();

	LOG_INF("Request start from G3");

	if (!board_ap_power_is_startup_ok()) {
		LOG_INF("Start from G3 inhibited"
			" by !is_startup_ok");
		return;
	}

	/*
	 * If in S5, restart the timer to give the CPU more time
	 * to respond to a power button press (which is presumably
	 * why we are being called). This avoids having the S5
	 * inactivity timer expiring before the AP can process
	 * the power button press and start up.
	 */
	if ((ap_pwrseq_get_current_state(dev) == AP_POWER_STATE_S5) &&
	    (AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout) != 0)) {
		k_timer_start(
			&x86_non_dsx_timer,
			K_SECONDS(AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout)),
			K_NO_WAIT);
		return;
	}

	atomic_set_bit(flags, START_FROM_G3);
	if (ap_pwrseq_get_current_state(dev) == AP_POWER_STATE_G3) {
		if (start_from_g3_delay_ms) {
			k_timer_start(&x86_non_dsx_timer,
				      K_MSEC(start_from_g3_delay_ms),
				      K_NO_WAIT);
			start_from_g3_delay_ms = 0;
		} else {
			ap_pwrseq_post_event(dev,
					     AP_PWRSEQ_EVENT_POWER_STARTUP);
		}
	}
}

void apshutdown(void)
{
	const struct device *dev = ap_pwrseq_get_instance();

	ap_pwrseq_state_lock(dev);

	if (ap_pwrseq_get_current_state(dev) != AP_POWER_STATE_G3) {
		ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
	}

	ap_pwrseq_state_unlock(dev);
}
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

void ap_power_force_shutdown(enum ap_power_shutdown_reason reason)
{
#ifdef CONFIG_AP_PWRSEQ_DEBUG_MODE_COMMAND
	/* This prevents force shutdown if debug mode is enabled */
	if (in_debug_mode) {
		LOG_WRN("debug_mode is enabled, preventing force shutdown");
		return;
	}
#endif /* CONFIG_AP_PWRSEQ_DEBUG_MODE_COMMAND */

	report_ap_reset((enum chipset_shutdown_reason)reason);

	board_ap_power_force_shutdown();
}

void set_start_from_g3_delay_seconds(uint32_t d_time)
{
	start_from_g3_delay_ms = d_time * MSEC;
}

void ap_power_reset(enum ap_power_shutdown_reason reason)
{
	/*
	 * Irrespective of cold_reset value, always toggle SYS_RESET_L to
	 * perform an AP reset. RCIN# which was used earlier to trigger
	 * a warm reset is known to not work in certain cases where the CPU
	 * is in a bad state (crbug.com/721853).
	 *
	 * The EC cannot control warm vs cold reset of the AP using
	 * SYS_RESET_L; it's more of a request.
	 */
	LOG_DBG("%s: %d", __func__, reason);

	/*
	 * Toggling SYS_RESET_L will not have any impact when it's already
	 * low (i,e. AP is in reset state).
	 */
	if (power_signal_get(PWR_SYS_RST)) {
		LOG_DBG("Chipset is in reset state");
		return;
	}

	report_ap_reset((enum chipset_shutdown_reason)reason);

	power_signal_set(PWR_SYS_RST, 1);
	/*
	 * Debounce time for SYS_RESET_L is 16 ms. Wait twice that period
	 * to be safe.
	 */
	k_msleep(AP_PWRSEQ_DT_VALUE(sys_reset_delay));
	power_signal_set(PWR_SYS_RST, 0);
	ap_power_ev_send_callbacks(AP_POWER_RESET);
}

/* Check RSMRST is fine to move from S5 to higher state */
int rsmrst_power_is_good(void)
{
	/* TODO: Check if this is still intact */
	return power_signal_get(PWR_RSMRST_PWRGD);
}

/* Handling RSMRST signal is mostly common across x86 chipsets */
void rsmrst_pass_thru_handler(void)
{
	/* Handle RSMRST passthrough */
	/* TODO: Add additional conditions for RSMRST handling */
	if (power_signal_get(PWR_RSMRST_PWRGD)) {
		if (power_signal_get(PWR_EC_PCH_RSMRST)) {
			/*
			 * Delay `PWR_EC_PCH_RSMRST` de-assertion for at least
			 * `rsmrst_delay` after detecting that power wells are
			 * stable.
			 */
			k_msleep(AP_PWRSEQ_DT_VALUE(rsmrst_delay));
			LOG_DBG("Deasserting PWR_EC_PCH_RSMRST");
			power_signal_set(PWR_EC_PCH_RSMRST, 0);
			update_ap_boot_time(RSMRST);
		}
	} else {
		power_signal_set(PWR_EC_PCH_RSMRST, 1);
	}
}

#ifndef CONFIG_AP_PWRSEQ_DRIVER
/* Common power sequencing */
static int common_pwr_sm_run(int state)
{
	switch (state) {
	case SYS_POWER_STATE_G3:
		/*
		 * If the START_FROM_G3 flag is set, begin starting
		 * the AP. There may be a delay set, so only start
		 * after that delay.
		 */
		if (atomic_test_and_clear_bit(flags, START_FROM_G3)) {
			LOG_INF("Starting from G3, delay %d ms",
				start_from_g3_delay_ms);
			k_msleep(start_from_g3_delay_ms);
			start_from_g3_delay_ms = 0;

			if (!board_ap_power_is_startup_ok()) {
				LOG_INF("Start from G3 inhibited"
					" by !is_startup_ok");
				break;
			}
			return SYS_POWER_STATE_G3S5;
		}

		break;

	case SYS_POWER_STATE_G3S5:
		if ((power_get_signals() & PWRSEQ_G3S5_UP_SIGNAL) ==
		    PWRSEQ_G3S5_UP_VALUE)
			return SYS_POWER_STATE_S5;
		else
			return SYS_POWER_STATE_S5G3;

	case SYS_POWER_STATE_S5:
		/* In S5 make sure no more signal lost */
		/* If A-rails are stable then move to higher state */
		if (board_ap_power_check_power_rails_enabled() &&
		    rsmrst_power_is_good()) {
			/* rsmrst is intact */
			rsmrst_pass_thru_handler();
			if (signals_valid_and_off(IN_PCH_SLP_S5)) {
				k_timer_stop(&s5_inactive_timer);
				/* Clear the timer running flag */
				atomic_clear_bit(flags,
						 S5_INACTIVE_TIMER_RUNNING);
				/* Clear any request to exit hard-off */
				atomic_clear_bit(flags, START_FROM_G3);
				LOG_INF("Clearing request to exit G3");
				return SYS_POWER_STATE_S5S4;
			}
		}
		/*
		 * S5 state has an inactivity timer, so moving
		 * to S5G3 (where the power rails are turned off) is
		 * delayed for some time, usually ~10 seconds or so.
		 * The purpose of this delay is:
		 *  - to handle AP initiated cold boot, where the AP
		 *    will go to S5 for a short time and then restart.
		 *  - give time for the power button to be pressed,
		 *    which may set the START_FROM_G3 flag.
		 */
		if (AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout) == 0)
			return SYS_POWER_STATE_S5G3;
		else if (AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout) > 0) {
			/*
			 * Test and set timer running flag.
			 * If it was 0, then the timer wasn't running
			 * and it is started (and the flag is set),
			 * otherwise it is already set, so no change.
			 */
			if (!atomic_test_and_set_bit(
				    flags, S5_INACTIVE_TIMER_RUNNING)) {
				/*
				 * Timer is not started, or needs
				 * restarting.
				 */
				k_timer_start(&s5_inactive_timer,
					      K_SECONDS(AP_PWRSEQ_DT_VALUE(
						      s5_inactivity_timeout)),
					      K_NO_WAIT);
			} else if (k_timer_status_get(&s5_inactive_timer) > 0) {
				/* Timer is expired */
				atomic_clear_bit(flags,
						 S5_INACTIVE_TIMER_RUNNING);
				return SYS_POWER_STATE_S5G3;
			}
		}
		break;

	case SYS_POWER_STATE_S5G3:
		/* Nofity power event after we remove power rails */
		ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);

		/* Notify power event before we enter G3 */
		ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
		return SYS_POWER_STATE_G3;

	case SYS_POWER_STATE_S5S4:
		/* Check if the PCH has come out of suspend state */
		if (rsmrst_power_is_good()) {
			LOG_DBG("RSMRST is ok");
			return SYS_POWER_STATE_S4;
		}
		LOG_DBG("RSMRST is not ok");
		return SYS_POWER_STATE_S5;

	case SYS_POWER_STATE_S4:
		if (signals_valid_and_on(IN_PCH_SLP_S5) ||
		    !rsmrst_power_is_good())
			return SYS_POWER_STATE_S4S5;
		else if (signals_valid_and_off(IN_PCH_SLP_S4))
			return SYS_POWER_STATE_S4S3;

		break;

	case SYS_POWER_STATE_S4S3:
		if (!chipset_is_prim_power_good()) {
			/* Required rail went away */
			shutdown_and_notify(AP_POWER_SHUTDOWN_POWERFAIL);
			return SYS_POWER_STATE_G3;
		}

		/* Notify power event that rails are up */
		ap_power_ev_send_callbacks(AP_POWER_STARTUP);

#if CONFIG_AP_PWRSEQ_S0IX
		/*
		 * Clearing the S0ix flag on the path to S0
		 * to handle any reset conditions.
		 */
		ap_power_reset_host_sleep_state();
#endif
		return SYS_POWER_STATE_S3;

	case SYS_POWER_STATE_S3:
		/* AP is out of suspend to RAM */
		if (!rsmrst_power_is_good()) {
			LOG_WRN("RSMRST is not GOOD");
			return SYS_POWER_STATE_S3S4;
		}
		if (!chipset_is_prim_power_good()) {
			/* Required rail went away, go straight to S5 */
			shutdown_and_notify(AP_POWER_SHUTDOWN_POWERFAIL);
			return SYS_POWER_STATE_G3;
		} else if (signals_valid_and_off(IN_PCH_SLP_S3))
			return SYS_POWER_STATE_S3S0;
		else if (signals_valid_and_on(IN_PCH_SLP_S4))
			return SYS_POWER_STATE_S3S4;

		break;

	case SYS_POWER_STATE_S3S0:
		if (!chipset_is_prim_power_good()) {
			shutdown_and_notify(AP_POWER_SHUTDOWN_POWERFAIL);
			return SYS_POWER_STATE_G3;
		}
		if (!rsmrst_power_is_good()) {
			return SYS_POWER_STATE_S3;
		}

		/* All the power rails must be stable */
		if (power_signal_get(PWR_ALL_SYS_PWRGD)) {
			/*
			 * Disable idle task deep sleep when in S0.
			 */
			disable_sleep(SLEEP_MASK_AP_RUN);
#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
			/* Notify power event before resume */
			ap_power_ev_send_callbacks(AP_POWER_RESUME_INIT);
#endif
			/* Notify power event rails are up */
			ap_power_ev_send_callbacks(AP_POWER_RESUME);
			return SYS_POWER_STATE_S0;
		}
		break;

#if CONFIG_AP_PWRSEQ_S0IX
	case SYS_POWER_STATE_S0ix:
		/* System in S0 only if SLP_S0 and SLP_S3 are de-asserted */
		if (power_signals_off(IN_PCH_SLP_S0) &&
		    signals_valid_and_off(IN_PCH_SLP_S3)) {
			/* TODO: Make sure ap reset handling is done
			 * before leaving S0ix.
			 */
			return SYS_POWER_STATE_S0ixS0;
		} else if (!chipset_is_all_power_good())
			return SYS_POWER_STATE_S0;

		break;

	case SYS_POWER_STATE_S0S0ix:
		/*
		 * Check sleep state and notify listeners of S0ix suspend if
		 * HC already set sleep suspend state.
		 */
		ap_power_sleep_notify_transition(AP_POWER_SLEEP_SUSPEND);
		ap_power_ev_send_callbacks(AP_POWER_S0IX_SUSPEND);

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S0ix.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
		ap_power_ev_send_callbacks(AP_POWER_SUSPEND_COMPLETE);
#endif

		return SYS_POWER_STATE_S0ix;

	case SYS_POWER_STATE_S0ixS0:
		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		ap_power_ev_send_callbacks(AP_POWER_S0IX_RESUME
#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
					   | AP_POWER_RESUME_INIT
#endif
		);

		return SYS_POWER_STATE_S0;

#endif /* CONFIG_AP_PWRSEQ_S0IX */

	case SYS_POWER_STATE_S0:
		if (!chipset_is_prim_power_good()) {
			shutdown_and_notify(AP_POWER_SHUTDOWN_POWERFAIL);
			return SYS_POWER_STATE_G3;
		} else if (signals_valid_and_on(IN_PCH_SLP_S3)) {
			return SYS_POWER_STATE_S0S3;

#if CONFIG_AP_PWRSEQ_S0IX
			/*
			 * SLP_S0 may assert in system idle scenario without a
			 * kernel freeze call. This may cause interrupt storm
			 * since there is no freeze/unfreeze of threads/process
			 * in the idle scenario. Ignore the SLP_S0 assertions in
			 * idle scenario by checking the host sleep state.
			 */
		} else if (ap_power_sleep_get_notify() ==
				   AP_POWER_SLEEP_SUSPEND &&
			   power_signals_on(IN_PCH_SLP_S0)) {
			return SYS_POWER_STATE_S0S0ix;
		} else if (ap_power_sleep_get_notify() ==
			   AP_POWER_SLEEP_RESUME) {
			ap_power_sleep_notify_transition(AP_POWER_SLEEP_RESUME);
#endif /* CONFIG_AP_PWRSEQ_S0IX */
		}

		break;

	case SYS_POWER_STATE_S4S5:
		/* Notify power event before we remove power rails */
		ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);

		/*
		 * If support controlling power of wifi/WWAN/BT devices
		 * add handling here.
		 */
		ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN_COMPLETE);

		/* Always enter into S5 state. The S5 state is required to
		 * correctly handle global resets which have a bit of delay
		 * while the SLP_Sx_L signals are asserted then deasserted.
		 */
		/* TODO */
		/* power_s5_up = 0; */

		return SYS_POWER_STATE_S5;

	case SYS_POWER_STATE_S3S4:
		return SYS_POWER_STATE_S4;

	case SYS_POWER_STATE_S0S3:
		/* Notify power event before we remove power rails */
		ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
		/* Notify power event after suspend */
		ap_power_ev_send_callbacks(AP_POWER_SUSPEND_COMPLETE);
#endif

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S3 or lower.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

#if CONFIG_AP_PWRSEQ_S0IX
		/* Re-initialize S0ix flag */
		ap_power_reset_host_sleep_state();
#endif

		return SYS_POWER_STATE_S3;

	default:
		break;
	}

	return state;
}

/*
 * Determine the current CPU state and ensure it
 * is matching what is required.
 */
static void pwr_seq_set_initial_state(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	/* Determine current state using chipset specific handler */
	enum power_states_ndsx state = chipset_pwr_seq_get_state();

	/*
	 * Not in warm boot, but CPU is not shutdown.
	 */
	if (((reset_flags & EC_RESET_FLAG_SYSJUMP) == 0) &&
	    (state != SYS_POWER_STATE_G3)) {
		ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
		state = SYS_POWER_STATE_G3;
	}
	pwr_sm_set_state(state);
}

static void pwrseq_loop_thread(void *p1, void *p2, void *p3)
{
	enum power_states_ndsx curr_state, new_state;
	power_signal_mask_t this_in_signals;
	power_signal_mask_t last_in_signals = 0;
	enum power_states_ndsx last_state = -1;

	/*
	 * Let clients know that the AP power state is now
	 * initialized and ready.
	 */
	ap_power_ev_send_callbacks(AP_POWER_INITIALIZED);
	while (1) {
		curr_state = pwr_sm_get_state();

		/*
		 * In order to prevent repeated console spam, only print the
		 * current power state if something has actually changed.  It's
		 * possible that one of the power signals goes away briefly and
		 * comes back by the time we update our signals.
		 */
		this_in_signals = power_get_signals();

		if (this_in_signals != last_in_signals ||
		    curr_state != last_state) {
			LOG_INF("power state %d = %s, in 0x%04x", curr_state,
				pwr_sm_get_state_name(curr_state),
				this_in_signals);
			last_in_signals = this_in_signals;
			last_state = curr_state;
		}

		/* Run chipset specific state machine */
		new_state = chipset_pwr_sm_run(curr_state);

		/*
		 * Run common power state machine
		 * if the state has changed in chipset state
		 * machine then skip running common state
		 * machine
		 */
		if (curr_state == new_state)
			new_state = common_pwr_sm_run(curr_state);

		if (curr_state != new_state) {
			pwr_sm_set_state(new_state);
			ap_power_set_active_wake_mask();
		} else {
			/*
			 * No state transition, we can go to sleep and wait
			 * for any event to wake us up.
			 */
			k_sem_take(&pwrseq_sem, K_FOREVER);
		}
	}
}

static inline void create_pwrseq_thread(void)
{
	k_thread_create(&pwrseq_thread_data, pwrseq_thread_stack,
			K_KERNEL_STACK_SIZEOF(pwrseq_thread_stack),
			(k_thread_entry_t)pwrseq_loop_thread, NULL, NULL, NULL,
			CONFIG_AP_PWRSEQ_THREAD_PRIORITY, 0,
			IS_ENABLED(CONFIG_AP_PWRSEQ_AUTOSTART) ? K_NO_WAIT :
								 K_FOREVER);

	k_thread_name_set(&pwrseq_thread_data, "pwrseq_task");
}

void ap_pwrseq_task_start(void)
{
	if (!IS_ENABLED(CONFIG_AP_PWRSEQ_AUTOSTART)) {
		k_thread_start(&pwrseq_thread_data);
	}
}

static void init_pwr_seq_state(void)
{
	atomic_clear_bit(flags, START_FROM_G3);
	/*
	 * The state of the CPU needs to be determined now
	 * so that init routines can check the state of
	 * the CPU.
	 */
	pwr_seq_set_initial_state();
}

/* Initialize power sequence system state */
static int pwrseq_init(void)
{
	LOG_INF("Pwrseq Init");

	k_sem_init(&pwrseq_sem, 0, 1);
	/* Initialize signal handlers */
	power_signal_init();
	LOG_DBG("Init pwr seq state");
	init_pwr_seq_state();
	/* Create power sequence state handler core function thread */
	create_pwrseq_thread();
	return 0;
}

/*
 * The initialization must occur after system I/O initialization that
 * the signals depend upon, such as GPIO, ADC etc.
 */
SYS_INIT(pwrseq_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#else
static int x86_non_dsx_g3_entry(void *data)
{
	if (!atomic_test_bit(flags, START_FROM_G3)) {
		return 0;
	}

	if (start_from_g3_delay_ms) {
		k_timer_start(&x86_non_dsx_timer,
			      K_MSEC(start_from_g3_delay_ms), K_NO_WAIT);
		start_from_g3_delay_ms = 0;
	} else {
		ap_pwrseq_post_event(ap_pwrseq_get_instance(),
				     AP_PWRSEQ_EVENT_POWER_STARTUP);
	}

	return 0;
}

static int x86_non_dsx_g3_run(void *data)
{
	/*
	 * If the START_FROM_G3 flag is set, begin starting
	 * the AP. There may be a delay set, so only start
	 * after that delay.
	 */
	if (!atomic_test_bit(flags, START_FROM_G3)) {
		return 0;
	}

	if (k_timer_remaining_get(&x86_non_dsx_timer)) {
		return 0;
	}
	/*
	 * At this point all power rails and power signals are already checked
	 * by application and chipset state action handlers, it is safe to move
	 * forward to S5.
	 */
	return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S5);
}

static int x86_non_dsx_g3_exit(void *data)
{
	atomic_clear_bit(flags, START_FROM_G3);

	return 0;
}

AP_POWER_ARCH_STATE_DEFINE(AP_POWER_STATE_G3, x86_non_dsx_g3_entry,
			   x86_non_dsx_g3_run, x86_non_dsx_g3_exit);

static int x86_non_dsx_s5_entry(void *data)
{
	if (AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout)) {
		atomic_set_bit(flags, S5_INACTIVE_TIMER_RUNNING);
		k_timer_start(
			&x86_non_dsx_timer,
			K_SECONDS(AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout)),
			K_NO_WAIT);
	}

	return 0;
}

static int x86_non_dsx_s5_run(void *data)
{
	/*
	 * At this point, lower level action handlers of state machine should
	 * have already checked that required power rails are OK.
	 */
	rsmrst_pass_thru_handler();
	if (!power_signal_get(PWR_EC_PCH_RSMRST)) {
		if (signals_valid_and_off(IN_PCH_SLP_S5)) {
			return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S4);
		}
	}
	/* S5 inactivity timeout, go to G3 */
	if (AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout) == 0) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
	} else if (k_timer_remaining_get(&x86_non_dsx_timer) == 0) {
		/* Timer is expired */
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
	}

	return 0;
}

static int x86_non_dsx_s5_exit(void *data)
{
	if (atomic_test_bit(flags, S5_INACTIVE_TIMER_RUNNING)) {
		k_timer_stop(&x86_non_dsx_timer);
		atomic_clear_bit(flags, S5_INACTIVE_TIMER_RUNNING);
	}

	return 0;
}

AP_POWER_ARCH_STATE_DEFINE(AP_POWER_STATE_S5, x86_non_dsx_s5_entry,
			   x86_non_dsx_s5_run, x86_non_dsx_s5_exit);

static int x86_non_dsx_s4_run(void *data)
{
	if (power_signal_get(PWR_RSMRST_PWRGD) == 0 ||
	    signals_valid_and_on(IN_PCH_SLP_S5)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S5);
	}

	if (signals_valid_and_off(IN_PCH_SLP_S4)) {
#if CONFIG_AP_PWRSEQ_S0IX
		/*
		 * Clearing the S0ix flag on the path to S0
		 * to handle any reset conditions.
		 */
		ap_power_reset_host_sleep_state();
#endif
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S3);
	}

	return 0;
}

AP_POWER_ARCH_STATE_DEFINE(AP_POWER_STATE_S4, NULL, x86_non_dsx_s4_run, NULL);

static int x86_non_dsx_s3_run(void *data)
{
	if (power_signal_get(PWR_RSMRST_PWRGD) == 0 ||
	    signals_valid_and_on(IN_PCH_SLP_S4)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S4);
	}

	if (signals_valid_and_on(IN_PCH_SLP_S3)) {
		return 0;
	}

	/* All the power rails must be stable */
	if (power_signal_get(PWR_ALL_SYS_PWRGD)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S0);
	}

	return 0;
}

AP_POWER_ARCH_STATE_DEFINE(AP_POWER_STATE_S3, NULL, x86_non_dsx_s3_run, NULL);

static int x86_non_dsx_s0_run(void *data)
{
	if (signals_valid_and_on(IN_PCH_SLP_S3)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S3);
	}
#if CONFIG_AP_PWRSEQ_S0IX
	if (ap_power_sleep_get_notify() == AP_POWER_SLEEP_SUSPEND &&
	    power_signals_on(IN_PCH_SLP_S0)) {
		return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S0IX);
	} else if (ap_power_sleep_get_notify() == AP_POWER_SLEEP_RESUME) {
		ap_power_sleep_notify_transition(AP_POWER_SLEEP_RESUME);
	}
#endif

	return 0;
}

AP_POWER_ARCH_STATE_DEFINE(AP_POWER_STATE_S0, NULL, x86_non_dsx_s0_run, NULL);
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

#ifdef CONFIG_AP_PWRSEQ_DEBUG_MODE_COMMAND
/*
 * Intel debugger puts SOC in boot halt mode for step debugging,
 * during this time EC may lose Sx lines, Adding this console
 * command to avoid force shutdown.
 */
static int disable_force_shutdown(int argc, const char **argv)
{
	if (argc > 1) {
		if (!strcmp(argv[1], "enable")) {
			in_debug_mode = true;
		} else if (!strcmp(argv[1], "disable")) {
			in_debug_mode = false;
		} else {
			return EC_ERROR_PARAM1;
		}
	}
	LOG_INF("debug_mode = %s", (in_debug_mode ? "enabled" : "disabled"));

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(debug_mode, disable_force_shutdown, "[enable|disable]",
			"Prevents force shutdown if enabled");
#endif /* CONFIG_AP_PWRSEQ_DEBUG_MODE_COMMAND */
