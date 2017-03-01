/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(AC_PRESENT,     PIN(C, 6),  GPIO_INT_BOTH,    extpower_interrupt)
GPIO_INT(LID_OPEN,       PIN(C, 9),  GPIO_INT_BOTH,    lid_interrupt)          /* LID switch detection */
GPIO_INT(SUSPEND_L,      PIN(C, 11), GPIO_INT_BOTH,    power_signal_interrupt) /* AP suspend/resume state */
GPIO_INT(SOC_POWER_GOOD, PIN(A, 3),  GPIO_INT_BOTH,    power_signal_interrupt)
GPIO_INT(PD_MCU_INT,     PIN(C, 15), GPIO_INT_RISING,  pd_mcu_interrupt)       /* Signal from PD MCU, external pull-up */
GPIO_INT(BC12_CABLE_INT, PIN(C, 7),  GPIO_INT_FALLING | GPIO_PULL_UP, usb_evt) /* interrupt from BC12 and CABLE_DET */
GPIO_INT(POWER_BUTTON_L, PIN(B, 5),  GPIO_INT_BOTH | GPIO_PULL_UP,  power_button_interrupt)
GPIO_INT(SPI1_NSS,       PIN(A, 4),  GPIO_INT_BOTH | GPIO_PULL_UP,  spi_event) /* SPI Chip Select */
GPIO_INT(VOLUME_DOWN_L,  PIN(A, 14), GPIO_INT_BOTH, button_interrupt)
GPIO_INT(VOLUME_UP_L,    PIN(A, 13), GPIO_INT_BOTH, button_interrupt)

/* Inputs without interrupt handlers */
GPIO(5V_POWER_GOOD,      PIN(A, 1),  GPIO_INPUT)
GPIO(EC_WAKE,            PIN(A, 0),  GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(WP_L,               PIN(B, 4),  GPIO_INPUT)      /* Write protect input */
GPIO(BAT_PRESENT_L,      PIN(C, 12), GPIO_INPUT | GPIO_PULL_UP)
GPIO(USB_C0_VBUS_WAKE_L, PIN(B, 1),  GPIO_INPUT)
GPIO(EC_INT_L,           PIN(B, 9),  GPIO_ODR_HIGH)

/* Board version */
GPIO(BOARD_VERSION1,     PIN(C, 5), GPIO_INPUT) /* Board ID 0 */

/* Outputs */
GPIO(BAT_LED0,          PIN(B, 11), GPIO_OUT_LOW)   /* LED_BLUE */
GPIO(BAT_LED1,          PIN(B,  0), GPIO_OUT_LOW)   /* LED_ORANGE   */

UNIMPLEMENTED(EC_BL_OVERRIDE)
GPIO(ENTERING_RW,       PIN(F, 0),  GPIO_OUT_LOW)

GPIO(AP_RESET_L,        PIN(B, 8),  GPIO_ODR_HIGH) /* Connect to the PMU_SYSRSTB */
GPIO(BC12_WAKE_L,       PIN(D, 2),  GPIO_INPUT)
GPIO(USB_C0_CABLE_DET_L,PIN(A, 8),  GPIO_INPUT | GPIO_PULL_UP)

GPIO(SYSTEM_POWER_H,    PIN(B, 10), GPIO_OUT_LOW)
GPIO(PMIC_PWRON_H,      PIN(C, 8),  GPIO_OUT_LOW)
GPIO(PMIC_WARM_RESET_H, PIN(B, 3),  GPIO_OUT_LOW)
GPIO(LEVEL_SHIFT_EN_L,  PIN(C, 13), GPIO_OUT_LOW)    /* LID/AC level shift */

GPIO(USB_C0_5V_EN,      PIN(B, 2),  GPIO_OUT_LOW)    /* USBC port 0 5V */
GPIO(USB_C0_CHARGE_L,   PIN(C, 2),  GPIO_OUT_LOW)    /* USBC port 0 charge */
GPIO(USB_C0_RST,        PIN(C, 4),  GPIO_ODR_HIGH)   /* ANX7688 reset */
GPIO(USB_C0_PWR_EN_L,   PIN(C, 10), GPIO_ODR_HIGH)   /* ANX7688 power enable */
GPIO(USB_C0_EXTPWR_EN,  PIN(C, 14), GPIO_OUT_LOW)    /* ANX7688 3.3V ext power enable */

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
GPIO(I2C1_SCL, PIN(A, 11), GPIO_INPUT) /* PD I2C */
GPIO(I2C1_SDA, PIN(A, 12), GPIO_INPUT)

/* SPI MASTER. For SPI sensor */
GPIO(SPI2_NSS, PIN(B, 12), GPIO_OUT_HIGH)

ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,       0) /* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(B, 0x00c0), 1, MODULE_I2C,        0) /* I2C MASTER:PB6/7 */
ALTERNATE(PIN_MASK(A, 0x1800), 5, MODULE_I2C,        0) /* I2C MASTER:PA11/12 */
ALTERNATE(PIN_MASK(A, 0x00f0), 0, MODULE_SPI,        0) /* SPI SLAVE:PA4/5/6/7 */
ALTERNATE(PIN_MASK(B, 0xe000), 0, MODULE_SPI_MASTER, 0) /* SPI MASTER:PB13/14/15 */
