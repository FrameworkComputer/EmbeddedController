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
 * special code- just check that the DRAM rails are good.
 */
#define IN_PGOOD_ALL_CORE                                                     \
	(CHIPSET_G3S5_POWERUP_SIGNAL | POWER_SIGNAL_MASK(PP2500_DRAM_PGOOD) | \
	 POWER_SIGNAL_MASK(PP1200_DRAM_PGOOD))

/*
 * intel_x86 power mask for S0 all-OK.
 *
 * This is only used on power task init to check whether the system is powered
 * up and already in S0, to correctly handle switching from RO to RW firmware.
 */
#define IN_ALL_S0                                       \
	(IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED)

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

#endif /* __CROS_EC_COMETLAKE_DISCRETE_H */
