/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <x86_non_dsx_adlp_pwrseq_sm.h>

LOG_MODULE_DECLARE(ap_pwrseq, 4);

static const struct chipset_pwrseq_config chip_cfg = {
	.pch_pwrok_delay_ms = DT_INST_PROP(0, pch_pwrok_delay),
	.sys_pwrok_delay_ms = DT_INST_PROP(0, sys_pwrok_delay),
	.vccst_pwrgd_delay_ms = DT_INST_PROP(0, vccst_pwrgd_delay),
	.vrrdy_timeout_ms = DT_INST_PROP(0, vrrdy_timeout),
	.sys_reset_delay_ms = DT_INST_PROP(0, sys_reset_delay),
	.all_sys_pwrgd_timeout = DT_INST_PROP(0, all_sys_pwrgd_timeout),
	.vccst_pwrgd_od = GPIO_DT_SPEC_GET(DT_DRV_INST(0),
						vccst_pwrgd_od_gpios),
	.imvp9_vrrdy_od = GPIO_DT_SPEC_GET(DT_DRV_INST(0),
						imvp9_vrrdy_od_gpios),
	.pch_pwrok = GPIO_DT_SPEC_GET(DT_DRV_INST(0), pch_pwrok_gpios),
	.ec_pch_sys_pwrok = GPIO_DT_SPEC_GET(DT_DRV_INST(0),
						ec_pch_sys_pwrok_gpios),
	.sys_rst_l = GPIO_DT_SPEC_GET(DT_DRV_INST(0), sys_rst_l_gpios),
};

void ap_off(void)
{
	/* TODO: This could be added as g3action handler */
	gpio_pin_set_dt(&chip_cfg.vccst_pwrgd_od, 0);
	gpio_pin_set_dt(&chip_cfg.pch_pwrok, 0);
	gpio_pin_set_dt(&chip_cfg.ec_pch_sys_pwrok, 0);
}

/* This should be overridden if there is no power sequencer chip */
__attribute__((weak)) int intel_x86_get_pg_ec_all_sys_pwrgd(
				const struct common_pwrseq_config *com_cfg)
{
	return gpio_pin_get_dt(&com_cfg->all_sys_pwrgd);
}

/* Handle ALL_SYS_PWRGD signal
 * This will be overridden if the custom signal handler is needed
 */
__attribute__((weak)) int all_sys_pwrgd_handler(
				const struct common_pwrseq_config *com_cfg)
{
	int retry = 0;

	/* TODO: Add condition for no power sequencer */
	k_msleep(chip_cfg.all_sys_pwrgd_timeout);

	if (intel_x86_get_pg_ec_all_sys_pwrgd(com_cfg) == 0) {
	/* Todo: Remove workaround for the retry
	 * without this change the system hits G3 as it detects
	 * ALL_SYS_PWRGD as 0 and then 1 as a glitch
	 */
		while (!intel_x86_get_pg_ec_all_sys_pwrgd(com_cfg)) {
			if (++retry > 2) {
				LOG_ERR("PG_EC_ALL_SYS_PWRGD not ok\n");
				ap_off();
				return -1;
			}
			k_msleep(10);
		}
	}

	/* PG_EC_ALL_SYS_PWRGD is asserted, enable VCCST_PWRGD_OD. */

	if (gpio_pin_get_dt(&chip_cfg.vccst_pwrgd_od) == 0) {
		k_msleep(chip_cfg.vccst_pwrgd_delay_ms);
		gpio_pin_set_dt(&chip_cfg.vccst_pwrgd_od, 1);
	}
	return 0;
}

/*
 * We have asserted VCCST_PWRGO_OD, now wait for the IMVP9.1
 * to assert IMVP9_VRRDY_OD.
 *
 * Returns state of VRRDY.
 */

static int wait_for_vrrdy(void)
{
	int timeout_ms = chip_cfg.vrrdy_timeout_ms;

	for (; timeout_ms > 0; --timeout_ms) {
		if (gpio_pin_get_dt(&chip_cfg.imvp9_vrrdy_od) != 0)
			return 1;
		k_msleep(1);
	}
	return 0;
}

/* PCH_PWROK to PCH from EC */
int generate_pch_pwrok_handler(void)
{
	/* Enable PCH_PWROK, gated by VRRDY. */
	if (gpio_pin_get_dt(&chip_cfg.pch_pwrok) == 0) {
		if (wait_for_vrrdy() == 0) {
			LOG_DBG("Timed out waiting for VRRDY, "
				"shutting AP off!");
			ap_off();
			return -1;
		}
		k_msleep(chip_cfg.pch_pwrok_delay_ms);
		gpio_pin_set_dt(&chip_cfg.pch_pwrok, 1);
		LOG_DBG("Set PCH_PWROK\n");
	}

	return 0;
}

/* Generate SYS_PWROK->SOC if needed by system */
void generate_sys_pwrok_handler(const struct common_pwrseq_config *com_cfg)
{
	/* Enable PCH_SYS_PWROK. */
	if (gpio_pin_get_dt(&chip_cfg.ec_pch_sys_pwrok) == 0) {
		k_msleep(chip_cfg.sys_pwrok_delay_ms);
		/* Check if we lost power while waiting. */
		if (intel_x86_get_pg_ec_all_sys_pwrgd(com_cfg) == 0) {
			LOG_DBG("PG_EC_ALL_SYS_PWRGD deasserted, "
				"shutting AP off!");
			ap_off();
			return;
		}
		gpio_pin_set_dt(&chip_cfg.ec_pch_sys_pwrok, 1);
		/* PCH will now release PLT_RST */
	}
}

/* Chipset specific power state machine handler */

/* TODO: Separate with and without power sequencer logic here */

void s0_action_handler(const struct common_pwrseq_config *com_cfg)
{
	int ret;

	/* Handle DSW_PWROK passthrough */
	/* This is not needed for alderlake silego, guarded by CONFIG? */

	/* Check ALL_SYS_PWRGD and take action */
	ret = all_sys_pwrgd_handler(com_cfg);
	if (ret) {
		LOG_DBG("ALL_SYS_PWRGD handling failed err= %d\n", ret);
		return;
	}

	/* Send PCH_PWROK->SoC if conditions met */
	/* TODO: There is possibility of EC not needing to generate
	 * this as power sequencer may do it
	 */
	ret = generate_pch_pwrok_handler();
	if (ret) {
		LOG_DBG("PCH_PWROK handling failed err=%d\n", ret);
		return;
	}

	/* SYS_PWROK may be optional and the delay must be
	 * configurable as it is variable with platform
	 */
	/* Send SYS_PWROK->SoC if conditions met */
	generate_sys_pwrok_handler(com_cfg);
}

/* This should be overridden if there is no power sequencer chip */
__attribute__((weak)) int intel_x86_get_pg_ec_dsw_pwrok(
			const struct common_pwrseq_config *com_cfg)
{
#if PWRSEQ_GPIO_PRESENT(pg_ec_dsw_pwroks_gpios)
	return gpio_pin_get_dt(&com_cfg->pg_ec_dsw_pwrok);
#else
	return 0;
#endif
}

void intel_x86_sys_reset_delay(void)
{
	/*
	 * Debounce time for SYS_RESET_L is 16 ms. Wait twice that period
	 * to be safe.
	 */
	k_msleep(chip_cfg.sys_reset_delay_ms);
}

void chipset_reset(enum pwrseq_chipset_shutdown_reason reason)
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
	LOG_DBG("%s: %d", __func__, reason);

	/*
	 * Toggling SYS_RESET_L will not have any impact when it's already
	 * low (i,e. Chipset is in reset state).
	 */
	if (gpio_pin_get_dt(&(chip_cfg.sys_rst_l)) == 0) {
		LOG_DBG("Chipset is in reset state");
		return;
	}

	gpio_pin_set_dt(&(chip_cfg.sys_rst_l), 0);
	intel_x86_sys_reset_delay();
	gpio_pin_set_dt(&(chip_cfg.sys_rst_l), 1);
}

void chipset_force_shutdown(enum pwrseq_chipset_shutdown_reason reason,
				const struct common_pwrseq_config *com_cfg)
{
	int timeout_ms = 50;

	/* TODO: below
	 * report_ap_reset(reason);
	 */

	/* Turn off RMSRST_L  to meet tPCH12 */
	gpio_pin_set_dt(&com_cfg->ec_pch_rsmrst_odl, 0);

	/* Turn off S5 rails */
	gpio_pin_set_dt(&com_cfg->enable_pp5000_a, 0);

	/*
	 * TODO(b/179519791): Replace this wait with
	 * power_wait_signals_timeout()
	 */
	/* Now wait for DSW_PWROK and  RSMRST_ODL to go away. */
	while (intel_x86_get_pg_ec_dsw_pwrok(com_cfg) &&
			gpio_pin_get_dt(&com_cfg->pg_ec_rsmrst_odl) &&
			(timeout_ms > 0)) {
		k_msleep(1);
		timeout_ms--;
	};

	if (!timeout_ms)
		LOG_DBG("DSW_PWROK or RSMRST_ODL didn't go low!  Assuming G3.");
}


void g3s5_action_handler(const struct common_pwrseq_config *com_cfg)
{
	gpio_pin_set_dt(&com_cfg->enable_pp5000_a, 1);
}

void init_chipset_pwr_seq_state(void)
{
	int ret = 0;

	/* Configure gpios specific to the chipset */
	ret |= gpio_pin_configure_dt(&chip_cfg.vccst_pwrgd_od,
							GPIO_OUTPUT_LOW);
	ret |= gpio_pin_configure_dt(&chip_cfg.imvp9_vrrdy_od,
							GPIO_INPUT);
	ret |= gpio_pin_configure_dt(&chip_cfg.pch_pwrok,
							GPIO_OUTPUT_LOW);
	ret |= gpio_pin_configure_dt(&chip_cfg.ec_pch_sys_pwrok,
							GPIO_OUTPUT_LOW);
	ret |= gpio_pin_configure_dt(&chip_cfg.sys_rst_l,
							GPIO_OUTPUT_HIGH);

	if (!ret)
		LOG_INF("Configuring GPIO complete");
	else
		LOG_ERR("Power seq gpio configuration failed\n");
}

enum power_states_ndsx chipset_pwr_sm_run(enum power_states_ndsx curr_state,
				 const struct common_pwrseq_config *com_cfg)
{
/* Add chipset specific state handling if any */
	switch (curr_state) {
	case SYS_POWER_STATE_G3S5:
		g3s5_action_handler(com_cfg);
		break;
	case SYS_POWER_STATE_S5:
		break;
	case SYS_POWER_STATE_S0:
		s0_action_handler(com_cfg);
		break;
	default:
		break;
	}
	return curr_state;
}
