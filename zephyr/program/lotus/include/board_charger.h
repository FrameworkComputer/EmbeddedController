/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_CHARGER_H
#define __CROS_EC_BOARD_CHARGER_H


/* Customer charger defination */
#define ISL9241_CONTROL1_IMON				BIT(5)
#define ISL9241_CONTROL1_PROCHOT_REF_6800		(7 << 0)

#define ISL9241_CONTROL2_GENERAL_PURPOSE_COMPARATOR	BIT(3)
#define ISL9241_CONTROL2_PROCHOT_DEBOUNCE_100		(1 << 9)

#define ISL9241_CONTROL3_PSYS_GAIN			(3 << 8)

#define ISL9241_CONTROL4_GP_COMPARATOR			BIT(12)

/**
 * Control charger's BGATE and NGATE for power saving
 */
void charger_gate_onoff(uint8_t enable);

/**
 * Control charger's psys enable/disable for pwoer saving
 */
void charger_psys_enable(uint8_t enable);

#endif	/* __CROS_EC_BOARD_CHARGER_H */
