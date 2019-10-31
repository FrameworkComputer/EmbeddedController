/* -*- mode:c -*-
 *
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* TODO: pmarheine - use the real values. */

/* Wake Source interrupts */
GPIO_INT(WP_L,                 PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)              /* EC_WP_ODL */
GPIO_INT(AC_PRESENT,		PIN(C, 2), GPIO_INT_BOTH,			extpower_interrupt)
GPIO_INT(POWER_BUTTON_L,	PIN(0, 4), GPIO_INT_BOTH | GPIO_PULL_UP,	power_button_interrupt)	/* MECH_PWR_BTN_ODL */

GPIO_INT(RSMRST_L_PGOOD,       PIN(E, 2), GPIO_INT_BOTH, intel_x86_rsmrst_signal_interrupt)
GPIO_INT(PCH_SLP_S0_L,             PIN(D, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PCH_SLP_S3_L,             PIN(A, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PCH_SLP_S4_L,             PIN(D, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_EC_ALL_SYS_PWRGD,  PIN(F, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PP5000_A_PG_OD,       PIN(D, 7), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(RECOVERY_L,		PIN(2, 4), GPIO_INT_BOTH, button_interrupt)	/* H1_EC_RECOVERY_BTN_ODL */

GPIO(SYS_RESET_L,              PIN(0, 2), GPIO_ODR_HIGH)        /* SYS_RST_ODL */
GPIO(PCH_WAKE_L,               PIN(7, 4), GPIO_ODR_HIGH)        /* EC_PCH_WAKE_ODL */
GPIO(PCH_PWRBTN_L,             PIN(C, 1), GPIO_ODR_HIGH)        /* EC_PCH_PWR_BTN_ODL */
GPIO(PCH_RSMRST_L,          PIN(A, 6), GPIO_OUT_LOW)
GPIO(PCH_RTCRST,		PIN(7, 6), GPIO_ODR_HIGH)	/* EC_PCH_RTCRST */
GPIO(ENTERING_RW,              PIN(E, 3), GPIO_OUT_LOW)         /* EC_ENTERING_RW */
GPIO(CPU_PROCHOT,           PIN(6, 3), GPIO_ODR_HIGH)
GPIO(EN_PP5000,           PIN(6, 4), GPIO_ODR_HIGH)
GPIO(EN_PP5000_A,           PIN(6, 5), GPIO_ODR_HIGH)
GPIO(PCH_SYS_PWROK,         PIN(3, 7), GPIO_OUT_LOW)
GPIO(EN_A_RAILS,               PIN(A, 3), GPIO_OUT_LOW)
GPIO(FAN_PWM_1,                PIN(B, 6), GPIO_OUT_LOW)

/* I2C pins - Alternate function below configures I2C module on these pins */
GPIO(I2C0_SCL,                 PIN(B, 5), GPIO_INPUT)           /* EC_I2C_INA_SCL */
GPIO(I2C0_SDA,                 PIN(B, 4), GPIO_INPUT)           /* EC_I2C_INA_SDA */
GPIO(I2C1_SCL,                 PIN(9, 0), GPIO_INPUT)           /* EC_I2C_USB_C0_TCPPC_SCL */
GPIO(I2C1_SDA,                 PIN(8, 7), GPIO_INPUT)           /* EC_I2C_USB_C0_TCPPC_SDA */
GPIO(I2C3_SCL,                 PIN(D, 1), GPIO_INPUT)           /* EC_I2C_USB_C0_TCPC_SCL */
GPIO(I2C3_SDA,                 PIN(D, 0), GPIO_INPUT)           /* EC_I2C_USB_C0_TCPC_SDA */
GPIO(I2C5_SCL,                 PIN(3, 3), GPIO_INPUT)           /* EC_I2C_IMVP8_SCL */
GPIO(I2C5_SDA,                 PIN(3, 6), GPIO_INPUT)           /* EC_I2C_IMVP8_SDA */
GPIO(I2C7_SCL,                 PIN(B, 3), GPIO_INPUT)           /* EC_I2C_EEPROM_SCL */
GPIO(I2C7_SDA,                 PIN(B, 2), GPIO_INPUT)           /* EC_I2C_EEPROM_SDA */

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(B, 0x30), 0, MODULE_I2C, 0)                             /* I2C0 */
ALTERNATE(PIN_MASK(9, 0x07), 0, MODULE_I2C, 0)                             /* I2C1 SCL / I2C2 */
ALTERNATE(PIN_MASK(8, 0x80), 0, MODULE_I2C, 0)                             /* I2C1 SDA */
ALTERNATE(PIN_MASK(D, 0x03), 0, MODULE_I2C, 0)                             /* I2C3 */
ALTERNATE(PIN_MASK(3, 0x48), 0, MODULE_I2C, 0)                             /* I2C5 */
ALTERNATE(PIN_MASK(B, 0x0C), 0, MODULE_I2C, 0)                             /* I2C7 */

/* PWM */
ALTERNATE(PIN_MASK(B, 0x80), 0, MODULE_PWM, 0)			/* PWM5 - Fan 1 */
