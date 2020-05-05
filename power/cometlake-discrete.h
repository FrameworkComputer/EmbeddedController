/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Chrome EC chipset power control for Cometlake with platform-controlled
 * discrete sequencing.
 */

#ifndef __CROS_EC_COMETLAKE_DISCRETE_H
#define __CROS_EC_COMETLATE_DISCRETE_H

/* Input state flags. */
#define IN_PCH_SLP_S3_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_PCH_SLP_S4_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_S4_DEASSERTED)

#define IN_ALL_PM_SLP_DEASSERTED \
	(IN_PCH_SLP_S3_DEASSERTED | IN_PCH_SLP_S4_DEASSERTED)

/*
 * Power mask used by intel_x86 to check that S5 is ready.
 *
 * This driver controls RSMRST in the G3->S5 transition so this check has nearly
 * no use, but letting the common Intel code read RSMRST allows us to avoid
 * duplicating the common code (introducing a little redundancy instead).
 *
 * PP3300 monitoring is analog-only: power_handle_state enforces that it's good
 * before continuing to common_intel_x86_power_handle_state. This means we can't
 * detect dropouts on that rail, however.
 *
 * Polling analog inputs as a signal for the common code would require
 * modification to support non-power signals as inputs and incurs a minimum 12
 * microsecond time penalty on NPCX7 to do an ADC conversion. Running the ADC
 * in repetitive scan mode and enabling threshold detection on the relevant
 * channels would permit immediate readings (that might be up to 100
 * microseconds old) but is not currently supported by the ADC driver.
 * TODO(b/143188569) try to implement analog watchdogs
 */
#define CHIPSET_G3S5_POWERUP_SIGNAL          \
	(POWER_SIGNAL_MASK(PP5000_A_PGOOD) | \
	 POWER_SIGNAL_MASK(PP1800_A_PGOOD) | \
	 POWER_SIGNAL_MASK(PP1050_A_PGOOD) | \
	 POWER_SIGNAL_MASK(OUT_PCH_RSMRST_DEASSERTED))

/*
 * Power mask used by intel_x86 to check that S3 is ready.
 *
 * Transition S5->S3 only involves turning on the DRAM power rails which are
 * controlled directly from the PCH, so this condition doesn't require any
 * special code, except this collection of signals is also polled in POWER_S3
 * and POWER_S0 states.
 *
 * During normal shutdown the PCH will turn off the DRAM rails before the EC
 * notices, so if this collection includes those rails a normal shutdown will be
 * treated as a power failure so the system immediately drops to G3 rather than
 * doing an orderly shutdown. This must only include those signals that are
 * EC-controlled, not those controlled by the PCH.
 */
#define IN_PGOOD_ALL_CORE CHIPSET_G3S5_POWERUP_SIGNAL

/*
 * intel_x86 power mask for S0 all-OK.
 *
 * This is only used on power task init to check whether the system is powered
 * up and already in S0, to correctly handle switching from RO to RW firmware.
 */
#define IN_ALL_S0                                                   \
	(IN_PGOOD_ALL_CORE | POWER_SIGNAL_MASK(PP2500_DRAM_PGOOD) | \
	 POWER_SIGNAL_MASK(PP1200_DRAM_PGOOD) | IN_ALL_PM_SLP_DEASSERTED)

#define CHARGER_INITIALIZED_DELAY_MS 100
#define CHARGER_INITIALIZED_TRIES 40

/* Power signals, in power-on sequence order. */
enum power_signal {
	PP5000_A_PGOOD,
	/* PP3300 monitoring is analog */
	PP1800_A_PGOOD,
	VPRIM_CORE_A_PGOOD,
	PP1050_A_PGOOD,
	OUT_PCH_RSMRST_DEASSERTED,
	/* S5 ready */
	X86_SLP_S4_DEASSERTED,
	PP2500_DRAM_PGOOD,
	PP1200_DRAM_PGOOD,
	/* S3 ready */
	X86_SLP_S3_DEASSERTED,
	/* PP1050 monitoring is analog */
	PP950_VCCIO_PGOOD,
	/* S0 ready */
	X86_SLP_S0_DEASSERTED,
	CPU_C10_GATE_DEASSERTED,
	IMVP8_READY,

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

/*
 * Board-specific enable for any additional rails in S0.
 *
 * Input 0 to turn off, 1 to turn on.
 *
 * This function may be called from interrupts so must not assume it's running
 * in a task.
 */
void board_enable_s0_rails(int enable);

/*
 * Board-specific flag for whether EN_S0_RAILS can be turned off when
 * CPU_C10_GATED is asserted by the PCH.
 *
 * Return 0 if EN_S0_RAILS must be left on when in S0, even if the PCH asserts
 * the C10 gate.
 *
 * If this can ever return 1, the CPU_C10_GATE_L input from the PCH must also
 * be configured to call c10_gate_interrupt() rather than
 * power_signal_interrupt() in order to actually control the relevant core
 * rails.
 *
 * TODO: it is safe to remove this function and assume C10 gating is enabled if
 * support for rev0 puff boards is no longer required- it was added only for the
 * benefit of those boards.
 */
int board_is_c10_gate_enabled(void);

/*
 * Special interrupt for CPU_C10_GATE_L handling.
 *
 * Response time on resume from C10 has very strict timing requirements- no more
 * than 65 uS to turn on, and the load switches are specified to turn on in 65
 * uS max at 1V (30 uS typical). This means the response to changes on the C10
 * gate input must be as fast as possible to meet PCH timing requirements- much
 * faster than doing this handling in the power state machine can achieve
 * (hundreds of microseconds).
 */
void c10_gate_interrupt(enum gpio_signal signal);

/*
 * Special interrupt for SLP_S3_L handling.
 *
 * The time window in which to turn off some rails when dropping to S3 is
 * ~200us, and using the regular power state machine path tends to have latency
 * >1ms. This ISR short-circuits the relevant signals in a fast path before
 * scheduling a state machine update to ensure sufficiently low latency.
 */
void slp_s3_interrupt(enum gpio_signal signal);

#endif /* __CROS_EC_COMETLAKE_DISCRETE_H */
