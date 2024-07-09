/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PTLRVP_MCHP_BOARD_H_
#define PTLRVP_MCHP_BOARD_H_

/* Power Signals */
#define PWR_EN_PP3300_S5 MCHP_GPIO_DECODE_226
#define PWR_EN_PP5000_S5 MCHP_GPIO_DECODE_244
#define PWR_RSMRST_PWRGD MCHP_GPIO_DECODE_012
#define PWR_EC_PCH_RSMRST MCHP_GPIO_DECODE_054
#define PWR_SLP_S0 MCHP_GPIO_DECODE_052
#define PWR_PCH_PWROK MCHP_GPIO_DECODE_100
#define PWR_EC_PCH_SYS_PWROK MCHP_GPIO_DECODE_015
#define PWR_ALL_SYS_PWRGD MCHP_GPIO_DECODE_057

/* I2C Ports */
#define CHARGER_I2C i2c_smb_0

#define PD_POW_I2C i2c_smb_1

/* PD Interrupts */
#define PD_POW_IRQ_GPIO MCHP_GPIO_DECODE_143

#endif /* PTLRVP_MCHP_BOARD_H_ */
