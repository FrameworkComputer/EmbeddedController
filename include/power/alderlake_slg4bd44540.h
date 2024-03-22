/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Alder Lake chipset power control module using the SLG4BD44540 power
 * sequencer chip for Chrome EC
 */

#ifndef __CROS_EC_ALDERLAKE_SLG4BD44540_H
#define __CROS_EC_ALDERLAKE_SLG4BD44540_H

/* Input state flags. */
#define IN_PCH_SLP_S3_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_PCH_SLP_S4_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_S4_DEASSERTED)
#define IN_PCH_SLP_SUS_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_SUS_DEASSERTED)

#define IN_ALL_PM_SLP_DEASSERTED                               \
	(IN_PCH_SLP_S3_DEASSERTED | IN_PCH_SLP_S4_DEASSERTED | \
	 IN_PCH_SLP_SUS_DEASSERTED)

#define IN_PGOOD_ALL_CORE POWER_SIGNAL_MASK(X86_DSW_DPWROK)

#define IN_ALL_S0 (IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED)

#define CHIPSET_G3S5_POWERUP_SIGNAL IN_PCH_SLP_SUS_DEASSERTED

#define CHARGER_INITIALIZED_DELAY_MS 100
#define CHARGER_INITIALIZED_TRIES 40

/* Power signals list */
enum power_signal {
	X86_SLP_S0_DEASSERTED,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,
	X86_SLP_S5_DEASSERTED,
	X86_SLP_SUS_DEASSERTED,
	X86_RSMRST_L_PGOOD,
	X86_DSW_DPWROK,
	X86_ALL_SYS_PGOOD,

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

__override_proto int board_get_all_sys_pgood(void);

#endif /* __CROS_EC_ALDERLAKE_SLG4BD44540_H */
