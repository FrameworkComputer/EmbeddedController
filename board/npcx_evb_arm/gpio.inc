/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/********************** Inputs with interrupt handlers  **********************/
/* TODO: Redefine debug 2 inputs */
GPIO_INT(RECOVERY_L,       PIN(0, 0), GPIO_PULL_UP   | GPIO_INT_BOTH, switch_interrupt)       /* Recovery signal from servo */
GPIO_INT(WP_L,             PIN(9, 3), GPIO_PULL_UP   | GPIO_INT_BOTH, switch_interrupt)       /* Write protect input */
/* Used for ARM based platform */
GPIO_INT(SHI_CS_L,         PIN(5, 3), GPIO_INT_FALLING,shi_cs_event)                          /* SHI CS Ready, Low Active. */
/* For testing keyboard commands, we need the following 4 GPIOs */
/* TODO: Redefine 4 inputs */
GPIO_INT(POWER_BUTTON_L,   PIN(0, 2), GPIO_PULL_UP   | GPIO_INT_BOTH, power_button_interrupt) /* Power button */
GPIO_INT(LID_OPEN,         PIN(3, 3), GPIO_PULL_DOWN | GPIO_INT_BOTH, lid_interrupt)          /* Lid switch */

/**************************** Need a empty line between GPIO_INT and GPIO ****************************/
#ifdef CONFIG_TEST_1P8V
GPIO(ENTERING_RW,          PIN(3, 6), GPIO_ODR_LOW | GPIO_SEL_1P8V)                    /* Indicate when EC is entering RW code */
#else
GPIO(ENTERING_RW,          PIN(3, 6), GPIO_OUT_LOW )                                   /* Indicate when EC is entering RW code */
#endif
GPIO(PCH_WAKE_L,           PIN(5, 0), GPIO_OUT_HIGH)                /* Wake signal output to PCH */
/* For testing keyboard mkbp */
GPIO(EC_INT_L,             PIN(7, 4), GPIO_ODR_HIGH)                /* Interrupt pin for keyboard mkbp */
/* Used for module testing */
GPIO(PGOOD_FAN,            PIN(C, 7), GPIO_PULL_UP   | GPIO_INPUT)  /* Power Good for FAN test */

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
#ifdef CONFIG_TEST_1P8V
GPIO(I2C0_SCL0,     PIN(B, 5), GPIO_ODR_HIGH | GPIO_SEL_1P8V)
GPIO(I2C0_SDA0,     PIN(B, 4), GPIO_ODR_HIGH | GPIO_SEL_1P8V)
#else
GPIO(I2C0_SCL0,     PIN(B, 5), GPIO_ODR_HIGH)
GPIO(I2C0_SDA0,     PIN(B, 4), GPIO_ODR_HIGH)
#endif
GPIO(I2C0_SCL1,     PIN(B, 3), GPIO_ODR_HIGH)
GPIO(I2C0_SDA1,     PIN(B, 2), GPIO_ODR_HIGH)
GPIO(I2C1_SCL,      PIN(9, 0), GPIO_ODR_HIGH)
GPIO(I2C1_SDA,      PIN(8, 7), GPIO_ODR_HIGH)
GPIO(I2C2_SCL,      PIN(9, 2), GPIO_ODR_HIGH)
GPIO(I2C2_SDA,      PIN(9, 1), GPIO_ODR_HIGH)
GPIO(I2C3_SCL,      PIN(D, 1), GPIO_ODR_HIGH)
GPIO(I2C3_SDA,      PIN(D, 0), GPIO_ODR_HIGH)

/* Used for board version command */
GPIO(BOARD_VERSION1,       PIN(6, 4), GPIO_INPUT)    /* Board version stuffing resistor 1 */
GPIO(BOARD_VERSION2,       PIN(6, 5), GPIO_INPUT)    /* Board version stuffing resistor 2 */
GPIO(BOARD_VERSION3,       PIN(6, 6), GPIO_INPUT)    /* Board version stuffing resistor 3 */

/**************************** Alternate pins for UART/I2C/ADC/SPI/PWM/MFT ****************************/
/* Alternate pins for UART/I2C/ADC/SPI/PWM/MFT */
#if NPCX_UART_MODULE2
ALTERNATE(PIN_MASK(6, 0x30), 1,  MODULE_UART,        0)    /* CR_SIN/SOUT             GPIO64/65    */
#else
ALTERNATE(PIN_MASK(1, 0x03), 1,  MODULE_UART,        0)    /* CR_SIN/SOUT             GPIO10/11    */
#endif
ALTERNATE(PIN_MASK(B, 0x0C), 1,  MODULE_I2C,         0)    /* I2C0SDA1/I2C0SCL1       GPIOB2/B3	   */
ALTERNATE(PIN_MASK(B, 0x30), 1,  MODULE_I2C,         0)    /* I2C0SDA0/I2C0SCL0       GPIOB4/B5	   */
ALTERNATE(PIN_MASK(8, 0x80), 1,  MODULE_I2C,         0)    /* I2C1SDA                 GPIO87       */
ALTERNATE(PIN_MASK(9, 0x07), 1,  MODULE_I2C,         0)    /* I2C1SCL/I2C2SDA/I2C2SCL GPIO90/91/92 */
ALTERNATE(PIN_MASK(D, 0x03), 1,  MODULE_I2C,         0)    /* I2C3SDA/I2C3SCL         GPIOD0/D1    */
ALTERNATE(PIN_MASK(4, 0x38), 1,  MODULE_ADC,         0)    /* ADC                     GPIO45/44/43 */
ALTERNATE(PIN_MASK(A, 0x0A), 1,  MODULE_SPI,         0)    /* SPIP_MOSI/SPIP_SCLK     GPIOA3/A1	   */
ALTERNATE(PIN_MASK(9, 0x20), 1,  MODULE_SPI,         0)    /* SPIP_MISO               GPIO95       */
ALTERNATE(PIN_MASK(C, 0x04), 1,  MODULE_PWM,         0)    /* PWM1 for PWM/KBLIGHT Test GPIOC2     */
/* Alternative functionality for FANS */
#ifdef CONFIG_FANS
ALTERNATE(PIN_MASK(C, 0x08), 1,  MODULE_PWM,         0)    /* PWM0 for PWM/FAN Test   GPIOC3       */
#if NPCX_TACH_SEL2
ALTERNATE(PIN_MASK(9, 0x08), 1,  MODULE_PWM,         0)    /* MFT-1/TA1_TACH1 for FAN GPIO93       */
#else
ALTERNATE(PIN_MASK(4, 0x01), 1,  MODULE_PWM,         0)    /* MFT-1/TA1_TACH1 for FAN Test GPIO40  */
#endif
#endif

/* Keyboard Columns */
ALTERNATE(PIN_MASK(0, 0xE0), 0,  MODULE_KEYBOARD_SCAN, 0)
ALTERNATE(PIN_MASK(1, 0xFF), 0,  MODULE_KEYBOARD_SCAN, 0)
ALTERNATE(PIN_MASK(2, 0x03), 0,  MODULE_KEYBOARD_SCAN, 0)

/* Keyboard Rows */
ALTERNATE(PIN_MASK(2, 0xFC), 0,  MODULE_KEYBOARD_SCAN, 0)
ALTERNATE(PIN_MASK(3, 0x03), 0,  MODULE_KEYBOARD_SCAN, 0)
