/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <init.h>

#include <x86_non_dsx_common_pwrseq_sm_handler.h>

static K_KERNEL_STACK_DEFINE(pwrseq_thread_stack,
			CONFIG_AP_PWRSEQ_STACK_SIZE);
static struct k_thread pwrseq_thread_data;
static struct pwrseq_context pwrseq_ctx;
/* S5 inactive timer*/
K_TIMER_DEFINE(s5_inactive_timer, NULL, NULL);

LOG_MODULE_REGISTER(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

/**
 * @brief power_state names for debug
 */
const char pwrsm_dbg[][25] = {
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

/*
 * Returns true if all signals in mask are valid.
 * This is only done for virtual wire signals.
 */
static inline bool signals_valid(power_signal_mask_t signals)
{
#if defined(CONFIG_PLATFORM_EC_ESPI_VW_SLP_S3)
	if ((signals & POWER_SIGNAL_MASK(PWR_SLP_S3)) &&
	    power_signal_get(PWR_SLP_S3) < 0)
		return false;
#endif
#if defined(CONFIG_PLATFORM_EC_ESPI_VW_SLP_S4)
	if ((signals & POWER_SIGNAL_MASK(PWR_SLP_S4)) &&
	    power_signal_get(PWR_SLP_S4) < 0)
		return false;
#endif
#if defined(CONFIG_PLATFORM_EC_ESPI_VW_SLP_S5)
	if ((signals & POWER_SIGNAL_MASK(PWR_SLP_S5)) &&
	    power_signal_get(PWR_SLP_S4) < 0)
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

static int check_power_rails_enabled(void)
{
	int out = 1;

	out &= power_signal_get(PWR_EN_PP3300_A);
	out &= power_signal_get(PWR_EN_PP5000_A);
	out &= power_signal_get(PWR_EC_SOC_DSW_PWROK);
	return out;
}

enum power_states_ndsx pwr_sm_get_state(void)
{
	return pwrseq_ctx.power_state;
}

void pwr_sm_set_state(enum power_states_ndsx new_state)
{
	/* Add locking mechanism if multiple thread can update it */
	LOG_DBG("Power state: %s --> %s", pwrsm_dbg[pwrseq_ctx.power_state],
					pwrsm_dbg[new_state]);
	pwrseq_ctx.power_state = new_state;
}

void request_exit_hardoff(bool should_exit)
{
	pwrseq_ctx.want_g3_exit = should_exit;
}

static bool chipset_is_exit_hardoff(void)
{
	return pwrseq_ctx.want_g3_exit;
}

void apshutdown(void)
{
	if (pwr_sm_get_state() != SYS_POWER_STATE_G3) {
		ap_power_force_shutdown(AP_POWER_SHUTDOWN_G3);
		pwr_sm_set_state(SYS_POWER_STATE_G3);
	}
}

/* Check RSMRST is fine to move from S5 to higher state */
int rsmrst_power_is_good(void)
{
	/* TODO: Check if this is still intact */
	return power_signal_get(PWR_RSMRST);
}

int check_pch_out_of_suspend(void)
{
	int ret;

	/*
	 * Wait for SLP_SUS deasserted.
	 */
	ret = power_wait_mask_signals_timeout(IN_PCH_SLP_SUS,
					      0,
					      IN_PCH_SLP_SUS_WAIT_TIME_MS);

	if (ret == 0) {
		LOG_DBG("SLP_SUS now %d", power_signal_get(PWR_SLP_SUS));
		return 1;
	}
	LOG_ERR("wait SLP_SUS deassertion timeout");
	return 0; /* timeout */
}

/* Handling RSMRST signal is mostly common across x86 chipsets */
void rsmrst_pass_thru_handler(void)
{
	/* Handle RSMRST passthrough */
	/* TODO: Add additional conditions for RSMRST handling */
	int in_sig_val = power_signal_get(PWR_RSMRST);
	int out_sig_val = power_signal_get(PWR_EC_PCH_RSMRST);

	if (in_sig_val != out_sig_val) {
		if (in_sig_val)
			k_msleep(AP_PWRSEQ_DT_VALUE(rsmrst_delay));
		LOG_DBG("Setting PWR_EC_PCH_RSMRST to %d", in_sig_val);
		power_signal_set(PWR_EC_PCH_RSMRST, in_sig_val);
	}
}

/* TODO:
 * Add power down sequence
 * Add S0ix
 */
static int common_pwr_sm_run(int state)
{
	switch (state) {
	case SYS_POWER_STATE_G3:
		if (chipset_is_exit_hardoff()) {
			request_exit_hardoff(false);
			return SYS_POWER_STATE_G3S5;
		}

		break;

	case SYS_POWER_STATE_G3S5:
		if (power_wait_signals_timeout(
			IN_PGOOD_ALL_CORE,
			AP_PWRSEQ_DT_VALUE(wait_signal_timeout)))
			break;
		/*
		 * Now wait for SLP_SUS_L to go high based on tPCH32. If this
		 * signal doesn't go high within 250 msec then go back to G3.
		 */
		if (check_pch_out_of_suspend()) {
			ap_power_ev_send_callbacks(AP_POWER_PRE_INIT);
			return SYS_POWER_STATE_S5;
		}
		return SYS_POWER_STATE_S5G3;

	case SYS_POWER_STATE_S5:
		/* In S5 make sure no more signal lost */
		/* If A-rails are stable then move to higher state */
		if (check_power_rails_enabled() && rsmrst_power_is_good()) {
			/* rsmrst is intact */
			rsmrst_pass_thru_handler();
			if (power_signals_on(IN_PCH_SLP_SUS)) {
				k_timer_stop(&s5_inactive_timer);
				return SYS_POWER_STATE_S5G3;
			}
			if (signals_valid_and_off(IN_PCH_SLP_S5)) {
				k_timer_stop(&s5_inactive_timer);
				return SYS_POWER_STATE_S5S4;
			}
		}
		/* S5 inactivity timeout, go to S5G3 */
		if (AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout) == 0)
			return SYS_POWER_STATE_S5G3;
		else if (AP_PWRSEQ_DT_VALUE(s5_inactivity_timeout) > 0) {
			if (k_timer_status_get(&s5_inactive_timer) > 0)
				/* Timer is expired */
				return SYS_POWER_STATE_S5G3;
			else if (k_timer_remaining_get(
						&s5_inactive_timer) == 0)
				/* Timer is not started or stopped */
				k_timer_start(&s5_inactive_timer,
					K_SECONDS(AP_PWRSEQ_DT_VALUE(
						s5_inactivity_timeout)),
					K_NO_WAIT);
		}
		break;

	case SYS_POWER_STATE_S5G3:
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
		if (signals_valid_and_on(IN_PCH_SLP_S5))
			return SYS_POWER_STATE_S4S5;
		else if (signals_valid_and_off(IN_PCH_SLP_S4))
			return SYS_POWER_STATE_S4S3;

		break;

	case SYS_POWER_STATE_S4S3:
		if (!power_signals_on(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away */
			ap_power_force_shutdown(AP_POWER_SHUTDOWN_POWERFAIL);
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
		if (!power_signals_on(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away, go straight to S5 */
			ap_power_force_shutdown(AP_POWER_SHUTDOWN_POWERFAIL);
			return SYS_POWER_STATE_G3;
		} else if (signals_valid_and_off(IN_PCH_SLP_S3))
			return SYS_POWER_STATE_S3S0;
		else if (signals_valid_and_on(IN_PCH_SLP_S4))
			return SYS_POWER_STATE_S3S4;

		break;

	case SYS_POWER_STATE_S3S0:
		if (!power_signals_on(IN_PGOOD_ALL_CORE)) {
			ap_power_force_shutdown(AP_POWER_SHUTDOWN_POWERFAIL);
			return SYS_POWER_STATE_G3;
		}

		/* All the power rails must be stable */
		if (power_signal_get(PWR_ALL_SYS_PWRGD)) {
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
		} else if (!power_signals_on(IN_PGOOD_ALL_CORE))
			return SYS_POWER_STATE_S0;

		break;

	case SYS_POWER_STATE_S0S0ix:
		/*
		 * Check sleep state and notify listeners of S0ix suspend if
		 * HC already set sleep suspend state.
		 */
		ap_power_sleep_notify_transition(AP_POWER_SLEEP_SUSPEND);

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
		if (power_get_host_sleep_state() !=
			HOST_SLEEP_EVENT_S0IX_RESUME)
			break;

		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
		ap_power_ev_send_callbacks(AP_POWER_RESUME_INIT);
#endif

		return SYS_POWER_STATE_S0;

#endif /* CONFIG_AP_PWRSEQ_S0IX */

	case SYS_POWER_STATE_S0:
		if (!power_signals_on(IN_PGOOD_ALL_CORE)) {
			ap_power_force_shutdown(AP_POWER_SHUTDOWN_POWERFAIL);
			return SYS_POWER_STATE_G3;
		} else if (signals_valid_and_on(IN_PCH_SLP_S3)) {
			return SYS_POWER_STATE_S0S3;

#if CONFIG_AP_PWRSEQ_S0IX
		/*
		 * SLP_S0 may assert in system idle scenario without a kernel
		 * freeze call. This may cause interrupt storm since there is
		 * no freeze/unfreeze of threads/process in the idle scenario.
		 * Ignore the SLP_S0 assertions in idle scenario by checking
		 * the host sleep state.
		 */
		} else if (power_get_host_sleep_state()
					== HOST_SLEEP_EVENT_S0IX_SUSPEND &&
				power_signals_on(IN_PCH_SLP_S0)) {

			return SYS_POWER_STATE_S0S0ix;
		} else {
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

		/* Nofity power event after we remove power rails */
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

static void pwrseq_loop_thread(void *p1, void *p2, void *p3)
{
	int32_t t_wait_ms = 10;
	enum power_states_ndsx curr_state, new_state;
	power_signal_mask_t this_in_signals;
	power_signal_mask_t last_in_signals = 0;
	enum power_states_ndsx last_state = pwr_sm_get_state();

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
			LOG_INF("power state %d = %s, in 0x%04x",
				curr_state, pwrsm_dbg[curr_state],
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
		}

		k_msleep(t_wait_ms);
	}
}

static inline void create_pwrseq_thread(void)
{
	k_thread_create(&pwrseq_thread_data,
			pwrseq_thread_stack,
			K_KERNEL_STACK_SIZEOF(pwrseq_thread_stack),
			(k_thread_entry_t)pwrseq_loop_thread,
			NULL, NULL, NULL,
			K_PRIO_COOP(8), 0,
			IS_ENABLED(CONFIG_AP_PWRSEQ_AUTOSTART) ? K_NO_WAIT
							       : K_FOREVER);

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
	init_chipset_pwr_seq_state();
	request_exit_hardoff(false);

	pwr_sm_set_state(SYS_POWER_STATE_G3S5);
}

/* Initialize power sequence system state */
static int pwrseq_init(const struct device *dev)
{
	LOG_INF("Pwrseq Init");

	/* Initialize signal handlers */
	power_signal_init();
	/* TODO: Define initial state of power sequence */
	LOG_DBG("Init pwr seq state");
	init_pwr_seq_state();
	/* Create power sequence state handler core function thread */
	create_pwrseq_thread();
	return 0;
}

/*
 * The initialisation must occur after system I/O initialisation that
 * the signals depend upon, such as GPIO, ADC etc.
 */
SYS_INIT(pwrseq_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
