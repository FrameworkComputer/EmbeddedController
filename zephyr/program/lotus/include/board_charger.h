/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_CHARGER_H
#define __CROS_EC_BOARD_CHARGER_H


/* Customer charger defination */
#define ISL9241_CONTROL1_IMON				BIT(5)
#define ISL9241_CONTROL1_PROCHOT_REF_6000		(3 << 0)
#define ISL9241_CONTROL1_PROCHOT_REF_6800               (7 << 0)

#define ISL9241_CONTROL2_GENERAL_PURPOSE_COMPARATOR	BIT(3)
#define ISL9241_CONTROL2_PROCHOT_DEBOUNCE_100		(1 << 9)

#define ISL9241_CONTROL1_EXIT_LEARN_MODE		BIT(12)
#define ISL9241_CONTROL3_BATGONE			BIT(4)

#define ISL9241_CONTROL4_GP_COMPARATOR			BIT(12)
#define ISL9241_CONTROL4_WOCP_FUNCTION			BIT(9)
#define ISL9241_CONTROL4_VSYS_SHORT_CHECK		BIT(8)
#define ISL9241_CONTROL4_ACOK_BATGONE_DEBOUNCE_25US	(1 << 2)

#define ISL9241_CONTROL0_BGATE_FORCE_ON		BIT(10)

enum ec_prochot_status {
	EC_DEASSERTED_PROCHOT = 0,
	EC_ASSERTED_PROCHOT = 1,
};

int update_charger_in_cutoff_mode(void);

#ifdef CONFIG_BOARD_LOTUS
int charger_in_bypass_mode(void);
void board_charger_lpm_control(int enable);
void board_disable_bypass_oneshot(void);
#endif

#endif	/* __CROS_EC_BOARD_CHARGER_H */
