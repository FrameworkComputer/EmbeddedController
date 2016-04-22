/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(AC_PRESENT,     PIN(C, 6),  GPIO_INT_BOTH,    extpower_interrupt)
GPIO_INT(LID_OPEN,       PIN(C, 13), GPIO_INT_BOTH,    lid_interrupt)          /* LID switch detection */
GPIO_INT(SUSPEND_L,      PIN(C, 7),  GPIO_INT_BOTH,    power_signal_interrupt) /* AP suspend/resume state */
GPIO_INT(SOC_POWER_GOOD, PIN(A, 3),  GPIO_INT_BOTH,    power_signal_interrupt)
GPIO_INT(PD_MCU_INT,     PIN(E, 0),  GPIO_INT_RISING,  pd_mcu_interrupt)       /* Signal from PD MCU, external pull-up */
GPIO_INT(BC12_CABLE_INT, PIN(E, 1),  GPIO_INT_FALLING | GPIO_PULL_UP, usb_evt)    /* interrupt from BC12 and CABLE_DET */
GPIO_INT(POWER_BUTTON_L, PIN(B, 5),  GPIO_INT_BOTH | GPIO_PULL_UP,  power_button_interrupt)
GPIO_INT(SPI1_NSS,       PIN(A, 4),  GPIO_INT_BOTH | GPIO_PULL_UP,  spi_event)              /* SPI Chip Select */

/* Keyboard inputs */
#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT GPIO_ODR_HIGH

GPIO_INT(KB_IN00, PIN(C, 8),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN01, PIN(C, 9),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN02, PIN(C, 10), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN03, PIN(C, 11), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN04, PIN(C, 12), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN05, PIN(C, 14), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN06, PIN(C, 15), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN07, PIN(D, 2),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)

GPIO(KB_OUT00,          PIN(B, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT01,          PIN(B, 8),  GPIO_KB_OUTPUT)
GPIO(KB_OUT02,          PIN(B, 12), GPIO_OUT_LOW)   /* KSO2 is inverted */
GPIO(KB_OUT03,          PIN(B, 2),  GPIO_KB_OUTPUT)
GPIO(KB_OUT04,          PIN(A, 8),  GPIO_KB_OUTPUT)
GPIO(KB_OUT05,          PIN(D, 14), GPIO_KB_OUTPUT)
GPIO(KB_OUT06,          PIN(D, 13), GPIO_KB_OUTPUT)
GPIO(KB_OUT07,          PIN(D, 15), GPIO_KB_OUTPUT)
GPIO(KB_OUT08,          PIN(C, 2),  GPIO_KB_OUTPUT)
GPIO(KB_OUT09,          PIN(B, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT10,          PIN(C, 5),  GPIO_KB_OUTPUT)
GPIO(KB_OUT11,          PIN(C, 4),  GPIO_KB_OUTPUT)
GPIO(KB_OUT12,          PIN(D, 5),  GPIO_KB_OUTPUT)


/* Inputs without interrupt handlers */
GPIO(5V_POWER_GOOD,      PIN(A, 1), GPIO_INPUT)
GPIO(EC_WAKE,            PIN(A, 0), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(WP_L,               PIN(B, 4), GPIO_INPUT)      /* Write protect input */
GPIO(BAT_PRESENT_L,      PIN(E, 3), GPIO_INPUT | GPIO_PULL_UP)
GPIO(USB_C0_VBUS_WAKE_L, PIN(D, 12), GPIO_INPUT)
GPIO(EC_INT_L,           PIN(B, 9),  GPIO_ODR_HIGH)

/* Board version */
GPIO(BOARD_VERSION1, PIN(E, 10), GPIO_INPUT) /* Board ID 0 */
GPIO(BOARD_VERSION2, PIN(E,  9), GPIO_INPUT) /* Board ID 1 */
GPIO(BOARD_VERSION3, PIN(E, 12), GPIO_INPUT) /* Board ID 2 */
GPIO(BOARD_VERSION4, PIN(E, 11), GPIO_INPUT) /* Board ID 3 */

/* Outputs */
GPIO(BAT_LED0,          PIN(A, 11), GPIO_OUT_LOW)   /* LED_BLUE */
GPIO(BAT_LED1,          PIN(B, 11), GPIO_OUT_LOW)   /* LED_ORANGE   */
GPIO(PWR_LED0,          PIN(E,  8), GPIO_OUT_LOW)   /* LED_BLUE */
GPIO(PWR_LED1,          PIN(D,  6), GPIO_OUT_LOW)   /* LED_ORANGE   */

GPIO(EC_BL_OVERRIDE,    PIN(F, 1),  GPIO_OUT_LOW)
GPIO(ENTERING_RW,       PIN(F, 0),  GPIO_OUT_LOW)

GPIO(AP_RESET_L,        PIN(C, 3),  GPIO_ODR_HIGH) /* Connect to the PMU_SYSRSTB */
GPIO(BC12_WAKE_L,       PIN(D, 7),  GPIO_INPUT)
GPIO(USB_C0_CABLE_DET_L,PIN(E, 7),  GPIO_INPUT | GPIO_PULL_UP)

GPIO(SYSTEM_POWER_H,    PIN(B, 10), GPIO_OUT_LOW)
GPIO(PMIC_PWRON_H,      PIN(A, 12), GPIO_OUT_LOW)
GPIO(PMIC_WARM_RESET_H, PIN(B, 3),  GPIO_OUT_LOW)
GPIO(LEVEL_SHIFT_EN_L,  PIN(F, 10), GPIO_OUT_LOW)    /* LID/AC level shift */

GPIO(USB_C0_5V_EN,      PIN(D, 8),  GPIO_OUT_LOW)    /* USBC port 0 5V */
GPIO(USB_C0_CHARGE_L,   PIN(D, 9),  GPIO_OUT_LOW)    /* USBC port 0 charge */
GPIO(USB_C0_RST,        PIN(D, 10), GPIO_ODR_HIGH)    /* ANX7688 reset */
GPIO(USB_C0_PWR_EN_L,   PIN(B, 15), GPIO_ODR_HIGH)    /* ANX7688 power enable */
GPIO(USB_C0_EXTPWR_EN,  PIN(F, 2),  GPIO_OUT_HIGH)   /* ANX7688 3.3V ext power enable */
GPIO(USB_DP_HPD,        PIN(F, 3),  GPIO_INPUT)
GPIO(EN_TP_INT_L,       PIN(E, 14), GPIO_OUT_LOW)    /* touchpad interrupt enable */

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

/* SPI MASTER. For SPI sensor */
GPIO(SPI2_NSS,    PIN(D, 0),  GPIO_OUT_HIGH) /* mainboard */
GPIO(SPI2_NSS_DB, PIN(F, 6),  GPIO_OUT_HIGH) /* daughterboard */

ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,       0) /* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(B, 0x00c0), 1, MODULE_I2C,        0) /* I2C MASTER:PB6/7 */
ALTERNATE(PIN_MASK(B, 0x6000), 5, MODULE_I2C,        0) /* I2C MASTER:PB13/14 */
ALTERNATE(PIN_MASK(A, 0x00f0), 0, MODULE_SPI,        0) /* SPI SLAVE:PA4/5/6/7 */
ALTERNATE(PIN_MASK(D, 0x001A), 1, MODULE_SPI_MASTER, 0) /* SPI MASTER:PD1/3/4 */
