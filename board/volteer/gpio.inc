/* -*- mode:c -*-
 *
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Wake Source interrupts */
GPIO_INT(EC_LID_OPEN,          PIN(D, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, lid_interrupt)
GPIO_INT(EC_WP_L,              PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)
GPIO_INT(H1_EC_PWR_BTN_ODL,    PIN(0, 1), GPIO_INT_BOTH, power_button_interrupt)
GPIO_INT(ACOK_OD,              PIN(0, 0), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, extpower_interrupt)

/* Power sequencing interrupts */
GPIO_INT(SLP_S0_L,             PIN(D, 5), GPIO_INT_BOTH, power_signal_interrupt)
#ifndef CONFIG_HOSTCMD_ESPI_VW_SLP_S3
GPIO_INT(SLP_S3_L,             PIN(A, 5), GPIO_INT_BOTH, power_signal_interrupt)
#endif
GPIO_INT(SLP_SUS_L,            PIN(D, 7), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_EC_RSMRST_ODL,     PIN(E, 2), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(DSW_PWROK,            PIN(C, 7), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_EC_ALL_SYS_PWRGD,  PIN(F, 4),  GPIO_INT_BOTH, power_signal_interrupt)

/* Sensor Interrupts */

/* USB-C interrupts */

/* HDMI interrupts */

/* Volume button interrupts */

/* Power Sequencing Signals */
GPIO(EN_PP3300_A,              PIN(A, 3), GPIO_OUT_LOW)
GPIO(EN_PP5000_A,              PIN(A, 4), GPIO_OUT_LOW)
/* The EC does not buffer this signal on Volteer. */
UNIMPLEMENTED(PCH_DSW_PWROK)

/* Other wake sources */
/*
 * GPIO_INT_BOTH is required for PSL wake from hibernate, but we don't need an
 * interrupt handler because it is automatically handled by the PSL.
 *
 * We need to lock the setting so this gpio can't be reconfigured to overdrive
 * the real reset signal. (This is the PSL input pin not the real reset pin).
 */
GPIO(EC_RST_ODL,		PIN(0, 2), GPIO_INT_BOTH |
					   GPIO_HIB_WAKE_HIGH |
					   GPIO_LOCKED)

/* AP/PCH Signals */
GPIO(EC_PCH_SYS_PWROK,         PIN(3, 7), GPIO_OUT_LOW)
GPIO(EC_PCH_RSMRST_ODL,        PIN(A, 6), GPIO_ODR_HIGH) /* TODO - b/140950085 - implement TGL sequencing requirement */
GPIO(EC_PCH_PWR_BTN_ODL,       PIN(C, 1), GPIO_ODR_HIGH)
GPIO(EC_PCH_RTCRST,            PIN(7, 6), GPIO_OUT_LOW)
GPIO(EC_PCH_WAKE_ODL,          PIN(7, 4), GPIO_ODR_HIGH)
GPIO(EC_ENTERING_RW,           PIN(E, 3), GPIO_OUT_LOW)
GPIO(EC_PROCHOT_ODL,           PIN(6, 3), GPIO_ODR_HIGH)
GPIO(CPU_C10_GATE_L,           PIN(6, 7), GPIO_INPUT)    /* TODO - b/141322107 - implement if board power sequencing insufficient */
GPIO(SYS_RST_ODL,              PIN(C, 5), GPIO_ODR_HIGH)

GPIO(EC_PCH_INT_ODL,           PIN(D, 6), GPIO_ODR_HIGH) /* TODO - b/140557015 - implement with MKBD sensor events */

/* USB and USBC Signals */

/* Misc Signals */
/* This selects between an LED module on the motherboard and one on the daughter
 * board, to be controlled by LED{1,2,3}. Select the one on the motherboard by
 * default. */
GPIO(LED_SIDESEL_4_L,          PIN(6, 0), GPIO_OUT_LOW)

/* I2C pins - Alternate function below configures I2C module on these pins */
GPIO(EC_I2C0_SENSOR_SCL,       PIN(B, 5), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C0_SENSOR_SDA,       PIN(B, 4), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C1_USB_C0_SCL,       PIN(9, 0), GPIO_INPUT)
GPIO(EC_I2C1_USB_C0_SDA,       PIN(8, 7), GPIO_INPUT)
GPIO(EC_I2C2_USB_C1_SCL,       PIN(9, 2), GPIO_INPUT)
GPIO(EC_I2C2_USB_C1_SDA,       PIN(9, 1), GPIO_INPUT)
GPIO(EC_I2C3_USB_1_MIX_SCL,    PIN(D, 1), GPIO_INPUT)
GPIO(EC_I2C3_USB_1_MIX_SDA,    PIN(D, 0), GPIO_INPUT)
GPIO(EC_I2C5_POWER_SCL,        PIN(3, 3), GPIO_INPUT)
GPIO(EC_I2C5_POWER_SDA,        PIN(3, 6), GPIO_INPUT)
GPIO(EC_I2C7_EEPROM_SCL,       PIN(B, 3), GPIO_INPUT)
GPIO(EC_I2C7_EEPROM_SDA,       PIN(B, 2), GPIO_INPUT)

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(B, BIT(5) | BIT(4)), 0, MODULE_I2C, (GPIO_INPUT | GPIO_SEL_1P8V)) /* I2C0 */
ALTERNATE(PIN_MASK(9, BIT(0) | BIT(2) | BIT(1)), 0, MODULE_I2C, 0)                   /* I2C1 SCL / I2C2 */
ALTERNATE(PIN_MASK(8, BIT(7)), 0, MODULE_I2C, 0)                                     /* I2C1 SDA */
ALTERNATE(PIN_MASK(D, BIT(1) | BIT(0)), 0, MODULE_I2C, 0)                            /* I2C3 */
ALTERNATE(PIN_MASK(3, BIT(3) | BIT(6)), 0, MODULE_I2C, 0)                            /* I2C5 */
ALTERNATE(PIN_MASK(B, BIT(3) | BIT(2)), 0, MODULE_I2C, 0)                            /* I2C7 */

ALTERNATE(PIN_MASK(C, BIT(2) | BIT(3) | BIT(4)), 0, MODULE_PWM, 0)                   /* LED{3,2,1}_L */

/* Keyboard pins */
#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
ALTERNATE(PIN_MASK(3, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT) /* KSI_00-01 */
ALTERNATE(PIN_MASK(2, 0xFC), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT) /* KSI_02-07 */
ALTERNATE(PIN_MASK(2, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH) /* KSO_00-01 */
GPIO(EC_KSO_02_INV, PIN(1, 7),                        GPIO_OUT_LOW)  /* KSO_02 */
ALTERNATE(PIN_MASK(1, 0x7F), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH) /* KSO_03-09 */
ALTERNATE(PIN_MASK(0, 0xF0), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH) /* KSO_10-13 */
ALTERNATE(PIN_MASK(8, 0x04), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH) /* KSO_14 */

/* UART */
ALTERNATE(PIN_MASK(6, BIT(5) | BIT(4)), 0, MODULE_UART, 0)                           /* UART from EC to Servo */

/* Power Switch Logic (PSL) inputs */
ALTERNATE(PIN_MASK(D, BIT(2)), 0, MODULE_PMU, 0)                    /* GPIOD2 = EC_LID_OPEN */
ALTERNATE(PIN_MASK(0, BIT(0) | BIT(1) | BIT(2)), 0, MODULE_PMU, 0)  /* GPIO00 = ACOK_OD,
                                                                       GPIO01 = H1_EC_PWR_BTN_ODL
                                                                       GPIO02 = EC_RST_ODL */

