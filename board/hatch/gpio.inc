/* -*- mode:c -*-
 *
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Wake Source interrupts */
GPIO_INT(LID_OPEN,             PIN(D, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, lid_interrupt)
GPIO_INT(WP_L,                 PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)              /* EC_WP_ODL */
GPIO_INT(POWER_BUTTON_L,       PIN(0, 1), GPIO_INT_BOTH, power_button_interrupt)        /* MECH_PWR_BTN_ODL */
GPIO_INT(ACOK_OD,              PIN(0, 0), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, extpower_interrupt)

/* Power sequencing interrupts */
GPIO_INT(SLP_S0_L,             PIN(D, 5), GPIO_INT_BOTH, power_signal_interrupt)
#ifndef CONFIG_HOSTCMD_ESPI_VW_SLP_S3
GPIO_INT(SLP_S3_L,             PIN(A, 5), GPIO_INT_BOTH, power_signal_interrupt)
#endif
#ifndef CONFIG_HOSTCMD_ESPI_VW_SLP_S4
GPIO_INT(SLP_S4_L,             PIN(D, 4), GPIO_INT_BOTH, power_signal_interrupt)
#endif
GPIO_INT(PG_EC_RSMRST_L,       PIN(E, 2), GPIO_INT_BOTH, intel_x86_rsmrst_signal_interrupt)
GPIO_INT(PG_EC_ALL_SYS_PWRGD,  PIN(F, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PP5000_A_PG_OD,       PIN(D, 7), GPIO_INT_BOTH, power_signal_interrupt)

/* Sensor Interrupts */
GPIO_INT(BASE_SIXAXIS_INT_L,   PIN(5, 6), GPIO_INT_FALLING, bmi160_interrupt)
GPIO_INT(TCS3400_INT_ODL,      PIN(F, 1), GPIO_INT_FALLING, tcs3400_interrupt)

/* USB-C interrupts */
GPIO_INT(USB_C0_PPC_INT_ODL,   PIN(E, 0), GPIO_INT_FALLING, ppc_interrupt)
GPIO_INT(USB_C1_PPC_INT_ODL,   PIN(A, 2), GPIO_INT_FALLING, ppc_interrupt)
GPIO_INT(USB_C0_TCPC_INT_ODL,  PIN(6, 2), GPIO_INT_FALLING, tcpc_alert_event)
GPIO_INT(USB_C1_TCPC_INT_ODL,  PIN(F, 5), GPIO_INT_FALLING, tcpc_alert_event)
GPIO_INT(USB_C0_BC12_INT_ODL,  PIN(9, 5), GPIO_INT_FALLING, bc12_interrupt)
GPIO_INT(USB_C1_BC12_INT_ODL,  PIN(E, 4), GPIO_INT_FALLING, bc12_interrupt)

GPIO_INT(HDMI_CONN_HPD,        PIN(7, 2), GPIO_INT_BOTH, hdmi_hpd_interrupt)

/* Volume button interrupts */
GPIO_INT(EC_VOLDN_BTN_ODL,     PIN(9, 3), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(EC_VOLUP_BTN_ODL,     PIN(7, 5), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)

GPIO(SYS_RESET_L,              PIN(0, 2), GPIO_ODR_HIGH)        /* SYS_RST_ODL */
GPIO(ENTERING_RW,              PIN(E, 3), GPIO_OUT_LOW)         /* EC_ENTERING_RW */
GPIO(PCH_WAKE_L,               PIN(7, 4), GPIO_ODR_HIGH)        /* EC_PCH_WAKE_ODL */
GPIO(PCH_PWRBTN_L,             PIN(C, 1), GPIO_ODR_HIGH)        /* EC_PCH_PWR_BTN_ODL */

/* Power Sequencing Signals */
GPIO(EN_PP5000_A_V1,           PIN(A, 4), GPIO_DEFAULT)
GPIO(EN_PP5000_A_V0,           PIN(7, 3), GPIO_DEFAULT)
GPIO(EN_A_RAILS,               PIN(A, 3), GPIO_OUT_LOW)
GPIO(EC_PCH_RSMRST_L,          PIN(A, 6), GPIO_OUT_LOW)
GPIO(EC_PROCHOT_ODL,           PIN(6, 3), GPIO_ODR_HIGH)
GPIO(EC_PROCHOT_IN_OD,         PIN(3, 4), GPIO_INPUT)
GPIO(EC_PCH_SYS_PWROK,         PIN(3, 7), GPIO_OUT_LOW)
GPIO(CPU_C10_GATE_L,           PIN(6, 7), GPIO_INPUT)

/* MKBP event synchronization */
GPIO(EC_INT_L,                 PIN(7, 0), GPIO_ODR_HIGH)

/* USB and USBC Signals */
GPIO(USB_C_OC_ODL,             PIN(B, 1), GPIO_ODR_HIGH)
GPIO(USB_C0_TCPC_RST,          PIN(9, 7), GPIO_OUT_LOW)
GPIO(USB_C1_TCPC_RST_ODL,      PIN(3, 2), GPIO_ODR_HIGH)
GPIO(EN_USB_A_5V,              PIN(3, 5), GPIO_OUT_LOW)
GPIO(EN_USB_A_LOW_PWR_OD,      PIN(9, 4), GPIO_OUT_LOW)

/* Misc Signals */
GPIO(EC_BATT_PRES_ODL,         PIN(E, 1), GPIO_INPUT)
GPIO(LED_1_L,                  PIN(C, 4), GPIO_OUT_HIGH)        /* Yellow (hatch) */
GPIO(LED_2_L,                  PIN(C, 3), GPIO_OUT_HIGH)        /* White (hatch) */
GPIO(LED_3_L,                  PIN(C, 2), GPIO_OUT_HIGH)
GPIO(LED_4_L,                  PIN(6, 0), GPIO_OUT_HIGH)
GPIO(EC_KB_BL_EN,              PIN(8, 6), GPIO_OUT_LOW)         /* Keyboard backlight */
GPIO(EDP_BKLTEN_OD,            PIN(D, 3), GPIO_ODR_HIGH)        /* Display backlight */
GPIO(EN_PP5000_FAN,            PIN(6, 1), GPIO_OUT_LOW)
GPIO(EN_MST,                   PIN(9, 6), GPIO_OUT_LOW)


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

/* Keyboard pins */
#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
GPIO(KBD_KSO2,                 PIN(1, 7), GPIO_OUT_LOW)        /* KSO_02 inverted */
ALTERNATE(PIN_MASK(3, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)       /* KSI_00-01 */
ALTERNATE(PIN_MASK(2, 0xFC), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)       /* KSI_02-07 */
ALTERNATE(PIN_MASK(2, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH)       /* KSO_00-01 */
ALTERNATE(PIN_MASK(1, 0x7F), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH)       /* KSO_03-09 */
ALTERNATE(PIN_MASK(0, 0xF0), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH)       /* KSO_10-13 */
ALTERNATE(PIN_MASK(8, 0x04), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH)       /* KSO_14 */

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(B, 0x30), 0, MODULE_I2C, (GPIO_INPUT | GPIO_SEL_1P8V))  /* I2C0 1.8V */
ALTERNATE(PIN_MASK(9, 0x07), 0, MODULE_I2C, 0)                             /* I2C1 SCL / I2C2 */
ALTERNATE(PIN_MASK(8, 0x80), 0, MODULE_I2C, 0)                             /* I2C1 SDA */
ALTERNATE(PIN_MASK(D, 0x03), 0, MODULE_I2C, 0)                             /* I2C3 */
ALTERNATE(PIN_MASK(3, 0x48), 0, MODULE_I2C, 0)                             /* I2C5 */
ALTERNATE(PIN_MASK(B, 0x0C), 0, MODULE_I2C, 0)                             /* I2C7 */

/* UART */
ALTERNATE(PIN_MASK(6, 0x30), 0, MODULE_UART, 0)                            /* UART from EC to Servo */

/* PWM */
ALTERNATE(PIN_MASK(8, 0x01), 0, MODULE_PWM, 0)                             /* PWM3 - Keyboard backlight */
ALTERNATE(PIN_MASK(B, 0x80), 0, MODULE_PWM, 0)                             /* PWM5 - FAN */
ALTERNATE(PIN_MASK(4, 0x01), 0, MODULE_PWM, 0)                             /* TA1 - Fan Tachometer */

/* ADC */
ALTERNATE(PIN_MASK(4, 0x30), 0, MODULE_ADC, 0)                             /* ADC0-1 */

/* Power Switch Logic (PSL) inputs */
ALTERNATE(PIN_MASK(D, 0x04), 0, MODULE_PMU, 0)                             /* GPIOD2 = LID_OPEN */
ALTERNATE(PIN_MASK(0, 0x07), 0, MODULE_PMU, 0)                             /* GPIO00 = ACOK_OD,
                                                                              GPIO01 = MECH_PWR_BTN_ODL
                                                                              GPIO02 = EC_RST_ODL */
