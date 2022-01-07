/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <x86_non_dsx_common_pwrseq_sm_handler.h>

static K_KERNEL_STACK_DEFINE(pwrseq_thread_stack,
			CONFIG_X86_NON_DSW_PWRSEQ_STACK_SIZE);
static struct k_thread pwrseq_thread_data;
static struct pwrseq_context pwrseq_ctx;

LOG_MODULE_REGISTER(ap_pwrseq, 4);

static const struct common_pwrseq_config com_cfg = {
	.pch_dsw_pwrok_delay_ms = DT_INST_PROP(0, dsw_pwrok_delay),
	.pch_pm_pwrbtn_delay_ms =  DT_INST_PROP(0, pm_pwrbtn_delay),
	.pch_rsmrst_delay_ms = DT_INST_PROP(0, rsmrst_delay),
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

void power_signal_interrupt(void)
{
	/* TODO: Add handling */
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

/* Check RSMRST is fine to move from S5 to higher state */
int check_rsmrst_ok(void)
{
	/* TODO: Check if this is still intact*/
	return gpio_pin_get_dt(&com_cfg.pg_ec_rsmrst_odl);
}

int check_pch_out_of_suspend(void)
{
	return gpio_pin_get_dt(&com_cfg.slp_sus_l);
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
 * Add power signal monitoring
 * Add logic to suspend and resume the thread
 */
static int common_pwr_sm_run(int state)
{
	switch (state) {
	case SYS_POWER_STATE_G3:
		/* Nothing to do */
		break;

	case SYS_POWER_STATE_G3S5:
		/* TODO: Check if we are good to move to S5*/
		if (check_pch_out_of_suspend())
			return SYS_POWER_STATE_S5;
		break;

	case SYS_POWER_STATE_S5:
		/* If A-rails are stable move to higher state */
		if (check_power_rails_enabled() && check_rsmrst_ok()) {
			/* rsmrst is intact */
			rsmrst_pass_thru_handler();
			return SYS_POWER_STATE_S5S4;
		}
		break;

	case SYS_POWER_STATE_S5S4:
		/* Check if the PCH has come out of suspend state */
		if (check_rsmrst_ok()) {
			LOG_DBG("RSMRST is ok");
			return SYS_POWER_STATE_S4;
		}
		break;

	case SYS_POWER_STATE_S4:
		return SYS_POWER_STATE_S3;

	case SYS_POWER_STATE_S3:
		/* AP is out of suspend to RAM */
		if (gpio_pin_get_dt(&com_cfg.slp_s3_l))
			return SYS_POWER_STATE_S3S0;
		break;

	case SYS_POWER_STATE_S3S0:
		/* All the power rails must be stable */
		if (gpio_pin_get_dt(&com_cfg.all_sys_pwrgd))
			return SYS_POWER_STATE_S0;
		break;

	case SYS_POWER_STATE_S0:
		/* Stay in S0 */
		break;

	case SYS_POWER_STATE_S4S5:
	case SYS_POWER_STATE_S3S4:
	case SYS_POWER_STATE_S0S3:
	case SYS_POWER_STATE_S5G3:
		break;

	default:
		break;
	}

	return state;
}

static void pwrseq_loop_thread(void *p1, void *p2, void *p3)
{
	int32_t t_wait_ms = 10;
	enum power_states_ndsx curr_state, new_state;

	while (1) {
		curr_state = pwr_sm_get_state();
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
	/* TODO: Define initial state of power sequence */
	LOG_DBG("Init pwr seq state");
	init_pwr_seq_state();
	/* Create power sequence state handler core function thread */
	create_pwrseq_thread();
	return 0;
}

SYS_INIT(pwrseq_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
