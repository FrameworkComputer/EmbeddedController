/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PTLRVP_NPCX_BOARD_H_
#define PTLRVP_NPCX_BOARD_H_

/* Power Signals */
#define PWR_EN_PP3300_S5 &gpio4 1
#define PWR_EN_PP5000_S5 &gpio7 6
#define PWR_RSMRST_PWRGD &gpio7 2
#define PWR_EC_PCH_RSMRST &gpio9 5
#define PWR_SLP_S0 &gpiob 0
#define PWR_PCH_PWROK &gpio6 1
#define PWR_ALL_SYS_PWRGD &gpio6 3

/* PD Interrupts */
#define PD_POW_IRQ_GPIO &gpiof 0

#endif /* PTLRVP_NPCX_BOARD_H_ */
