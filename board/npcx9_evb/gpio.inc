/* -*- mode:c -*-
 *
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Pins for internal flash testing */
GPIO_INT(RECOVERY_L,       PIN(0, 3), GPIO_INT_BOTH | GPIO_PULL_UP, switch_interrupt)
GPIO_INT(WP_L,             PIN(9, 3), GPIO_INT_BOTH | GPIO_PULL_UP, switch_interrupt)

/* Pins for hibernate testing */
#ifdef CONFIG_HIBERNATE_PSL
/*
 * Please notice internal PU/PD is gone if IOs are selected to PSL_INx. The
 * power consumption of PSL is ultra-low and sensitive. Putting a large
 * external PU/PD resistance for PSL input pins is recommended.
 */
GPIO_INT(AC_PRESENT,       PIN(D, 2), GPIO_INT_BOTH, extpower_interrupt)                 /* PSL_IN1# (Low Active) */
GPIO_INT(POWER_BUTTON_L,   PIN(0, 0), GPIO_INT_BOTH, power_button_interrupt)             /* PSL_IN2# (Low Active) */
GPIO_INT(LID_OPEN,         PIN(0, 1), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, lid_interrupt) /* PSL_IN3# (High Active) */
#else
GPIO_INT(AC_PRESENT,       PIN(7, 4), GPIO_INT_BOTH | GPIO_PULL_UP,   extpower_interrupt)
GPIO_INT(POWER_BUTTON_L,   PIN(7, 5), GPIO_INT_BOTH | GPIO_PULL_UP,   power_button_interrupt)
GPIO_INT(LID_OPEN,         PIN(A, 6), GPIO_INT_BOTH | GPIO_PULL_DOWN, lid_interrupt)
#endif

/* Pins for SPI/FAN/LPC modules testing */
GPIO(ENTERING_RW,          PIN(3, 6), GPIO_OUT_LOW )
GPIO(PCH_WAKE_L,           PIN(5, 0), GPIO_OUT_HIGH)
GPIO(PGOOD_FAN,            PIN(C, 7), GPIO_INPUT | GPIO_PULL_UP)
GPIO(SPI_CS_L,             PIN(A, 5), GPIO_OUT_HIGH)

/* Pins for I2C module testing */
GPIO(I2C0_SCL0,            PIN(B, 5), GPIO_ODR_HIGH)
GPIO(I2C0_SDA0,            PIN(B, 4), GPIO_ODR_HIGH)
GPIO(I2C1_SCL0,            PIN(9, 0), GPIO_ODR_HIGH)
GPIO(I2C1_SDA0,            PIN(8, 7), GPIO_ODR_HIGH)
GPIO(I2C2_SCL0,            PIN(9, 2), GPIO_ODR_HIGH)
GPIO(I2C2_SDA0,            PIN(9, 1), GPIO_ODR_HIGH)
GPIO(I2C3_SCL0,            PIN(D, 1), GPIO_ODR_HIGH)
GPIO(I2C3_SDA0,            PIN(D, 0), GPIO_ODR_HIGH)
GPIO(I2C7_SDA0,            PIN(B, 2), GPIO_ODR_HIGH)
GPIO(I2C7_SCL0,            PIN(B, 3), GPIO_ODR_HIGH)

/* Pins for board version command */
GPIO(BOARD_VERSION1,       PIN(6, 4), GPIO_INPUT)
GPIO(BOARD_VERSION2,       PIN(6, 5), GPIO_INPUT)
GPIO(BOARD_VERSION3,       PIN(6, 6), GPIO_INPUT)

/*********************** Alternate pins for npcx7 series **********************/
#if (CONFIG_CONSOLE_UART == 0)
/* UART1 Tx/Rx */
#if NPCX_UART_MODULE2
ALTERNATE(PIN_MASK(6, 0x30), 1, MODULE_UART, 0) /* CR_SIN/SOUT  GPIO64/65 */
#else
ALTERNATE(PIN_MASK(1, 0x03), 1, MODULE_UART, 0) /* CR_SIN/SOUT  GPIO10/11 */
#endif
#else
/* UART2 Tx/Rx */
ALTERNATE(PIN_MASK(7, 0x20), 1, MODULE_UART, 0) /* CR_SIN2  GPIO75 */
ALTERNATE(PIN_MASK(8, 0x40), 1, MODULE_UART, 0) /* CR_SOUT2 GPIO86 */
#endif

/* ADC */
ALTERNATE(PIN_MASK(4, 0x3E), 1, MODULE_ADC, 0) /* ADC0/1/2/3/4 GPIO45/44/43/42/41 */
ALTERNATE(PIN_MASK(3, 0x90), 1, MODULE_ADC, 0) /* ADC5/6 GPIO37/34 */
ALTERNATE(PIN_MASK(E, 0x03), 1, MODULE_ADC, 0) /* ADC7/10 GPIOE1/E0 */
ALTERNATE(PIN_MASK(F, 0x03), 1, MODULE_ADC, 0) /* ADC8/9 GPIOF1/F0 */
ALTERNATE(PIN_MASK(C, 0x80), 1, MODULE_ADC, 0) /* ADC11 GPIOC7 */

/* SPI */
ALTERNATE(PIN_MASK(A, 0x0A), 1, MODULE_SPI, 0) /* SPIP_MOSI/SPIP_SCLK GPIOA3/A1 */
ALTERNATE(PIN_MASK(9, 0x20), 1, MODULE_SPI, 0) /* SPIP_MISO           GPIO95    */

/* PWM */
ALTERNATE(PIN_MASK(C, 0x10), 1, MODULE_PWM, 0) /* PWM2 for KBLIGHT Test - GPIOC4 */

/* Fan (Tachometer) */
#ifdef CONFIG_FANS
ALTERNATE(PIN_MASK(C, 0x08), 1, MODULE_PWM, 0) /* PWM0 for FAN Test - GPIOC3    */
#if NPCX_TACH_SEL2
ALTERNATE(PIN_MASK(9, 0x08), 1, MODULE_PWM, 0) /* TA1_SL1 for FAN Test - GPIO93 */
#else
ALTERNATE(PIN_MASK(4, 0x01), 1, MODULE_PWM, 0) /* TA1_SL2 for FAN Test - GPIO40 */
#endif
#endif

/* I2C Ports */
ALTERNATE(PIN_MASK(B, 0x30), 1, MODULE_I2C, 0) /* I2C0SDA0/SCL0          GPIOB4/B5    */
ALTERNATE(PIN_MASK(8, 0x80), 1, MODULE_I2C, 0) /* I2C1SDA0               GPIO87       */
ALTERNATE(PIN_MASK(9, 0x07), 1, MODULE_I2C, 0) /* I2C1SCL0/I2C2SDA0/SCL0 GPIO90/91/92 */
ALTERNATE(PIN_MASK(D, 0x03), 1, MODULE_I2C, 0) /* I2C3SDA0/SCL0          GPIOD0/D1    */
ALTERNATE(PIN_MASK(B, 0x0C), 1, MODULE_I2C, 0) /* I2C7SDA0/SCL0          GPIOB2/B3    */

/* Keyboard Columns */
ALTERNATE(PIN_MASK(0, 0xE0), 0, MODULE_KEYBOARD_SCAN, 0)
ALTERNATE(PIN_MASK(1, 0xFF), 0, MODULE_KEYBOARD_SCAN, 0)
ALTERNATE(PIN_MASK(2, 0x03), 0, MODULE_KEYBOARD_SCAN, 0)

/* Keyboard Rows */
ALTERNATE(PIN_MASK(2, 0xFC), 0, MODULE_KEYBOARD_SCAN, 0)
ALTERNATE(PIN_MASK(3, 0x03), 0, MODULE_KEYBOARD_SCAN, 0)

/* PSL for hibernating */
#ifdef CONFIG_HIBERNATE_PSL
ALTERNATE(PIN_MASK(D, 0x04), 1,  MODULE_PMU, 0) /* PSL_IN1     GPIOD2 */
ALTERNATE(PIN_MASK(0, 0x03), 1,  MODULE_PMU, 0) /* PSL_IN2/3/4 GPIO00/01/02 */
#endif
