/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Chrome EC chipset power control for Cometlake with platform-controlled
 * discrete sequencing.
 */

#include "adc.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "power.h"
#include "power/intel_x86.h"
#include "power_button.h"
#include "task.h"
#include "timer.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

/* Power signals list. Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	[PP5000_A_PGOOD] = {
		GPIO_PG_PP5000_A_OD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PP5000_A_PGOOD",
	},
	[PP1800_A_PGOOD] = {
		GPIO_PG_PP1800_A_OD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PP1800_A_PGOOD",
	},
	[VPRIM_CORE_A_PGOOD] = {
		GPIO_PG_VPRIM_CORE_A_OD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"VPRIM_CORE_A_PGOOD",
	},
	[PP1050_A_PGOOD] = {
		GPIO_PG_PP1050_A_OD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PP1050_A_PGOOD",
	},
	[OUT_PCH_RSMRST_DEASSERTED] = {
		GPIO_PCH_RSMRST_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"OUT_PCH_RSMRST_DEASSERTED",
	},
	[X86_SLP_S4_DEASSERTED] = {
		SLP_S4_SIGNAL_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_S4_DEASSERTED",
	},
	[PP2500_DRAM_PGOOD] = {
		GPIO_PG_PP2500_DRAM_U_OD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PP2500_DRAM_PGOOD",
	},
	[PP1200_DRAM_PGOOD] = {
		GPIO_PG_PP1200_U_OD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PP1200_DRAM_PGOOD",
	},
	[X86_SLP_S3_DEASSERTED] = {
		SLP_S3_SIGNAL_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_S3_DEASSERTED",
	},
	[PP950_VCCIO_PGOOD] = {
		GPIO_PG_PP950_VCCIO_OD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PP950_VCCIO_PGOOD",
	},
	[X86_SLP_S0_DEASSERTED] = {
		GPIO_PCH_SLP_S0_L,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
		"SLP_S0_DEASSERTED",
	},
	[CPU_C10_GATE_DEASSERTED] = {
		GPIO_CPU_C10_GATE_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"CPU_C10_GATE_DEASSERTED",
	},
	[IMVP8_READY] = {
		GPIO_IMVP8_VRRDY_OD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"IMVP8_READY",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/*
 * The EC is responsible for most of the power-on sequence with this driver,
 * enabling rails and waiting for power-good signals from regulators before
 * continuing. The power sequencing works as follows.
 *
 * 1. From G3 (all-off), power is applied and EC power supplies come up.
 *    The power button task kicks off platform power-up as desired.
 * 2. Power up the platform to reach S5
 *   a. Enable PP5000_A and wait for PP5000_A_PGOOD.
 *   b. Enable PP3300_A (EN_ROA_RAILS).
 *   c. Wait for PP3300_A power good. This regulator doesn't provide a power
 *      good output, so the EC monitors ADC_SNS_PP3300.
 *   d. Enable PP1800_A and wait for PP1800_A_PGOOD.
 *   e. PP1800_A_PGOOD automatically enables PPVAR_VPRIM_CORE_A, which receives
 *      power from PP3300_A (hence PP3300_A must precede PP1800_A, even though
 *      PP1800_A draws power from PP3300_G which is guaranteed to already be on)
 *   f. PPVAR_VPRIM_CORE_A_PGOOD automatically enables PP1050_A
 *   g. Wait for PP1050_A_PGOOD, indicating that both PPVAR_VPRIM_CORE_A and
 *      PP1050_A are good.
 *   h. Wait 10ms to satisfy tPCH03, then bring the PCH out of reset by
 *      deasserting RSMRST.
 * 3. The PCH controls transition from S5 up to S3 and higher-power states.
 *   a. PCH deasserts SLP_S4, automatically turning on PP2500_DRAM_U and
 *      PP1200_DRAM_U.
 *   b. Wait for PP2500_DRAM_PGOOD and PP1200_DRAM_PGOOD.
 * 4. PCH deasserts SLP_S3 to switch to S0
 *   a. SLP_S3 transition automatically enables PP1050_ST_S.
 *   b. Wait for PP1050_ST_S good. The power good output from this regulator is
 *      not connected, so the EC monitors ADC_SNS_PP1050_ST_S.
 *   c. Turn on EN_S0_RAILS (enabling PP1200_PLLOC and PP1050_STG).
 *      VCCIO must not ramp up before VCCST, VCCSTG and memory rails are good
 *      (PDG figure 424, note 14).
 *   d. Wait 2ms (for EN_S0_RAILS load switches to turn on).
 *   e. Enable PP950_VCCIO.
 *   f. Wait for PG_PP950_VCCIO. Although the PCH may be asserting CPU_C10_GATED
 *      which holds the VCCIO regulator in a low-power mode, the regulator will
 *      turn on normally and assert power good then drop into low power mode
 *      and continue asserting power good.
 * 5. Transition fully to S0 following SLP_S0
 *   a. Assert VCCST_PWRGD. This notionally tracks PP1050_ST_S but must be
 *      deasserted in S3 and lower.
 *   b. Enable IMVP8_VR.
 *   c. Wait 2ms.
 *   d. Assert SYS_PWROK.
 *   e. Wait for IMVP8_VRRDY.
 *   f. Wait 2ms.
 *   g. Assert PCH_PWROK.
 *
 * When CPU_C10_GATED is asserted, we are free to disable PP1200_PLLOC and
 * PP1050_STG by deasserting EN_S0_RAILS to save some power. VCCIO is
 * automatically placed in low-power mode by CPU_C10_GATED, and no further
 * action is required- power-good signals will not change, just the relevant
 * load switches (which are specified to meet the platform's minimum turn-on
 * time when CPU_C10_GATED is deasserted again) are turned off. This gating is
 * done asynchronously directly in the interrupt handler because its timing is
 * very tight.
 *
 * For further reference, Figure 421 and Table 370 in the Comet Lake U PDG
 * summarizes platform power rail requirements in a reasonably easy-to-digest
 * manner, while section 12.11 (containing those diagrams) details the required
 * operation.
 */

/*
 * Reverse of S0->S3 transition.
 *
 * This is a separate function so it can be reused when forcing shutdown due to
 * power failure or other reasons.
 *
 * This function may be called from an ISR (slp_s3_interrupt) so must not
 * assume that it's running in a regular task.
 */
static void shutdown_s0_rails(void)
{
	board_enable_s0_rails(0);
	/*
	 * Deassert VCCST_PG as early as possible to satisfy tCPU22; VDDQ is
	 * derived directly from SLP_S3.
	 */
	gpio_set_level(GPIO_VCCST_PG_OD, 0);
	gpio_set_level(GPIO_EC_PCH_PWROK, 0);
	gpio_set_level(GPIO_EC_PCH_SYS_PWROK, 0);
	gpio_set_level(GPIO_EN_IMVP8_VR, 0);
	gpio_set_level(GPIO_EN_S0_RAILS, 0);
	/*
	 * * tPCH10: PCH_PWROK to VCCIO off >400ns (but only on unexpected
	 *   power-down)
	 * * tPLT18: SLP_S3_L to VCCIO disable <200us
	 *
	 * tPCH10 is only 7 CPU cycles at 16 MHz so we should satisfy that
	 * minimum time with no extra code, and sleeping is likely to cause
	 * a delay that exceeds tPLT18.
	 */
	gpio_set_level(GPIO_EN_PP950_VCCIO, 0);
}

/*
 * Reverse of G3->S5 transition.
 *
 * This is a separate function so it can be reused when forcing shutdown due to
 * power failure or other reasons.
 */
static void shutdown_s5_rails(void)
{
	gpio_set_level(GPIO_PCH_RSMRST_L, 0);
	/* tPCH12: RSMRST to VCCPRIM (PPVAR_VPRIM_CORE_A) off >400ns */
	crec_usleep(1);
	gpio_set_level(GPIO_EN_PP1800_A, 0);
	gpio_set_level(GPIO_EN_ROA_RAILS, 0);
#ifdef CONFIG_POWER_PP5000_CONTROL
	power_5v_enable(task_get_current(), 0);
#else
	gpio_set_level(GPIO_EN_PP5000_A, 0);
#endif
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s(%d)", __func__, reason);
	report_ap_reset(reason);

	shutdown_s0_rails();
	/* S3->S5 is automatic based on SLP_S3 driving memory rails. */
	shutdown_s5_rails();
}

void chipset_handle_espi_reset_assert(void)
{
}

enum power_state chipset_force_g3(void)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);

	return POWER_G3;
}

/*
 * Wait for a power rail on an analog channel to become good.
 *
 * @param channel	ADC channel to read
 * @param min_voltage	Minimum required voltage for rail (in mV)
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
static int power_wait_analog(enum adc_channel channel, int min_voltage)
{
	timestamp_t deadline;
	int reading;

	/* One second timeout */
	deadline = get_time();
	deadline.val += SECOND;

	do {
		reading = adc_read_channel(channel);
		if (reading == ADC_READ_ERROR)
			return EC_ERROR_HW_INTERNAL;
		if (timestamp_expired(deadline, NULL))
			return EC_ERROR_TIMEOUT;
	} while (reading < min_voltage);

	return EC_SUCCESS;
}

/*
 * Force system power state if we time out waiting for a power rail to become
 * good.
 *
 * In general the new state is to transition down to the next lower-power state,
 * so if we time out in G3->S5 we return POWER_G3 to turn things off again and
 * if S3->S0 times out we return POWER_S3S5 for the same reason.
 *
 * Correct sequencing of rails that might already be enabled is handled by
 * chipset_force_shutdown(), so the caller of this function doesn't need to
 * clean up after itself.
 */
static enum power_state pgood_timeout(enum power_state new_state)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_WAIT);
	return new_state;
}

/*
 * Called in the chipset task when power signal inputs change state.
 * If this doesn't request a different state, power_common_state handles it.
 *
 * @param state Current power state
 * @return New power state
 */
enum power_state power_handle_state(enum power_state state)
{
	switch (state) {
	case POWER_G3S5:
		if (intel_x86_wait_power_up_ok() != EC_SUCCESS) {
			chipset_force_shutdown(
				CHIPSET_SHUTDOWN_BATTERY_INHIBIT);
			return POWER_G3;
		}
		/* Power-up steps 2a-2h. */
#ifdef CONFIG_POWER_PP5000_CONTROL
		power_5v_enable(task_get_current(), 1);
#else
		gpio_set_level(GPIO_EN_PP5000_A, 1);
#endif
		if (power_wait_signals(POWER_SIGNAL_MASK(PP5000_A_PGOOD)))
			return pgood_timeout(POWER_S5G3);
		gpio_set_level(GPIO_EN_ROA_RAILS, 1);
		if (power_wait_analog(ADC_SNS_PP3300, 3000) != EC_SUCCESS)
			return pgood_timeout(POWER_S5G3);
		gpio_set_level(GPIO_EN_PP1800_A, 1);
		if (power_wait_signals(POWER_SIGNAL_MASK(PP1800_A_PGOOD) |
				       POWER_SIGNAL_MASK(PP1050_A_PGOOD)))
			return pgood_timeout(POWER_S5G3);
		crec_msleep(10); /* tPCH03: VCCPRIM good -> RSMRST >10ms */
		gpio_set_level(GPIO_PCH_RSMRST_L, 1);
		break;

	case POWER_S5G3:
		shutdown_s5_rails();
		break;

	case POWER_S5S3:
		/* Power-up steps 3a-3b. */
		if (power_wait_signals(POWER_SIGNAL_MASK(PP2500_DRAM_PGOOD) |
				       POWER_SIGNAL_MASK(PP1200_DRAM_PGOOD)))
			return pgood_timeout(POWER_S3S5);
		break;

	case POWER_S3S0:
		/* Power-up steps 4a-4f. */
		if (power_wait_analog(ADC_SNS_PP1050, 1000) != EC_SUCCESS)
			return pgood_timeout(POWER_S3S5);
		gpio_set_level(GPIO_EN_S0_RAILS, 1);
		crec_msleep(2);
		gpio_set_level(GPIO_EN_PP950_VCCIO, 1);
		if (power_wait_signals(POWER_SIGNAL_MASK(PP950_VCCIO_PGOOD)))
			return pgood_timeout(POWER_S3S5);

		/* Power-up steps 5a-5h */
		gpio_set_level(GPIO_VCCST_PG_OD, 1);
		gpio_set_level(GPIO_EN_IMVP8_VR, 1);
		crec_msleep(2);
		gpio_set_level(GPIO_EC_PCH_SYS_PWROK, 1);
		if (power_wait_signals(POWER_SIGNAL_MASK(IMVP8_READY)))
			return pgood_timeout(POWER_S3S5);
		crec_msleep(2);
		gpio_set_level(GPIO_EC_PCH_PWROK, 1);

		board_enable_s0_rails(1);
		break;

	case POWER_S0S3:
		/*
		 * Handled in the slp_s3_interrupt fast path, but also run
		 * here in case we miss the interrupt somehow.
		 */
		shutdown_s0_rails();
		break;

	case POWER_S5:
		/*
		 * Return to G3 if S5 rails are not on, probably because of
		 * a forced power-off.
		 */
		if ((power_get_signals() & CHIPSET_G3S5_POWERUP_SIGNAL) !=
		    CHIPSET_G3S5_POWERUP_SIGNAL)
			return POWER_S5G3;
		break;

	default:
		break;
	}

	/*
	 * Power-up steps 3a-3b (S5->S3 via IN_PGOOD_ALL_CORE) plus general
	 * bookkeeping.
	 */
	return common_intel_x86_power_handle_state(state);
}

#ifdef CONFIG_VBOOT_EFS
/*
 * Called in main() to ensure chipset power is in a good state.
 *
 * This may be useful because EC reset could happen under unexpected
 * conditions and we want to ensure that if the AP is wedged for some
 * reason (for instance) we unwedge it before continuing.
 *
 * Because power sequencing here is all EC-controlled and this is called
 * as part of the init sequence, we don't need to do anything- EC reset
 * implies power sequencing is all-off and we don't have any external
 * PMIC to synchronize state with.
 */
void chipset_handle_reboot(void)
{
}
#endif /* CONFIG_VBOOT_EFS */

void c10_gate_interrupt(enum gpio_signal signal)
{
	/*
	 * Per PDG, gate VccSTG and VCCIO on (SLP_S3_L && CPU_C10_GATE_L).
	 *
	 * When in S3 we let the state machine do it since timing is less
	 * critical; when in S0/S0ix we do it here because timing is very
	 * tight.
	 */
	if (board_is_c10_gate_enabled() && gpio_get_level(GPIO_SLP_S3_L)) {
		int enable_core = gpio_get_level(GPIO_CPU_C10_GATE_L);

		gpio_set_level(GPIO_EN_S0_RAILS, enable_core);
	}

	return power_signal_interrupt(signal);
}

void slp_s3_interrupt(enum gpio_signal signal)
{
	if (!gpio_get_level(GPIO_SLP_S3_L) &&
	    chipset_in_state(CHIPSET_STATE_ON)) {
		/* Falling edge on SLP_S3_L means dropping to S3 from S0 */
		shutdown_s0_rails();
	}

	return power_signal_interrupt(signal);
}
