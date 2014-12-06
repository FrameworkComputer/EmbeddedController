/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TODO: Redefine debug 2 inputs */
GPIO(RECOVERY_L,           0, 0, GPIO_PULL_UP   | GPIO_INT_BOTH, switch_interrupt)       /* Recovery signal from servo */
GPIO(WP_L,                 0, 1, GPIO_PULL_DOWN | GPIO_INT_BOTH, switch_interrupt)       /* Write protect input */

/* For testing 8042 commands, we need the following GPIOs */
/* TODO: Redefine 4 inputs */
GPIO(POWER_BUTTON_L,       0, 2, GPIO_PULL_UP   | GPIO_INT_BOTH, power_button_interrupt) /* Power button */
GPIO(LID_OPEN,             3, 3, GPIO_PULL_DOWN | GPIO_INT_BOTH, lid_interrupt)          /* Lid switch */
GPIO(ENTERING_RW,          3, 6, GPIO_OUT_LOW,  NULL)                                    /* Indicate when EC is entering RW code */
GPIO(PCH_WAKE_L,           5, 0, GPIO_OUT_HIGH, NULL)                                    /* Wake signal output to PCH */

/* Used for module testing */
GPIO(PGOOD_FAN,            C, 7, GPIO_PULL_UP	| GPIO_INPUT, NULL)                      /* Power Good for FAN test */
GPIO(SPI_CS_L,             A, 5, GPIO_OUT_HIGH,   NULL)                                  /* SPI_CS Ready, Low Active. */

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(MASTER_I2C_SCL,       B, 5, GPIO_INPUT,    NULL)
GPIO(MASTER_I2C_SDA,       B, 4, GPIO_INPUT,    NULL)

/* Used for board version command */
GPIO(BOARD_VERSION1,       6, 4, GPIO_INPUT,    NULL)  /* Board version stuffing resistor 1 */
GPIO(BOARD_VERSION2,       6, 5, GPIO_INPUT,    NULL)  /* Board version stuffing resistor 2 */
GPIO(BOARD_VERSION3,       6, 6, GPIO_INPUT,    NULL)  /* Board version stuffing resistor 3 */

/* Alternate pins for UART/I2C/ADC/SPI/PWM/MFT */
ALTERNATE(1, 0x03, 1,  MODULE_UART,        0)    /* CR_SIN/SOUT             GPIO10/11    */
ALTERNATE(B, 0x30, 1,  MODULE_I2C,         0)    /* I2C0SDA/I2C0SCL         GPIOB4/B5	 */
ALTERNATE(8, 0x80, 1,  MODULE_I2C,         0)    /* I2C1SDA                 GPIO87       */
ALTERNATE(9, 0x07, 1,  MODULE_I2C,         0)    /* I2C1SCL/I2C2SDA/I2C2SCL GPIO90/91/92 */
ALTERNATE(4, 0x38, 1,  MODULE_ADC,         0)    /* ADC                     GPIO45/44/43 */
ALTERNATE(A, 0x0A, 1,  MODULE_SPI,         0)    /* SPIP_MOSI/SPIP_SCLK     GPIOA3/A1	 */
ALTERNATE(9, 0x20, 1,  MODULE_SPI,         0)    /* SPIP_MISO               GPIO95       */
ALTERNATE(C, 0x04, 1,  MODULE_PWM_KBLIGHT, 0)    /* PWM1 for PWM/KBLIGHT Test	GPIOC2   */
ALTERNATE(C, 0x08, 1,  MODULE_PWM_FAN,     0)    /* PWM0 for PWM/FAN Test   GPIOC3       */
ALTERNATE(4, 0x01, 1,  MODULE_PWM_FAN,     0)    /* MFT-1/TA1_TACH1 for FAN Test GPIOD3  */
