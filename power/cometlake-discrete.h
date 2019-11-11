/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chrome EC chipset power control for Cometlake with platform-controlled
 * discrete sequencing.
 */

#ifndef __CROS_EC_COMETLAKE_DISCRETE_H
#define __CROS_EC_COMETLATE_DISCRETE_H

/* Input state flags. */
#define IN_PCH_SLP_S3_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_PCH_SLP_S4_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_S4_DEASSERTED)

#define IN_ALL_PM_SLP_DEASSERTED \
	(IN_PCH_SLP_S3_DEASSERTED | IN_PCH_SLP_S4_DEASSERTED)

/* TODO(b/143188569) RSMRST_L is an EC output, can't use POWER_SIGNAL_MASK */
#define IN_PGOOD_ALL_CORE \
	POWER_SIGNAL_MASK(/*X86_RSMRST_L_PGOOD*/ POWER_SIGNAL_COUNT)

#define IN_ALL_S0                                       \
	(IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED | \
	 PP5000_PGOOD_POWER_SIGNAL_MASK)

/* TODO(b/143188569) RSMRST_L is an EC output, can't use POWER_SIGNAL_MASK */
#define CHIPSET_G3S5_POWERUP_SIGNAL                                     \
	(POWER_SIGNAL_MASK(/*X86_RSMRST_L_PGOOD*/ POWER_SIGNAL_COUNT) | \
	 POWER_SIGNAL_MASK(PP5000_A_PGOOD))

#define CHARGER_INITIALIZED_DELAY_MS 100
#define CHARGER_INITIALIZED_TRIES 40

/* Power signals, in power-on sequence order. */
enum power_signal {
	PP5000_A_PGOOD,
	/* PP3300 monitoring is analog */
	PP1800_A_PGOOD,
	VPRIM_CORE_A_PGOOD,
	PP1050_A_PGOOD,
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
