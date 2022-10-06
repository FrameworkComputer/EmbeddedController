/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Cometlake chipset power control module for Chrome EC */

#ifndef __CROS_EC_COMETLAKE_H
#define __CROS_EC_COMETLAKE_H

/* Input state flags. */
#define IN_PCH_SLP_S3_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_PCH_SLP_S4_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_S4_DEASSERTED)

#define IN_ALL_PM_SLP_DEASSERTED \
	(IN_PCH_SLP_S3_DEASSERTED | IN_PCH_SLP_S4_DEASSERTED)

#define IN_PGOOD_ALL_CORE POWER_SIGNAL_MASK(X86_RSMRST_L_PGOOD)

#define IN_ALL_S0                                       \
	(IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED | \
	 PP5000_PGOOD_POWER_SIGNAL_MASK)

#define CHIPSET_G3S5_POWERUP_SIGNAL              \
	(POWER_SIGNAL_MASK(X86_RSMRST_L_PGOOD) | \
	 POWER_SIGNAL_MASK(X86_PP5000_A_PGOOD))

#define CHARGER_INITIALIZED_DELAY_MS 100
#define CHARGER_INITIALIZED_TRIES 40

/* Power signals list */
enum power_signal {
	X86_SLP_S0_DEASSERTED,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,
	X86_RSMRST_L_PGOOD,
	X86_PP5000_A_PGOOD,
	X86_ALL_SYS_PGOOD,

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

void all_sys_pgood_check_reboot(void);
__override_proto void board_chipset_forced_shutdown(void);

#endif /* __CROS_EC_COMETLAKE_H */
