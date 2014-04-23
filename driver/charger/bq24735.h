/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq24735 battery charger driver.
 */

#ifndef __CROS_EC_CHARGER_BQ24735_H
#define __CROS_EC_CHARGER_BQ24735_H

/* Chip specific commands */
#define BQ24735_CHARGE_OPTION           0x12
#define BQ24735_INPUT_CURRENT           0x3f
#define BQ24735_MANUFACTURE_ID          0xfe
#define BQ24735_DEVICE_ID               0xff

/* ChargeOption 0x12 */
#define OPTION_CHARGE_INHIBIT           (1 << 0)
#define OPTION_ACOC_THRESHOLD           (1 << 1)
#define OPTION_BOOST_MODE_STATE         (1 << 2)
#define OPTION_BOOST_MODE_ENABLE        (1 << 3)
#define OPTION_ACDET_STATE              (1 << 4)
#define OPTION_IOUT_SELECTION           (1 << 5)
#define OPTION_LEARN_ENABLE             (1 << 6)
#define OPTION_IFAULT_LOW_THRESHOLD     (1 << 7)
#define OPTION_IFAULT_HI_ENABLE         (1 << 8)
#define OPTION_EMI_FREQ_ENABLE          (1 << 9)
#define OPTION_EMI_FREQ_ADJ             (1 << 10)
#define OPTION_BAT_DEPLETION_THRESHOLD  (3 << 11)
#define OPTION_WATCHDOG_TIMER           (3 << 13)
#define OPTION_ACPRES_DEGLITCH_TIME     (1 << 15)

/* OPTION_ACOC_THRESHOLD */
#define ACOC_THRESHOLD_DISABLE          (0 << 1)
#define ACOC_THRESHOLD_133X             (1 << 1)

/* OPTION_IFAULT_LOW_THRESHOLD */
#define IFAULT_LOW_135MV_DEFAULT        (0 << 7)
#define IFAULT_LOW_230MV                (1 << 7)

/* OPTION_BAT_DEPLETION_THRESHOLD */
#define FALLING_THRESHOLD_5919          (0 << 11)
#define FALLING_THRESHOLD_6265          (1 << 11)
#define FALLING_THRESHOLD_6655          (2 << 11)
#define FALLING_THRESHOLD_7097_DEFAULT  (3 << 11)

/* OPTION_WATCHDOG_TIMER */
#define CHARGE_WATCHDOG_DISABLE         (0 << 13)
#define CHARGE_WATCHDOG_44SEC           (1 << 13)
#define CHARGE_WATCHDOG_88SEC           (2 << 13)
#define CHARGE_WATCHDOG_175SEC_DEFAULT  (3 << 13)

/* OPTION_ACPRES_DEGLITCH_TIME */
#define ACPRES_DEGLITCH_150MS           (0 << 15)
#define ACPRES_DEGLITCH_1300MS_DEFAULT  (1 << 15)

#endif /* __CROS_EC_CHARGER_BQ24735_H */
