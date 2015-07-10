/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO_INT(AC_PRESENT,     PIN(C, 6),  GPIO_INT_BOTH,                     extpower_interrupt)
GPIO_INT(POWER_BUTTON_L, PIN(B, 5),  GPIO_INT_BOTH | GPIO_PULL_UP,      power_button_interrupt)
GPIO_INT(SOC_POWER_GOOD, PIN(A, 3),  GPIO_INT_BOTH | GPIO_PULL_DOWN,    power_signal_interrupt)
GPIO_INT(LID_OPEN,       PIN(C, 13), GPIO_INT_BOTH,                     lid_interrupt)          /* LID switch detection */
GPIO_INT(SUSPEND_L,      PIN(C, 7),  GPIO_INT_BOTH,                     power_signal_interrupt) /* AP suspend/resume state */
GPIO_INT(VBUS_WAKE_L,    PIN(E, 1),  GPIO_INT_BOTH,                     vbus_wake_interrupt)    /* VBUS wake: PE 1 */
GPIO_INT(PD_MCU_INT,     PIN(E, 0),  GPIO_INT_FALLING,                  pd_mcu_interrupt)       /* Signal from PD MCU, external pull-up */
GPIO_INT(SPI1_NSS,       PIN(A, 4),  GPIO_INT_BOTH | GPIO_PULL_UP,      spi_event)              /* SPI Chip Select */
GPIO_INT(AP_RESET_L,     PIN(C, 3),  GPIO_INT_BOTH | GPIO_PULL_UP,      ap_reset_interrupt)     /* AP reset signal from servo board */

/* Keyboard inputs */
GPIO_INT(KB_IN00, PIN(C, 8),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN01, PIN(C, 9),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN02, PIN(C, 10), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN03, PIN(C, 11), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN04, PIN(C, 12), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN05, PIN(C, 14), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN06, PIN(C, 15), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN07, PIN(D, 2),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)

/* Inputs without interrupt handlers */
GPIO(5V_POWER_GOOD, PIN(A, 1), GPIO_INPUT)
GPIO(EC_WAKE,       PIN(A, 0), GPIO_INPUT|GPIO_PULL_DOWN)
GPIO(WP_L,          PIN(B, 4), GPIO_INPUT)      /* Write protect input */
GPIO(BAT_PRESENT_L, PIN(E, 3), GPIO_INPUT|GPIO_PULL_UP)

/* Board version */
GPIO(BOARD_VERSION1, PIN(E, 10), GPIO_INPUT|GPIO_PULL_UP) /* Board ID 0 */
GPIO(BOARD_VERSION2, PIN(E,  9), GPIO_INPUT|GPIO_PULL_UP) /* Board ID 1 */
GPIO(BOARD_VERSION3, PIN(E, 12), GPIO_INPUT|GPIO_PULL_UP) /* Board ID 2 */
GPIO(BOARD_VERSION4, PIN(E, 11), GPIO_INPUT|GPIO_PULL_UP) /* Board ID 3 */

/* Outputs */
GPIO(BAT_LED0,          PIN(B, 11), GPIO_OUT_LOW)   /* LED_GREEN */
GPIO(BAT_LED1,          PIN(A, 11), GPIO_OUT_LOW)   /* LED_ORANGE */
GPIO(EC_BL_OVERRIDE,    PIN(F, 1),  GPIO_OUT_LOW)
GPIO(EC_INT,            PIN(B, 9),  GPIO_OUT_HIGH)
GPIO(ENTERING_RW,       PIN(F, 0),  GPIO_OUT_LOW)
GPIO(KB_OUT00,          PIN(B, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT01,          PIN(B, 8),  GPIO_KB_OUTPUT)
GPIO(KB_OUT02,          PIN(B, 12), GPIO_OUT_LOW)   /* KSO2 is inverted */
GPIO(KB_OUT03,          PIN(B, 2),  GPIO_KB_OUTPUT)
GPIO(KB_OUT04,          PIN(A, 8),  GPIO_KB_OUTPUT)
GPIO(KB_OUT05,          PIN(D, 4),  GPIO_KB_OUTPUT)
GPIO(KB_OUT06,          PIN(D, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT07,          PIN(D, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT08,          PIN(C, 2),  GPIO_KB_OUTPUT)
GPIO(KB_OUT09,          PIN(B, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT10,          PIN(C, 5),  GPIO_KB_OUTPUT)
GPIO(KB_OUT11,          PIN(C, 4),  GPIO_KB_OUTPUT)
#ifdef CONFIG_BOARD_OAK_REV_1
GPIO(KB_OUT12,          PIN(A, 13), GPIO_KB_OUTPUT)
#else
GPIO(KB_OUT12,          PIN(D, 5), GPIO_KB_OUTPUT)
#endif /* CONFIG_BOARD_OAK_REV_1 */

GPIO(SYSTEM_POWER_H,    PIN(B, 10), GPIO_OUT_LOW)
GPIO(PMIC_PWRON_H,      PIN(A, 12), GPIO_OUT_LOW)
GPIO(PMIC_WARM_RESET_H, PIN(B, 3),  GPIO_OUT_LOW)
GPIO(LEVEL_SHIFT_EN_L,  PIN(D, 3),  GPIO_OUT_LOW)    /* LID/AC level shift */
GPIO(USB_PD_RST_L,      PIN(A, 15), GPIO_OUT_HIGH)   /* PD reset */
GPIO(USB_C0_5V_OUT,     PIN(D, 8),  GPIO_OUT_LOW)    /* USBC port 0 5V */
GPIO(USB_C0_CHARGE_L,   PIN(D, 9),  GPIO_OUT_LOW)    /* USBC port 0 charge */
GPIO(USB_C1_5V_OUT,     PIN(D, 10), GPIO_OUT_LOW)    /* USBC port 1 5V */
GPIO(USB_C1_CHARGE_L,   PIN(D, 11), GPIO_OUT_LOW)    /* USBC port 1 charge */
GPIO(USB_PD_VBUS_WAKE,  PIN(B, 15), GPIO_OUT_LOW)    /* PD VBUS wake */
GPIO(USB_DP_HPD,        PIN(F, 3),  GPIO_OUT_LOW)
GPIO(DP_MUX_EN_L,       PIN(E, 6),  GPIO_OUT_LOW)
GPIO(DP_SWITCH_CTL,     PIN(E, 5),  GPIO_OUT_LOW)

#ifdef CONFIG_BOARD_OAK_REV_1
GPIO(USB_C_BC12_SEL,    PIN(A, 14), GPIO_OUT_LOW)
#else
GPIO(USB_C_BC12_SEL,    PIN(D, 7),  GPIO_OUT_LOW)
#endif /* CONFIG_BOARD_OAK_REV_1 */

/* Analog pins */
GPIO(VDC_BOOSTIN_SENSE, PIN(C, 1), GPIO_ANALOG) /* ADC_IN11 */
GPIO(PSYS_MONITOR,      PIN(A, 2), GPIO_ANALOG) /* ADC_IN2  */
GPIO(AMON_BMON,         PIN(C, 0), GPIO_ANALOG) /* ADC_IN10 */

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C0_SCL, PIN(B, 6),  GPIO_INPUT) /* EC I2C */
GPIO(I2C0_SDA, PIN(B, 7),  GPIO_INPUT)
GPIO(I2C1_SCL, PIN(B, 13), GPIO_INPUT) /* PD I2C */
GPIO(I2C1_SDA, PIN(B, 14), GPIO_INPUT)

ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,   0) /* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(B, 0x00c0), 1, MODULE_I2C,    0) /* I2C MASTER:PB6/7 */
ALTERNATE(PIN_MASK(B, 0x6000), 5, MODULE_I2C,    0) /* I2C MASTER:PB13/14 */
ALTERNATE(PIN_MASK(A, 0x00f0), 0, MODULE_SPI,    0) /* SPI SLAVE:PA4/5/6/7 */
