/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq24738 battery charger driver.
 */

#ifndef __CROS_EC_BQ24738_H
#define __CROS_EC_BQ24738_H

/* Chip specific commands */
#define BQ24738_CHARGE_OPTION           0x12
#define BQ24738_INPUT_CURRENT           0x3f
#define BQ24738_MANUFACTURE_ID          0xfe
#define BQ24738_DEVICE_ID               0xff

/* ChargeOption 0x12 */
#define OPTION_CHARGE_INHIBIT           BIT(0)
#define OPTION_ACOC_THRESHOLD           BIT(1)
#define OPTION_BOOST_MODE_STATE         BIT(2)
#define OPTION_BOOST_MODE_ENABLE        BIT(3)
#define OPTION_ACDET_STATE              BIT(4)
#define OPTION_IOUT_SELECTION           BIT(5)
#define OPTION_LEARN_ENABLE             BIT(6)
#define OPTION_IFAULT_LOW_THRESHOLD     BIT(7)
#define OPTION_IFAULT_HI_ENABLE         BIT(8)
#define OPTION_EMI_FREQ_ENABLE          BIT(9)
#define OPTION_EMI_FREQ_ADJ             BIT(10)
#define OPTION_BAT_DEPLETION_THRESHOLD  (3 << 11)
#define OPTION_WATCHDOG_TIMER           (3 << 13)
#define OPTION_ACPRES_DEGLITCH_TIME     BIT(15)

/* OPTION_ACOC_THRESHOLD */
#define ACOC_THRESHOLD_DISABLE          (0 << 1)
#define ACOC_THRESHOLD_133X             BIT(1)

/* OPTION_IFAULT_LOW_THRESHOLD */
#define IFAULT_LOW_135MV_DEFAULT        (0 << 7)
#define IFAULT_LOW_230MV                BIT(7)

/* OPTION_BAT_DEPLETION_THRESHOLD */
#define FALLING_THRESHOLD_5919          (0 << 11)
#define FALLING_THRESHOLD_6265          BIT(11)
#define FALLING_THRESHOLD_6655          (2 << 11)
#define FALLING_THRESHOLD_7097_DEFAULT  (3 << 11)

/* OPTION_WATCHDOG_TIMER */
#define CHARGE_WATCHDOG_DISABLE         (0 << 13)
#define CHARGE_WATCHDOG_44SEC           BIT(13)
#define CHARGE_WATCHDOG_88SEC           (2 << 13)
#define CHARGE_WATCHDOG_175SEC_DEFAULT  (3 << 13)

/* OPTION_ACPRES_DEGLITCH_TIME */
#define ACPRES_DEGLITCH_150MS           (0 << 15)
#define ACPRES_DEGLITCH_1300MS_DEFAULT  BIT(15)

#endif /* __CROS_EC_BQ24738_H */
