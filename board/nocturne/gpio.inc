/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first.
 */

/* USB-C interrupts */
GPIO_INT(USB_C0_PD_INT_ODL,  PIN(6, 1), GPIO_INT_FALLING | GPIO_PULL_UP, usb_c_interrupt)
GPIO_INT(USB_C1_PD_INT_ODL,  PIN(F, 5), GPIO_INT_FALLING | GPIO_PULL_UP, usb_c_interrupt)

/* Power Sequencing interrupts */
GPIO_INT(ROP_DSW_PWROK_EC,   PIN(F, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(ROP_EC_RSMRST_L,    PIN(E, 2), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(EC_PWR_BTN_IN_ODL,  PIN(0, 1), GPIO_INT_BOTH | GPIO_PULL_UP, power_button_interrupt)
GPIO_INT(SLP_S0_L,           PIN(A, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_S4_L,           PIN(A, 3), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_S3_L,           PIN(A, 6), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_SUS_L_PCH,      PIN(D, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(ACOK_OD,            PIN(0, 0), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, extpower_interrupt)
GPIO_INT(ROP_INT_L,          PIN(D, 5), GPIO_INT_BOTH | GPIO_PULL_UP, power_signal_interrupt)

/* Misc. interrupts */
GPIO_INT(H1_EC_VOL_DOWN_ODL, PIN(6, 3), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(H1_EC_VOL_UP_ODL,   PIN(7, 5), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(EC_WP_L,            PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)
GPIO_INT(LID_OPEN,           PIN(D, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, lid_interrupt)
GPIO_INT(ACCELGYRO3_INT_L,   PIN(4, 1), GPIO_INT_FALLING, bmi160_interrupt)
GPIO_INT(BASE_USB_FAULT_ODL, PIN(2, 3), GPIO_INT_FALLING, base_pwr_fault_interrupt)
GPIO_INT(BASE_PWR_FAULT_ODL, PIN(2, 4), GPIO_INT_FALLING, base_pwr_fault_interrupt)
GPIO_INT(RCAM_VSYNC,         PIN(E, 4), GPIO_INT_RISING, sync_interrupt)
GPIO_INT(CCD_MODE_ODL,       PIN(E, 3), GPIO_INT_FALLING, board_connect_c0_sbu)

/* SoC */
GPIO(RSMRST_L,               PIN(C, 2), GPIO_OUT_LOW)
GPIO(EC_PCH_PWR_BTN_L,       PIN(C, 1), GPIO_OUT_HIGH)
GPIO(EC_PCH_RTCRST,          PIN(7, 6), GPIO_OUT_LOW)
GPIO(SLP_SUS_L_PMIC,         PIN(D, 7), GPIO_OUT_LOW)
GPIO(EC_PCH_WAKE_L,          PIN(7, 4), GPIO_ODR_HIGH | GPIO_PULL_UP)
GPIO(EC_PROCHOT_ODL,         PIN(3, 4), GPIO_ODR_HIGH | GPIO_SEL_1P8V)
GPIO(SYS_RESET_L,            PIN(0, 2), GPIO_ODR_HIGH)
GPIO(USB_C0_DP_HPD,          PIN(C, 5), GPIO_OUT_LOW)
GPIO(USB_C1_DP_HPD,          PIN(C, 6), GPIO_OUT_LOW)

/* Power Sequencing */
GPIO(EC_PCH_ACPRESENT,       PIN(7, 3), GPIO_OUT_LOW)

/* USB-C */
GPIO(USB_C0_OC_ODL,          PIN(6, 7), GPIO_ODR_HIGH)
GPIO(USB_C1_OC_ODL,          PIN(7, 0), GPIO_ODR_HIGH)
GPIO(EN_5V,                  PIN(0, 3), GPIO_OUT_LOW)
GPIO(EN_USB_C0_3A,           PIN(6, 2), GPIO_OUT_LOW)
GPIO(EN_USB_C1_3A,           PIN(8, 3), GPIO_OUT_LOW)

/* Misc */
GPIO(EC_BRD_ID0,             PIN(4, 0), GPIO_INPUT)
GPIO(EC_BRD_ID1,             PIN(9, 6), GPIO_INPUT)
GPIO(EC_BRD_ID2,             PIN(9, 3), GPIO_INPUT)
GPIO(EC_BRD_ID3,             PIN(F, 0), GPIO_INPUT)
GPIO(EC_INT_L,               PIN(9, 5), GPIO_OUT_HIGH)
GPIO(UHALL_PWR_EN,           PIN(E, 0), GPIO_OUT_HIGH)
GPIO(USB2_VBUSSENSE,         PIN(A, 2), GPIO_OUT_LOW)
GPIO(USB2_ID,                PIN(A, 0), GPIO_OUT_LOW)
GPIO(USB_PD_RST_L,           PIN(F, 1), GPIO_OUT_HIGH)
GPIO(ALS_INT_L,              PIN(5, 0), GPIO_INPUT)
GPIO(EC_BATT_PRES_L,         PIN(E, 5), GPIO_INPUT)
GPIO(EC_ENTERING_RW,         PIN(E, 1), GPIO_OUT_LOW)
GPIO(EC_BL_DISABLE_ODL,      PIN(D, 3), GPIO_ODR_HIGH)
GPIO(EC_PLATFORM_RST,        PIN(8, 6), GPIO_OUT_LOW)
GPIO(EC_GPIO31,              PIN(3, 1), GPIO_OUT_LOW)
GPIO(BASE_PWR_EN,            PIN(2, 2), GPIO_OUT_LOW)
GPIO(PP3300_NVME_EN,         PIN(2, 1), GPIO_INPUT | GPIO_PULL_DOWN) /*NC*/
GPIO(PP1800_NVME_EN,         PIN(2, 0), GPIO_INPUT | GPIO_PULL_DOWN) /*NC*/
GPIO(PPVAR_NVME_CORE_EN,     PIN(1, 7), GPIO_INPUT | GPIO_PULL_DOWN) /*NC*/
GPIO(EC_GPIO16,              PIN(1, 6), GPIO_OUT_LOW)
GPIO(EC_GPIO15,              PIN(1, 5), GPIO_OUT_LOW)
GPIO(EC_GPIO14,              PIN(1, 4), GPIO_OUT_LOW)

/* I2C pins */
GPIO(EC_I2C1_USB_C0_SCL,     PIN(9, 0), GPIO_INPUT)
GPIO(EC_I2C1_USB_C0_SDA,     PIN(8, 7), GPIO_INPUT)
GPIO(EC_I2C2_USB_C1_SCL,     PIN(9, 2), GPIO_INPUT)
GPIO(EC_I2C2_USB_C1_SDA,     PIN(9, 1), GPIO_INPUT)
GPIO(EC_I2C5_ALS_GYRO_SCL,   PIN(3, 3), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C5_ALS_GYRO_SDA,   PIN(3, 6), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C0_POWER_SCL,      PIN(B, 5), GPIO_INPUT)
GPIO(EC_I2C0_POWER_SDA,      PIN(B, 4), GPIO_INPUT)
GPIO(EC_I2C4_BATTERY_SCL,    PIN(F, 3), GPIO_INPUT)
GPIO(EC_I2C4_BATTERY_SDA,    PIN(F, 2), GPIO_INPUT)

/* Strap pins */
GPIO(GPO66_NC,       PIN(6, 6), GPIO_INPUT | GPIO_PULL_UP)
GPIO(GPOB6_NC,       PIN(B, 6), GPIO_INPUT | GPIO_PULL_UP)

/* Alternate mode configuration */
/* UART pins */
ALTERNATE(PIN_MASK(6, 0x30), 0, MODULE_UART, 0) /* Cr50 requires no pullups. */
/* I2C ports */
ALTERNATE(PIN_MASK(B, 0x30), 0, MODULE_I2C, 0) /* I2C0 */
ALTERNATE(PIN_MASK(8, 0x80), 0, MODULE_I2C, 0) /* I2C1 SDA */
ALTERNATE(PIN_MASK(9, 0x07), 0, MODULE_I2C, 0) /* I2C1 SCL, I2C 2 */
ALTERNATE(PIN_MASK(F, 0x0C), 0, MODULE_I2C, 0) /* I2C4 */
ALTERNATE(PIN_MASK(3, 0x48), 0, MODULE_I2C, GPIO_INPUT | GPIO_SEL_1P8V) /* 1.8V I2C5 */

/* PWM */
ALTERNATE(PIN_MASK(6, 0x01), 0, MODULE_PWM, 0) /* PWM7 */
ALTERNATE(PIN_MASK(8, 0x01), 0, MODULE_PWM, 0) /* PWM3 */
ALTERNATE(PIN_MASK(B, 0x80), 0, MODULE_PWM, 0) /* PWM5 */
ALTERNATE(PIN_MASK(C, 0x19), 0, MODULE_PWM, 0) /* PWM0,2, 6 */

/* ADC */
ALTERNATE(PIN_MASK(4, 0x30), 0, MODULE_ADC, 0) /* ADC0,1 */

/* Power Switch Logic (PSL) inputs */
ALTERNATE(PIN_MASK(0, 0x03), 0, MODULE_PMU, 0) /* GPIO00, GPIO01 */
ALTERNATE(PIN_MASK(D, 0x04), 0, MODULE_PMU, 0) /* GPIOD2 */
