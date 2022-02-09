/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <x86_non_dsx_common_pwrseq_sm_handler.h>

static K_KERNEL_STACK_DEFINE(pwrseq_thread_stack,
			CONFIG_X86_NON_DSW_PWRSEQ_STACK_SIZE);
static struct k_thread pwrseq_thread_data;
static struct pwrseq_context pwrseq_ctx;
/* S5 inactive timer*/
K_TIMER_DEFINE(s5_inactive_timer, NULL, NULL);

LOG_MODULE_REGISTER(ap_pwrseq, 4);

static const struct common_pwrseq_config com_cfg = {
	.pch_dsw_pwrok_delay_ms = DT_INST_PROP(0, dsw_pwrok_delay),
	.pch_pm_pwrbtn_delay_ms =  DT_INST_PROP(0, pm_pwrbtn_delay),
	.pch_rsmrst_delay_ms = DT_INST_PROP(0, rsmrst_delay),
	.wait_signal_timeout_ms = DT_INST_PROP(0, wait_signal_timeout),
	.s5_timeout_s = DT_INST_PROP(0, s5_inactivity_timeout),
#if PWRSEQ_GPIO_PRESENT(en_pp5000_s5_gpios)
	.enable_pp5000_a = GPIO_DT_SPEC_GET(DT_DRV_INST(0),
						en_pp5000_s5_gpios),
#endif
#if PWRSEQ_GPIO_PRESENT(en_pp3300_s5_gpios)
	.enable_pp3300_a = GPIO_DT_SPEC_GET(DT_DRV_INST(0),
						en_pp3300_s5_gpios),
#endif
	.pg_ec_rsmrst_odl = GPIO_DT_SPEC_GET(DT_DRV_INST(0),
						pg_ec_rsmrst_odl_gpios),
	.ec_pch_rsmrst_odl = GPIO_DT_SPEC_GET(DT_DRV_INST(0),
						ec_pch_rsmrst_odl_gpios),
#if PWRSEQ_GPIO_PRESENT(pg_ec_dsw_pwrok_gpios)
	.pg_ec_dsw_pwrok = GPIO_DT_SPEC_GET(DT_DRV_INST(0),
						pg_ec_dsw_pwrok_gpios),
#endif
#if PWRSEQ_GPIO_PRESENT(ec_soc_dsw_pwrok_gpios)
	.ec_soc_dsw_pwrok = GPIO_DT_SPEC_GET(DT_DRV_INST(0),
						ec_soc_dsw_pwrok_gpios),
#endif
	.slp_s3_l = GPIO_DT_SPEC_GET(DT_DRV_INST(0), slp_s3_l_gpios),
	.all_sys_pwrgd = GPIO_DT_SPEC_GET(DT_DRV_INST(0),
						pg_ec_all_sys_pwrgd_gpios),
	.slp_sus_l = GPIO_DT_SPEC_GET(DT_DRV_INST(0), slp_sus_l_gpios),
};

#ifdef CONFIG_LOG
/**
 * @brief power_state names for debug
 */
const char pwrsm_dbg[][25] = {
	[SYS_POWER_STATE_G3] = "STATE_G3",
	[SYS_POWER_STATE_S5] = "STATE_S5",
	[SYS_POWER_STATE_S4] = "STATE_S4",
	[SYS_POWER_STATE_S3] = "STATE_S3",
	[SYS_POWER_STATE_S0] = "STATE_S0",
	[SYS_POWER_STATE_G3S5] = "STATE_G3S5",
	[SYS_POWER_STATE_S5S4] = "STATE_S5S4",
	[SYS_POWER_STATE_S4S3] = "STATE_S4S3",
	[SYS_POWER_STATE_S3S0] = "STATE_S3S0",
	[SYS_POWER_STATE_S5G3] = "STATE_S5G3",
	[SYS_POWER_STATE_S4S5] = "STATE_S4S5",
	[SYS_POWER_STATE_S3S4] = "STATE_S3S4",
	[SYS_POWER_STATE_S0S3] = "STATE_S0S3",
};
#endif

int power_signal_disable_interrupt(enum power_signal signal)
{
	int index = power_signal_list[signal].source_id;

	return gpio_pin_interrupt_configure_dt(
			&power_signal_gpio_list[index].spec,
			GPIO_INT_DISABLE);
}

int power_signal_enable_interrupt(enum power_signal signal)
{
	int index = power_signal_list[signal].source_id;

	return gpio_pin_interrupt_configure_dt(
			&power_signal_gpio_list[index].spec,
			power_signal_gpio_list[index].intr_flags);
}

int power_wait_mask_signals_timeout(uint32_t want, uint32_t mask, int timeout)
{
	int time_left = timeout;

	pwrseq_ctx.in_want = want;
	if (!mask)
		return 0;

	while (time_left--) {
		if ((pwrseq_ctx.in_signals & mask) != pwrseq_ctx.in_want)
			k_msleep(1);
		else
			return 0;
	}

	power_update_signals();
	return -ETIMEDOUT;
}

int power_wait_signals_timeout(uint32_t want, int timeout)
{
	return power_wait_mask_signals_timeout(want, want, timeout);
}

int power_wait_signals(uint32_t want)
{
	int ret = power_wait_signals_timeout(want,
				com_cfg.wait_signal_timeout_ms);

	if (ret == -ETIMEDOUT)
		LOG_INF("power timeout on input; wanted 0x%04x, got 0x%04x",
			want, pwrseq_ctx.in_signals & want);
	return ret;
}

__attribute__((weak)) int board_power_signal_is_asserted(
	enum power_signal signal)
{
	return 0;
}

/* TODO: b/220634934 use GPIO logical level */
int power_signal_is_asserted(enum power_signal signal)
{
	int flags = power_signal_list[signal].flags;
	int id = power_signal_list[signal].source_id;

	if (power_signal_list[signal].source == SOURCE_GPIO)
		/* GPIO generated signal */
		return gpio_pin_get_dt(
			&power_signal_gpio_list[id].spec) ==
				!!(flags & POWER_SIGNAL_ACTIVE_STATE);
	else if (power_signal_list[signal].source == SOURCE_VW)
		/* ESPI generated signal */
		return vw_get_level(id) ==
				!!(flags & POWER_SIGNAL_ACTIVE_STATE);
	else /* TODO: handle SOURCE_ADC */
		return board_power_signal_is_asserted(signal);
}

/**
 * Update input signals mask
 */
void power_update_signals(void)
{
	uint32_t inew = 0;
	int i;

	for (i = 0; i < POWER_SIGNAL_COUNT; i++) {
		if (power_signal_is_asserted(i))
			inew |= BIT(i);
	}

	if ((pwrseq_ctx.in_signals & pwrseq_ctx.in_debug) !=
				(inew & pwrseq_ctx.in_debug))
		LOG_INF("power update 0x%04x->0x%04x",
					pwrseq_ctx.in_signals, inew);
	pwrseq_ctx.in_signals = inew;
}

uint32_t power_get_signals(void)
{
	return pwrseq_ctx.in_signals;
}

bool power_has_signals(uint32_t want)
{
	if ((pwrseq_ctx.in_signals & want) == want)
		return true;

	return false;
}

void power_signal_interrupt(const struct device *gpiodev,
			struct gpio_callback *cb,
			uint32_t pin)
{
	int index = cb - &intr_callbacks[0];

	if (power_signal_gpio_list[index].power_signal
			>= POWER_SIGNAL_COUNT)
		return;

	/* TODO: Monitor interrupt storm */

	power_update_signals();
}

static int check_power_rails_enabled(void)
{
	int out = 1;

#if PWRSEQ_GPIO_PRESENT(en_pp3300_s5_gpios)
	out &= gpio_pin_get_dt(&com_cfg.enable_pp3300_a);
#endif
#if PWRSEQ_GPIO_PRESENT(en_pp5000_s5_gpios)
	out &= gpio_pin_get_dt(&com_cfg.enable_pp5000_a);
#endif
#if PWRSEQ_GPIO_PRESENT(pg_ec_dsw_pwrok_gpios)
	out &= gpio_pin_get_dt(&com_cfg.pg_ec_dsw_pwrok);
#endif
	return out;
}

static void pwrseq_gpio_init(void)
{
	int ret = 0;
	int i;

	/* Configure the GPIO */
#if PWRSEQ_GPIO_PRESENT(en_pp5000_s5_gpios)
	ret = gpio_pin_configure_dt(&com_cfg.enable_pp5000_a,
						GPIO_OUTPUT_LOW);
#endif
#if PWRSEQ_GPIO_PRESENT(en_pp3300_s5_gpios)
	ret |= gpio_pin_configure_dt(&com_cfg.enable_pp3300_a,
						GPIO_OUTPUT_LOW);
#endif
	ret |= gpio_pin_configure_dt(&com_cfg.pg_ec_rsmrst_odl,
						GPIO_INPUT);
	ret |= gpio_pin_configure_dt(&com_cfg.ec_pch_rsmrst_odl,
						GPIO_OUTPUT_LOW);
#if PWRSEQ_GPIO_PRESENT(pg_ec_dsw_pwrok_gpios)
	ret |= gpio_pin_configure_dt(&com_cfg.pg_ec_dsw_pwrok,
						GPIO_INPUT);
#endif
#if PWRSEQ_GPIO_PRESENT(ec_soc_dsw_pwrok_gpios)
	ret |= gpio_pin_configure_dt(&com_cfg.ec_soc_dsw_pwrok,
						GPIO_OUTPUT_LOW);
#endif
	ret |= gpio_pin_configure_dt(&com_cfg.slp_s3_l, GPIO_INPUT);
	ret |= gpio_pin_configure_dt(&com_cfg.slp_sus_l, GPIO_INPUT);
	ret |= gpio_pin_configure_dt(&com_cfg.all_sys_pwrgd, GPIO_INPUT);

	if (!ret)
		LOG_INF("Configuring GPIO complete");
	else
		LOG_ERR("GPIO configure failed\n");

	/* Initialize GPIO interrupts */
	for (i = 0; i < POWER_SIGNAL_GPIO_COUNT; i++) {
		const struct gpio_power_signal_config *int_config = NULL;

		int_config = &power_signal_gpio_list[i];

		/* Configure interrupt */
		gpio_init_callback(&intr_callbacks[i],
					power_signal_interrupt,
					BIT(int_config->spec.pin));
		ret = gpio_add_callback(int_config->spec.port,
			&intr_callbacks[i]);

		if (!ret) {
			if (int_config->enable_on_boot)
				gpio_pin_interrupt_configure_dt(
					&int_config->spec,
					int_config->intr_flags);
		} else {
			LOG_ERR("Fail to config interrupt %s, ret=%d",
				power_signal_list[i].debug_name, ret);
		}
	}

	/*
	 * Update input state again since there is a small window
	 * before GPIO is enabled.
	 */
	power_update_signals();

}

enum power_states_ndsx pwr_sm_get_state(void)
{
	return pwrseq_ctx.power_state;
}

void pwr_sm_set_state(enum power_states_ndsx new_state)
{
	/* Add locking mechanism if multiple thread can update it */
	LOG_DBG("Power state: %s --> %s\n", pwrsm_dbg[pwrseq_ctx.power_state],
					pwrsm_dbg[new_state]);
	pwrseq_ctx.power_state = new_state;
}

uint32_t pwrseq_get_input_signals(void)
{
	return pwrseq_ctx.in_signals;
}

void pwrseq_set_debug_signals(uint32_t signals)
{
	pwrseq_ctx.in_debug = signals;
}

uint32_t pwrseq_get_debug_signals(void)
{
	return pwrseq_ctx.in_debug;
}

void apshutdown(void)
{
	if (pwr_sm_get_state() != SYS_POWER_STATE_G3) {
		chipset_force_shutdown(PWRSEQ_CHIPSET_SHUTDOWN_CONSOLE_CMD,
								&com_cfg);
		pwr_sm_set_state(SYS_POWER_STATE_G3);
	}
}

/* Check RSMRST is fine to move from S5 to higher state */
int check_rsmrst_ok(void)
{
	/* TODO: Check if this is still intact*/
	return gpio_pin_get_dt(&com_cfg.pg_ec_rsmrst_odl);
}

int check_pch_out_of_suspend(void)
{
	int ret;

	ret = power_wait_signals_timeout(
		IN_PCH_SLP_SUS_DEASSERTED, IN_PCH_SLP_SUS_WAIT_TIME_MS);

	if (ret == 0)
		return 1;
	return 0; /* timeout */
}

/* Handling RSMRST signal is mostly common across x86 chipsets */
__attribute__((weak)) void rsmrst_pass_thru_handler(void)
{
	/* Handle RSMRST passthrough */
	/* TODO: Add additional conditions for RSMRST handling */
	int in_sig_val = gpio_pin_get_dt(&com_cfg.pg_ec_rsmrst_odl);
	int out_sig_val = gpio_pin_get_dt(&com_cfg.ec_pch_rsmrst_odl);

	if (in_sig_val != out_sig_val) {
		if (in_sig_val)
			k_msleep(com_cfg.pch_rsmrst_delay_ms);
		gpio_pin_set_dt(&com_cfg.ec_pch_rsmrst_odl, in_sig_val);
	}
}

/* TODO:
 * Add power down sequence
 * Add logic to suspend and resume the thread
 */
static int common_pwr_sm_run(int state)
{
	switch (state) {
	case SYS_POWER_STATE_G3:
		/* Nothing to do */
		break;

	case SYS_POWER_STATE_G3S5:
		if (power_wait_signals(IN_PGOOD_ALL_CORE))
			break;
		/*
		 * Now wait for SLP_SUS_L to go high based on tPCH32. If this
		 * signal doesn't go high within 250 msec then go back to G3.
		 */
		if (check_pch_out_of_suspend())
			return SYS_POWER_STATE_S5;
		return SYS_POWER_STATE_S5G3;

	case SYS_POWER_STATE_S5:
		/* In S5 make sure no more signal lost */
		/* If A-rails are stable then move to higher state */
		if (check_power_rails_enabled() && check_rsmrst_ok()) {
			/* rsmrst is intact */
			rsmrst_pass_thru_handler();
			if (!power_has_signals(IN_PCH_SLP_SUS_DEASSERTED)) {
				k_timer_stop(&s5_inactive_timer);
				return SYS_POWER_STATE_S5G3;
			}
			if (power_has_signals(IN_PCH_SLP_S5_DEASSERTED)) {
				k_timer_stop(&s5_inactive_timer);
				return SYS_POWER_STATE_S5S4;
			}
		}
		/* S5 inactivity timeout, go to S5G3 */
		if (com_cfg.s5_timeout_s == 0)
			return SYS_POWER_STATE_S5G3;
		else if (com_cfg.s5_timeout_s > 0) {
			if (k_timer_status_get(&s5_inactive_timer) > 0)
				/* Timer is expired */
				return SYS_POWER_STATE_S5G3;
			else if (k_timer_remaining_get(
						&s5_inactive_timer) == 0)
				/* Timer is not started or stopped */
				k_timer_start(&s5_inactive_timer,
					K_SECONDS(com_cfg.s5_timeout_s),
					K_NO_WAIT);
		}
		break;

	case SYS_POWER_STATE_S5G3:
		chipset_force_shutdown(PWRSEQ_CHIPSET_SHUTDOWN_G3, &com_cfg);
		return SYS_POWER_STATE_G3;

	case SYS_POWER_STATE_S5S4:
		/* Check if the PCH has come out of suspend state */
		if (check_rsmrst_ok()) {
			LOG_DBG("RSMRST is ok");
			return SYS_POWER_STATE_S4;
		}
		LOG_DBG("RSMRST is not ok");
		return SYS_POWER_STATE_S5;

	case SYS_POWER_STATE_S4:
		if (!power_has_signals(IN_PCH_SLP_S5_DEASSERTED))
			return SYS_POWER_STATE_S4S5;
		else if (power_has_signals(IN_PCH_SLP_S4_DEASSERTED))
			return SYS_POWER_STATE_S4S3;

		break;

	case SYS_POWER_STATE_S4S3:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away */
			chipset_force_shutdown(
				PWRSEQ_CHIPSET_SHUTDOWN_POWERFAIL, &com_cfg);
			return SYS_POWER_STATE_G3;
		}

		/* Call hooks now that rails are up */
		/* TODO: hook_notify(HOOK_CHIPSET_STARTUP); */
		/* TODO: S0ix
		 * Clearing the S0ix flag on the path to S0
		 * to handle any reset conditions.
		 */

		return SYS_POWER_STATE_S3;

	case SYS_POWER_STATE_S3:
		/* AP is out of suspend to RAM */
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away, go straight to S5 */
			chipset_force_shutdown(
				PWRSEQ_CHIPSET_SHUTDOWN_POWERFAIL, &com_cfg);
			return SYS_POWER_STATE_G3;
		} else if (power_has_signals(IN_PCH_SLP_S3_DEASSERTED))
			return SYS_POWER_STATE_S3S0;
		else if (!power_has_signals(IN_PCH_SLP_S4_DEASSERTED))
			return SYS_POWER_STATE_S3S4;

		break;

	case SYS_POWER_STATE_S3S0:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			chipset_force_shutdown(
				PWRSEQ_CHIPSET_SHUTDOWN_POWERFAIL, &com_cfg);
			return SYS_POWER_STATE_G3;
		}

		/* All the power rails must be stable */
		if (gpio_pin_get_dt(&com_cfg.all_sys_pwrgd))
			return SYS_POWER_STATE_S0;
		break;

	case SYS_POWER_STATE_S0:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			chipset_force_shutdown(
				PWRSEQ_CHIPSET_SHUTDOWN_POWERFAIL, &com_cfg);
			return SYS_POWER_STATE_G3;
		} else if (!power_has_signals(IN_PCH_SLP_S3_DEASSERTED))
			return SYS_POWER_STATE_S0S3;
		/* TODO: S0ix */

		break;

	case SYS_POWER_STATE_S4S5:
		/* TODO */
		/* Call hooks before we remove power rails */
		/* hook_notify(HOOK_CHIPSET_SHUTDOWN); */
		/* Disable wireless */
		/* wireless_set_state(WIRELESS_OFF); */
		/* Call hooks after we remove power rails */
		/* hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE); */
		/* Always enter into S5 state. The S5 state is required to
		 * correctly handle global resets which have a bit of delay
		 * while the SLP_Sx_L signals are asserted then deasserted.
		 */
		return SYS_POWER_STATE_S5;

	case SYS_POWER_STATE_S3S4:
		return SYS_POWER_STATE_S4;

	case SYS_POWER_STATE_S0S3:
		/* TODO: Call hooks before we remove power rails */
		/* hook_notify(HOOK_CHIPSET_SUSPEND); */
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
	uint32_t this_in_signals;
	static uint32_t last_in_signals;
	static enum power_states_ndsx last_state;

	while (1) {
		curr_state = pwr_sm_get_state();

		/*
		 * In order to prevent repeated console spam, only print the
		 * current power state if something has actually changed.  It's
		 * possible that one of the power signals goes away briefly and
		 * comes back by the time we update our pwrseq_ctx.in_signals.
		 */
		this_in_signals = pwrseq_ctx.in_signals;
		if (this_in_signals != last_in_signals ||
				curr_state != last_state) {
			LOG_INF("power state %d = %s, in 0x%04x",
				curr_state, pwrsm_dbg[curr_state],
				this_in_signals);
			last_in_signals = this_in_signals;
			last_state = curr_state;
		}

		/* Run chipset specific state machine */
		new_state = chipset_pwr_sm_run(curr_state, &com_cfg);

		/*
		 * Run common power state machine
		 * if the state has changed in chipset state
		 * machine then skip running common state
		 * machine
		 */
		if (curr_state == new_state)
			new_state = common_pwr_sm_run(curr_state);

		if (curr_state != new_state)
			pwr_sm_set_state(new_state);

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
			K_PRIO_COOP(8), 0, K_NO_WAIT);

	k_thread_name_set(&pwrseq_thread_data, "pwrseq_task");
}

void init_pwr_seq_state(void)
{
	init_chipset_pwr_seq_state();

	pwr_sm_set_state(SYS_POWER_STATE_G3S5);
}

/* Initialize power sequence system state */
static int pwrseq_init()
{
	LOG_ERR("Pwrseq Init\n");

	/* Configure gpio from device tree */
	pwrseq_gpio_init();
	LOG_DBG("Done gpio init");
	/* Register espi handler */
	ndsx_espi_configure();
	/* TODO: Define initial state of power sequence */
	LOG_DBG("Init pwr seq state");
	init_pwr_seq_state();
	/* Create power sequence state handler core function thread */
	create_pwrseq_thread();
	return 0;
}

SYS_INIT(pwrseq_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
