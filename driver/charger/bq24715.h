/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq24715 battery charger driver.
 */

#ifndef __CROS_EC_BQ24715_H
#define __CROS_EC_BQ24715_H

/* NOTES:
 * If battery is not present keep charge current register (0x14) at 0.
 * Max charge voltage (0x15) needs to be programmed before 0x14.
 */

/* Chip specific registers */
#define BQ24715_CHARGE_OPTION           0x12
#define BQ24715_CHARGE_CURRENT          0x14
#define BQ24715_MAX_CHARGE_VOLTAGE      0x15
#define BQ24715_MIN_SYSTEM_VOLTAGE      0x3e
#define BQ24715_INPUT_CURRENT           0x3f
#define BQ24715_MANUFACTURER_ID         0xfe
#define BQ24715_DEVICE_ID               0xff

/* ChargeOption Register - 0x12 */
#define OPT_LOWPOWER_MASK               BIT(15)
#define   OPT_LOWPOWER_DSCHRG_I_MON_ON    (0 << 15)
#define   OPT_LOWPOWER_DSCHRG_I_MON_OFF   BIT(15)
#define OPT_WATCHDOG_MASK               (3 << 13)
#define   OPT_WATCHDOG_DISABLE            (0 << 13)
#define   OPT_WATCHDOG_44SEC              BIT(13)
#define   OPT_WATCHDOG_88SEC              (2 << 13)
#define   OPT_WATCHDOG_175SEC             (3 << 13)
#define OPT_SYSOVP_MASK                 BIT(12)
#define   OPT_SYSOVP_15P1_3SEC_10P1_2SEC  (0 << 12)
#define   OPT_SYSOVP_17P0_3SEC_11P3_2SEC  BIT(12)
#define OPT_SYSOVP_STATUS_MASK          BIT(11)
#define   OPT_SYSOVP_STATUS               BIT(11)
#define OPT_AUDIO_FREQ_LIMIT_MASK       BIT(10)
#define   OPT_AUDIO_FREQ_NO_LIMIT         (0 << 10)
#define   OPT_AUDIO_FREQ_40KHZ_LIMIT      BIT(10)
#define OPT_SWITCH_FREQ_MASK            (3 << 8)
#define   OPT_SWITCH_FREQ_600KHZ          (0 << 8)
#define   OPT_SWITCH_FREQ_800KHZ          BIT(8)
#define   OPT_SWITCH_FREQ_1MHZ            (2 << 8)
#define   OPT_SWITCH_FREQ_800KHZ_DUP      (3 << 8)
#define OPT_ACOC_MASK                   BIT(7)
#define   OPT_ACOC_DISABLED               (0 << 7)
#define   OPT_ACOC_333PCT_IPDM            BIT(7)
#define OPT_LSFET_OCP_MASK              BIT(6)
#define   OPT_LSFET_OCP_250MV             (0 << 6)
#define   OPT_LSFET_OCP_350MV             BIT(6)
#define OPT_LEARN_MASK                  BIT(5)
#define   OPT_LEARN_DISABLE               (0 << 5)
#define   OPT_LEARN_ENABLE                BIT(5)
#define OPT_IOUT_MASK                   BIT(4)
#define   OPT_IOUT_40X                    (0 << 4)
#define   OPT_IOUT_16X                    BIT(4)
#define OPT_FIX_IOUT_MASK               BIT(3)
#define   OPT_FIX_IOUT_IDPM_EN            (0 << 3)
#define   OPT_FIX_IOUT_ALWAYS             BIT(3)
#define OPT_LDO_MODE_MASK               BIT(2)
#define   OPT_LDO_DISABLE                 (0 << 2)
#define   OPT_LDO_ENABLE                  BIT(2)
#define OPT_IDPM_MASK                   BIT(1)
#define   OPT_IDPM_DISABLE                (0 << 1)
#define   OPT_IDPM_ENABLE                 BIT(1)
#define OPT_CHARGE_INHIBIT_MASK         BIT(0)
#define   OPT_CHARGE_ENABLE               (0 << 0)
#define   OPT_CHARGE_DISABLE              BIT(0)


/* ChargeCurrent Register - 0x14
 * The ChargeCurrent register controls a DAC. Therefore
 * the below definitions are cummulative. */
#define CHARGE_I_64MA                   BIT(6)
#define CHARGE_I_128MA                  BIT(7)
#define CHARGE_I_256MA                  BIT(8)
#define CHARGE_I_512MA                  BIT(9)
#define CHARGE_I_1024MA                 BIT(10)
#define CHARGE_I_2048MA                 BIT(11)
#define CHARGE_I_4096MA                 BIT(12)
#define CHARGE_I_OFF                    (0)
#define CHARGE_I_MIN                    (128)
#define CHARGE_I_MAX                    (8128)
#define CHARGE_I_STEP                   (64)

/* MaxChargeVoltage Register - 0x15
 * The MaxChargeVoltage register controls a DAC. Therefore
 * the below definitions are cummulative. */
#define CHARGE_V_16MV                   BIT(4)
#define CHARGE_V_32MV                   BIT(5)
#define CHARGE_V_64MV                   BIT(6)
#define CHARGE_V_128MV                  BIT(7)
#define CHARGE_V_256MV                  BIT(8)
#define CHARGE_V_512MV                  BIT(9)
#define CHARGE_V_1024MV                 BIT(10)
#define CHARGE_V_2048MV                 BIT(11)
#define CHARGE_V_4096MV                 BIT(12)
#define CHARGE_V_8192MV                 BIT(13)
#define CHARGE_V_MIN                    (4096)
#define CHARGE_V_MAX                    (0x3ff0)
#define CHARGE_V_STEP                   (16)

/* MinSystemVoltage Register - 0x3e
 * The MinSystemVoltage register controls a DAC. Therefore
 * the below definitions are cummulative. */
#define MIN_SYS_V_256MV                 BIT(8)
#define MIN_SYS_V_512MV                 BIT(9)
#define MIN_SYS_V_1024MV                BIT(10)
#define MIN_SYS_V_2048MV                BIT(11)
#define MIN_SYS_V_4096MV                BIT(12)
#define MIN_SYS_V_8192MV                BIT(13)
#define MIN_SYS_V_MIN                   (4096)

/* InputCurrent Register - 0x3f
 * The InputCurrent register controls a DAC. Therefore
 * the below definitions are cummulative. */
#define INPUT_I_64MA                   BIT(6)
#define INPUT_I_128MA                  BIT(7)
#define INPUT_I_256MA                  BIT(8)
#define INPUT_I_512MA                  BIT(9)
#define INPUT_I_1024MA                 BIT(10)
#define INPUT_I_2048MA                 BIT(11)
#define INPUT_I_4096MA                 BIT(12)
#define INPUT_I_MIN                    (128)
#define INPUT_I_MAX                    (8064)
#define INPUT_I_STEP                   (64)

extern const struct charger_drv bq24715_drv;

#endif /* __CROS_EC_BQ24715_H */
