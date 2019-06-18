/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq24707A battery charger driver.
 */

#ifndef __CROS_EC_BQ24707A_H
#define __CROS_EC_BQ24707A_H

/* Chip specific commands */
#define BQ24707_CHARGE_OPTION           0x12
#define BQ24707_INPUT_CURRENT           0x3f
#define BQ24707_MANUFACTURE_ID          0xfe
#define BQ24707_DEVICE_ID               0xff

/* ChargeOption 0x12 */
#define OPTION_CHARGE_INHIBIT           BIT(0)
#define OPTION_ACOC_THRESHOLD           (3 << 1)
#define OPTION_COMPARATOR_THRESHOLD     BIT(4)
#define OPTION_IOUT_SELECTION           BIT(5)
#define OPTION_IFAULT_HI_THRESHOLD      (3 << 7)
#define OPTION_EMI_FREQ_ENABLE          BIT(9)
#define OPTION_EMI_FREQ_ADJ             BIT(10)
#define OPTION_WATCHDOG_TIMER           (3 << 13)
#define OPTION_AOC_DELITCH_TIME         BIT(15)
/* OPTION_ACOC_THRESHOLD */
#define ACOC_THRESHOLD_DISABLE          (0 << 1)
#define ACOC_THRESHOLD_133X             BIT(1)
#define ACOC_THRESHOLD_166X_DEFAULT     (2 << 1)
#define ACOC_THRESHOLD_222X             (3 << 1)
/* OPTION_IFAULT_HI_THRESHOLD */
#define IFAULT_THRESHOLD_300MV          (0 << 7)
#define IFAULT_THRESHOLD_500MV          BIT(7)
#define IFAULT_THRESHOLD_700MV_DEFAULT  (2 << 7)
#define IFAULT_THRESHOLD_900MV          (3 << 7)
/* OPTION_WATCHDOG_TIMER */
#define CHARGE_WATCHDOG_DISABLE         (0 << 13)
#define CHARGE_WATCHDOG_44SEC           BIT(13)
#define CHARGE_WATCHDOG_88SEC           (2 << 13)
#define CHARGE_WATCHDOG_175SEC_DEFAULT  (3 << 13)

#endif /* __CROS_EC_BQ24707A_H */

