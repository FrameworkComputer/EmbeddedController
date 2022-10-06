/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Cannonlake chipset power control module for Chrome EC */

#ifndef __CROS_EC_CANNONLAKE_H
#define __CROS_EC_CANNONLAKE_H

/* Input state flags. */
#define IN_PCH_SLP_S3_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_PCH_SLP_S4_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_S4_DEASSERTED)
#define IN_PCH_SLP_SUS_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_SUS_DEASSERTED)

#define IN_ALL_PM_SLP_DEASSERTED                               \
	(IN_PCH_SLP_S3_DEASSERTED | IN_PCH_SLP_S4_DEASSERTED | \
	 IN_PCH_SLP_SUS_DEASSERTED)

#define IN_PGOOD_ALL_CORE POWER_SIGNAL_MASK(X86_PMIC_DPWROK)

#define IN_ALL_S0                                       \
	(IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED | \
	 PP5000_PGOOD_POWER_SIGNAL_MASK)

#define CHIPSET_G3S5_POWERUP_SIGNAL IN_PCH_SLP_SUS_DEASSERTED

#define CHARGER_INITIALIZED_DELAY_MS 100
#define CHARGER_INITIALIZED_TRIES 40

#endif /* __CROS_EC_CANNONLAKE_H */
