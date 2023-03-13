/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "power.h"
#include "power_sequence.h"
#include "task.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ##args)

#define IN_VR_PGOOD POWER_SIGNAL_MASK(X86_VR_PG)

static int power_ready;
static int power_s5_up;		/* Chipset is sequencing up or down */
static int keep_pch_power;	/* For S4 wake source */
static int ap_boot_delay = 9;	/* For global reset to wait SLP_S5 signal de-asserts */
static int s5_exit_tries;	/* For global reset to wait SLP_S5 signal de-asserts */
static int force_g3_flags;	/* Chipset force to g3 immediately when chipset force shutdown */

/* Power Signal Input List */
const struct power_signal_info power_signal_list[] = {
	[X86_3VALW_PG] = {
		.gpio = GPIO_POWER_GOOD_3VALW,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "3VALW_PG_DEASSERTED",
	},
	[X86_SLP_S3_N] = {
		.gpio = GPIO_PCH_SLP_S3_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S3_DEASSERTED",
	},
	[X86_SLP_S5_N] = {
		.gpio = GPIO_PCH_SLP_S5_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S5_DEASSERTED",
	},
	[X86_VR_PG] = {
		.gpio = GPIO_POWER_GOOD_VR,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "VR_PG_DEASSERTED",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

static void board_power_on(void);
DECLARE_DEFERRED(board_power_on);
DECLARE_HOOK(HOOK_INIT, board_power_on, HOOK_PRIO_DEFAULT);

static void board_power_on(void)
{
	static int logs_printed; /* Only prints the log one time */

	/*
	 * we need to wait the 3VALW power rail ready
	 * then enable 0.75VALW and 1.8VALW power rail
	 */
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_spok)) == 1) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75_1p8valw_pwren), 1);
		power_ready = 1;
		CPRINTS("0.75 and 1.8 VALW power rail ready");
	} else {
		if (!logs_printed) {
			CPRINTS("wait 3VALW power rail ready");
			logs_printed = 1;
		}
		hook_call_deferred(&board_power_on_data, 5 * MSEC);
	}
}

int get_power_rail_status(void)
{
	/*
	 * If the 3VALW, 0.75VALW and 1.8VALW power rail not ready,
	 * the unit should not power on.
	 * This function will be used by PB task.
	 */
	return power_ready;
}

void power_s5_up_control(int control)
{
	CPRINTS("%s power s5 up!", control ? "setup" : "clear");
	power_s5_up = control;
}

void chipset_reset(enum chipset_shutdown_reason reason)
{
	/* unused function, EC doesn't control GPIO_SYS_RESET_L */
}

static void chipset_force_g3(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_syson), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_apu_aud_pwr_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwr_en), 0);
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s(%d)", __func__, reason);
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		report_ap_reset(reason);
		keep_pch_power = 0;
		force_g3_flags = 1;
		chipset_force_g3();
	}
}

enum power_state power_chipset_init(void)
{
	/* If we don't need to image jump to RW, always start at G3 state */
	chipset_force_g3();
	return POWER_G3;
}

enum power_state power_handle_state(enum power_state state)
{
	switch (state) {
	case POWER_G3:
		break;

	case POWER_G3S5:
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_apu_aud_pwr_en), 1);
		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwr_en), 1);
		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 1);
		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l), 1);

		/* Customizes power button out signal without PB task for powering on. */
		k_msleep(90);
		CPRINTS("PCH PBTN LOW");
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 0);
		k_msleep(20);
		CPRINTS("PCH PBTN HIGH");
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 1);

		/* Exit SOC G3 */
		CPRINTS("Exit SOC G3");
		power_s5_up_control(1);
		return POWER_S5;

	case POWER_S5:

		if (force_g3_flags) {
			force_g3_flags = 0;
			return POWER_S5G3;
		}

		if (power_s5_up) {
			while (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s5_l)) == 0) {
				if (task_wait_event(SECOND) == TASK_EVENT_TIMER) {
					if (++s5_exit_tries > ap_boot_delay) {
						CPRINTS("timeout waiting for S5 exit");
						/*
						 * TODO: RTC reset function
						 * TODO: ODM stress tool feature
						 */

						/* SLP_S5 asserted, power down to G3S5 state */
						return POWER_S5G3;
					}
				}
			}
			/* Power up to next state */
			return POWER_S5S3;
		}

		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s5_l)) == 1) {
			/* Power up to next state */
			return POWER_S5S3;
		}

		break;

	case POWER_S5S3:

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);
		return POWER_S3;

	case POWER_S3:
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) == 1) {
			/* Power up to next state */
			k_msleep(10);
			return POWER_S3S0;
		} else if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s5_l)) == 0) {
			k_msleep(55);
			/* Power down to next state */
			return POWER_S3S5;
		}
		break;

	case POWER_S3S0:

		/*
		 * TODO: distinguish S5 -> S0 and S3 -> S0, the sequences are different
		 * S5 -> S0: wait 10 - 15 ms then assert the SYSON
		 * S3 -> S0: wait 10 - 15 ms then assert the SUSP_L
		 * currently, I will follow the power on sequence to make sure DUT can
		 * power up from S5.
		 */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_syson), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_vsys_vadp_en), 1);
		k_msleep(20);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 1);
		k_msleep(20);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 1);

		/* wait VR power good */
		if (power_wait_signals(IN_VR_PGOOD)) {
			/* something wrong, turn off power and force to g3 */
			chipset_force_g3();
			return POWER_G3;
		}

		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);
		return POWER_S0;

	case POWER_S0:

		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) == 0) {
			/* Power down to next state */
			k_msleep(5);
			return POWER_S0S3;
		}
		break;

	case POWER_S0S3:
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
		k_msleep(85);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 0);

		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);
		return POWER_S3;

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		power_s5_up_control(0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_syson), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_vsys_vadp_en), 0);
		hook_notify(HOOK_CHIPSET_SHUTDOWN);
		return POWER_S5;

	case POWER_S5G3:

		/*
		 * We need to keep pch power to wait SLP_S5 signal for the below cases:
		 *
		 * 1. Customer testing tool
		 * 2. There is a type-c USB input deck connect on the unit
		 */
		if (keep_pch_power)
			return POWER_S5;

		/* Don't need to keep pch power, turn off the pch power and power down to G3*/
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l), 0);
		k_msleep(5);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 0);
		k_msleep(5);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_apu_aud_pwr_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwr_en), 0);
		return POWER_G3;
	default:
		break;
	}
	return state;
}

/* Peripheral power control */
static void peripheral_power_startup(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wlan_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_h_prochot_l), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wl_rst_l), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, peripheral_power_startup, HOOK_PRIO_DEFAULT);

static void peripheral_power_resume(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_mute_l), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_reset), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_cam_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_invpwr), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sleep_l), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sm_panel_bken_ec), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, peripheral_power_resume, HOOK_PRIO_DEFAULT);

static void peripheral_power_shutdown(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wlan_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_h_prochot_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wl_rst_l), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, peripheral_power_shutdown, HOOK_PRIO_DEFAULT);

static void peripheral_power_suspend(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_mute_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_reset), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_cam_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_invpwr), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sleep_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sm_panel_bken_ec), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, peripheral_power_suspend, HOOK_PRIO_DEFAULT);

void chipset_throttle_cpu(int throttle)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_h_prochot_l), !throttle);
}
