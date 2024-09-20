/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PTLRVP_NPCX_BOARD_H_
#define PTLRVP_NPCX_BOARD_H_

/* Power Signals */
#define PWR_EN_PP3300_S5 &gpio4 1
#define PWR_EN_PP5000_S5 &gpio3 2
#define PWR_RSMRST_PWRGD &gpio7 2
#define PWR_EC_PCH_RSMRST &gpio9 5
#define PWR_SLP_S0 &gpioe 1
#define PWR_PCH_PWROK &gpio6 1
#define PWR_EC_PCH_SYS_PWROK &gpio6 4
#define PWR_ALL_SYS_PWRGD &gpio6 3

/* I2C Ports */
#define CHARGER_I2C i2c2_0

#define PD_POW_I2C i2c0_0

/* PD Interrupts */
#define PD_POW_IRQ_GPIO &gpiof 0

#endif /* PTLRVP_NPCX_BOARD_H_ */
