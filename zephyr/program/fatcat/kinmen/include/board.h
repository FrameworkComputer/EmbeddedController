/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef KINMEN_BOARD_H_
#define KINMEN_BOARD_H_

/* Power Signals */
#define PWR_EN_PP5000_S5 &gpioi 7
#define PWR_RSMRST_PWRGD &gpiof 0
#define PWR_EC_PCH_RSMRST &gpioj 0
#define PWR_SLP_S0 &gpioj 5
#define PWR_PCH_PWROK &gpioj 1
#define PWR_ALL_SYS_PWRGD &gpiod 5

#endif /* KINMEN_BOARD_H_ */
