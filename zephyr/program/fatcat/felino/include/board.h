/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FELINO_BOARD_H_
#define FELINO_BOARD_H_

/* Power Signals */
#define PWR_EN_PP3300_S5 &gpiof 2
#define PWR_EN_PP5000_S5 &gpioh 3
#define PWR_RSMRST_PWRGD &gpiog 1
#define PWR_EC_PCH_RSMRST &gpioi 6
#define PWR_SLP_S0 &gpioj 5
#define PWR_PCH_PWROK &gpiob 5
#define PWR_ALL_SYS_PWRGD &gpioe 2

#endif /* FELINO_BOARD_H_ */
