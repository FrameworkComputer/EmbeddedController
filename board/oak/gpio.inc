/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO_INT(AC_PRESENT,       C, 6,  GPIO_INT_BOTH,                     extpower_interrupt)
GPIO_INT(POWER_BUTTON_L,   B, 5,  GPIO_INT_BOTH | GPIO_PULL_UP,      power_button_interrupt)
GPIO_INT(SOC_POWER_GOOD,   A, 3,  GPIO_INT_BOTH | GPIO_PULL_DOWN,    power_signal_interrupt)
GPIO_INT(LID_OPEN,         C, 13, GPIO_INT_BOTH,                     lid_interrupt)          /* LID switch detection */
GPIO_INT(SUSPEND_L,        C, 7,  GPIO_INT_BOTH,                     power_signal_interrupt) /* AP suspend/resume state */
GPIO_INT(VBUS_WAKE_L,      E, 1,  GPIO_INT_BOTH,                     vbus_wake_interrupt)    /* VBUS wake: PE 1 */
GPIO_INT(PD_MCU_INT,       E, 0,  GPIO_INT_FALLING | GPIO_INT_DSLEEP,pd_mcu_interrupt)       /* Interrupt from PD MCU, external pull-up */
GPIO_INT(SPI1_NSS,         A, 4,  GPIO_INT_BOTH | GPIO_PULL_UP,      spi_event)              /* SPI Chip Select */
GPIO_INT(AP_RESET_L,       C, 3,  GPIO_INT_BOTH | GPIO_PULL_UP,      ap_reset_interrupt)     /* AP reset signal from servo board */

/* Keyboard inputs */
GPIO_INT(KB_IN00, C, 8,  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN01, C, 9,  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN02, C, 10, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN03, C, 11, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN04, C, 12, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN05, C, 14, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN06, C, 15, GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN07, D, 2,  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)

/* Inputs without interrupt handlers */
GPIO(5V_POWER_GOOD,    A, 1,  GPIO_INPUT)
GPIO(EC_WAKE,          A, 0,  GPIO_INPUT|GPIO_PULL_DOWN)
GPIO(WP_L,             B, 4,  GPIO_INPUT)      /* Write protect input */

/* Board version */
GPIO(BOARD_VERSION1,   E, 10,  GPIO_INPUT|GPIO_PULL_UP) /* Board ID 0 */
GPIO(BOARD_VERSION2,   E,  9,  GPIO_INPUT|GPIO_PULL_UP) /* Board ID 1 */
GPIO(BOARD_VERSION3,   E, 12,  GPIO_INPUT|GPIO_PULL_UP) /* Board ID 2 */
GPIO(BOARD_VERSION4,   E, 11,  GPIO_INPUT|GPIO_PULL_UP) /* Board ID 3 */

/* Outputs */
GPIO(BAT_LED0,          B, 11, GPIO_OUT_LOW)   /* LED_GREEN */
GPIO(BAT_LED1,          A, 11, GPIO_OUT_LOW)   /* LED_ORANGE */
GPIO(EC_BL_OVERRIDE,    F, 1,  GPIO_OUT_LOW)
GPIO(EC_INT,            B, 9,  GPIO_OUT_HIGH)
GPIO(ENTERING_RW,       F, 0,  GPIO_OUT_LOW)
GPIO(KB_OUT00,          B, 0,  GPIO_KB_OUTPUT)
GPIO(KB_OUT01,          B, 8,  GPIO_KB_OUTPUT)
GPIO(KB_OUT02,          B, 12, GPIO_OUT_LOW)   /* KSO2 is inverted */
GPIO(KB_OUT03,          B, 2,  GPIO_KB_OUTPUT)
GPIO(KB_OUT04,          A, 8,  GPIO_KB_OUTPUT)
GPIO(KB_OUT05,          D, 4,  GPIO_KB_OUTPUT)
GPIO(KB_OUT06,          D, 0,  GPIO_KB_OUTPUT)
GPIO(KB_OUT07,          D, 1,  GPIO_KB_OUTPUT)
GPIO(KB_OUT08,          C, 2,  GPIO_KB_OUTPUT)
GPIO(KB_OUT09,          B, 1,  GPIO_KB_OUTPUT)
GPIO(KB_OUT10,          C, 5,  GPIO_KB_OUTPUT)
GPIO(KB_OUT11,          C, 4,  GPIO_KB_OUTPUT)
GPIO(KB_OUT12,          A, 13, GPIO_KB_OUTPUT)
GPIO(SYSTEM_POWER_H,    B, 10, GPIO_OUT_LOW)
GPIO(PMIC_PWRON_H,      A, 12, GPIO_OUT_LOW)
GPIO(PMIC_WARM_RESET_H, B, 3,  GPIO_OUT_LOW)
GPIO(LEVEL_SHIFT_EN_L,  D, 3,  GPIO_OUT_LOW)    /* LID/AC level shift */
GPIO(USB_PD_RST_L,      A, 15, GPIO_OUT_HIGH)   /* PD reset */
GPIO(USB_C0_5V_OUT,     D, 8,  GPIO_OUT_LOW)    /* USBC port 0 5V */
GPIO(USB_C0_CHARGE_L,   D, 9,  GPIO_OUT_LOW)    /* USBC port 0 charge */
GPIO(USB_C1_5V_OUT,     D, 10, GPIO_OUT_LOW)    /* USBC port 1 5V */
GPIO(USB_C1_CHARGE_L,   D, 11, GPIO_OUT_HIGH)   /* USBC port 1 charge */
GPIO(USB_PD_VBUS_WAKE,  B, 15, GPIO_OUT_LOW)    /* PD VBUS wake */
GPIO(USB_DP_HPD,        F, 3,  GPIO_OUT_LOW)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C0_SCL,          B, 6,  GPIO_INPUT) /* EC I2C */
GPIO(I2C0_SDA,          B, 7,  GPIO_INPUT)
GPIO(I2C1_SCL,          B, 13, GPIO_INPUT) /* PD I2C */
GPIO(I2C1_SDA,          B, 14, GPIO_INPUT)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(NONE)

ALTERNATE(A, 0x0600, 1, MODULE_UART,   0) /* USART1: PA9/PA10 */
ALTERNATE(B, 0x00c0, 1, MODULE_I2C,    0) /* I2C MASTER:PB6/7 */
ALTERNATE(B, 0x6000, 5, MODULE_I2C,    0) /* I2C MASTER:PB13/14 */
ALTERNATE(A, 0x00f0, 0, MODULE_SPI,    0) /* SPI SLAVE:PA4/5/6/7 */
