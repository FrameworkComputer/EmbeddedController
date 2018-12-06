/* -*- mode:c -*-
 *
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Wake Source interrupts */
GPIO_INT(LID_OPEN,             PIN(D, 2), GPIO_INT_BOTH, lid_interrupt)
GPIO_INT(WP_L,                 PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)              /* EC_WP_ODL */
GPIO_INT(POWER_BUTTON_L,       PIN(0, 1), GPIO_INT_BOTH, power_button_interrupt)        /* MECH_PWR_BTN_ODL */


GPIO(SYS_RESET_L,              PIN(0, 2), GPIO_ODR_HIGH)        /* SYS_RST_ODL */
GPIO(ENTERING_RW,              PIN(E, 3), GPIO_OUT_LOW)         /* EC_ENTERING_RW */
GPIO(PCH_WAKE_L,               PIN(7, 4), GPIO_ODR_HIGH)        /* EC_PCH_WAKE_ODL */
GPIO(PCH_PWRBTN_L,             PIN(C, 1), GPIO_ODR_HIGH)        /* EC_PCH_PWR_BTN_ODL */

/* I2C pins - Alternate function below configures I2C module on these pins */
GPIO(I2C0_SCL,                 PIN(B, 5), GPIO_INPUT |
                                          GPIO_SEL_1P8V)        /* EC_I2C_SENSOR_1V8_SCL */
GPIO(I2C0_SDA,                 PIN(B, 4), GPIO_INPUT |
                                          GPIO_SEL_1P8V)        /* EC_I2C_SENSOR_1V8_SDA */
GPIO(I2C1_SCL,                 PIN(9, 0), GPIO_INPUT)           /* EC_I2C_USB_C0_PD_SCL */
GPIO(I2C1_SDA,                 PIN(8, 7), GPIO_INPUT)           /* EC_I2C_USB_C0_PD_SDA */
GPIO(I2C2_SCL,                 PIN(9, 2), GPIO_INPUT)           /* EC_I2C_USB_C1_PD_SCL */
GPIO(I2C2_SDA,                 PIN(9, 1), GPIO_INPUT)           /* EC_I2C_USB_C1_PD_SDA */
GPIO(I2C3_SCL,                 PIN(D, 1), GPIO_INPUT)           /* EC_I2C_USB_C0_TCPC_SDA */
GPIO(I2C3_SDA,                 PIN(D, 0), GPIO_INPUT)           /* EC_I2C_USB_C0_TCPC_SCL */
GPIO(I2C5_SCL,                 PIN(3, 3), GPIO_INPUT)           /* EC_I2C_POWER_SCL */
GPIO(I2C5_SDA,                 PIN(3, 6), GPIO_INPUT)           /* EC_I2C_POWER_SDA */
GPIO(I2C7_SCL,                 PIN(B, 3), GPIO_INPUT)           /* EC_I2C_EEPROM_SCL */
GPIO(I2C7_SDA,                 PIN(B, 2), GPIO_INPUT)           /* EC_I2C_EEPROM_SDA */

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(B, 0x30), 0, MODULE_I2C, (GPIO_INPUT | GPIO_SEL_1P8V))  /* I2C0 1.8V */
ALTERNATE(PIN_MASK(9, 0x07), 0, MODULE_I2C, 0)                             /* I2C1 SCL / I2C2 */
ALTERNATE(PIN_MASK(8, 0x80), 0, MODULE_I2C, 0)                             /* I2C1 SDA */
ALTERNATE(PIN_MASK(D, 0x03), 0, MODULE_I2C, 0)                             /* I2C3 */
ALTERNATE(PIN_MASK(3, 0x48), 0, MODULE_I2C, 0)                             /* I2C5 */
ALTERNATE(PIN_MASK(B, 0x0C), 0, MODULE_I2C, 0)                             /* I2C7 */
