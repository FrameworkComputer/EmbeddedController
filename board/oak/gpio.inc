/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(AC_PRESENT,     PIN(C, 6),  GPIO_INT_BOTH,                     extpower_interrupt)
GPIO_INT(POWER_BUTTON_L, PIN(B, 5),  GPIO_INT_BOTH | GPIO_PULL_UP,      power_button_interrupt)
GPIO_INT(LID_OPEN,       PIN(C, 13), GPIO_INT_BOTH,                     lid_interrupt)          /* LID switch detection */
#if BOARD_REV <= OAK_REV3
GPIO_INT(SUSPEND_L,      PIN(C, 7),  GPIO_INT_BOTH | GPIO_PULL_DOWN,    power_signal_interrupt) /* AP suspend/resume state */
#else
GPIO_INT(SUSPEND_L,      PIN(C, 7),  GPIO_INT_BOTH,                     power_signal_interrupt) /* AP suspend/resume state */
#endif
GPIO_INT(PD_MCU_INT,     PIN(E, 0),  GPIO_INT_FALLING,                  pd_mcu_interrupt)       /* Signal from PD MCU, external pull-up */
GPIO_INT(SPI1_NSS,       PIN(A, 4),  GPIO_INT_BOTH | GPIO_PULL_UP,      spi_event)              /* SPI Chip Select */

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

/* Board specific interrupt and input */
#if BOARD_REV <= OAK_REV1
GPIO_INT(SOC_POWER_GOOD, PIN(A, 3),  GPIO_INT_BOTH | GPIO_PULL_DOWN,    power_signal_interrupt)
GPIO(EC_INT_L,           PIN(B, 9),  GPIO_OUT_HIGH)
#elif BOARD_REV == OAK_REV2
GPIO_INT(SOC_POWER_GOOD, PIN(A, 3),  GPIO_INT_BOTH | GPIO_PULL_DOWN,    power_signal_interrupt)
GPIO(EC_INT_L,           PIN(B, 9),  GPIO_OUT_HIGH)
GPIO(USB_VBUS_WAKE_L,    PIN(E, 1),  GPIO_INPUT)
#elif BOARD_REV == OAK_REV3
GPIO_INT(SOC_POWER_GOOD, PIN(A, 3),  GPIO_INT_BOTH | GPIO_PULL_DOWN,    power_signal_interrupt)
GPIO(EC_INT_L,           PIN(B, 9),  GPIO_OUT_HIGH)
GPIO(USB_C0_VBUS_WAKE_L, PIN(E, 1),  GPIO_INPUT)
GPIO(USB_C1_VBUS_WAKE_L, PIN(F, 2),  GPIO_INPUT)
#elif BOARD_REV >= OAK_REV4
GPIO_INT(SOC_POWER_GOOD, PIN(A, 3),  GPIO_INT_BOTH                 ,    power_signal_interrupt)
GPIO_INT(USB_BC12_INT,   PIN(E, 1),  GPIO_INT_FALLING, usb_evt)
GPIO(USB_C0_VBUS_WAKE_L, PIN(D, 12), GPIO_INPUT)
GPIO(USB_C1_VBUS_WAKE_L, PIN(F, 2),  GPIO_INPUT)
GPIO(EC_INT_L,           PIN(B, 9),  GPIO_ODR_HIGH)
#endif

/* Inputs without interrupt handlers */
GPIO(5V_POWER_GOOD, PIN(A, 1), GPIO_INPUT)
GPIO(EC_WAKE,       PIN(A, 0), GPIO_INPUT|GPIO_PULL_DOWN)
GPIO(WP_L,          PIN(B, 4), GPIO_INPUT)      /* Write protect input */
GPIO(BAT_PRESENT_L, PIN(E, 3), GPIO_INPUT|GPIO_PULL_UP)

/* Board version */
GPIO(BOARD_VERSION1, PIN(E, 10), GPIO_INPUT) /* Board ID 0 */
GPIO(BOARD_VERSION2, PIN(E,  9), GPIO_INPUT) /* Board ID 1 */
GPIO(BOARD_VERSION3, PIN(E, 12), GPIO_INPUT) /* Board ID 2 */
GPIO(BOARD_VERSION4, PIN(E, 11), GPIO_INPUT) /* Board ID 3 */

/* Outputs */
#if BOARD_REV < OAK_REV5
GPIO(BAT_LED0,          PIN(B, 11), GPIO_OUT_LOW)   /* LED_GREEN */
GPIO(BAT_LED1,          PIN(A, 11), GPIO_OUT_LOW)   /* LED_ORANGE or LED_RED(>rev3)*/
#else
GPIO(BAT_LED0,          PIN(A, 11), GPIO_OUT_LOW)   /* LED_GREEN */
GPIO(BAT_LED1,          PIN(B, 11), GPIO_OUT_LOW)   /* LED_ORANGE or LED_RED(>rev3)*/
#endif

#if (BOARD_REV == OAK_REV3) || (BOARD_REV == OAK_REV4)
GPIO(PWR_LED0,          PIN(F, 10), GPIO_OUT_LOW)   /* LED_GREEN */
GPIO(PWR_LED1,          PIN(F,  9), GPIO_OUT_LOW)   /* LED_ORANGE */
#else
UNIMPLEMENTED(PWR_LED0)
UNIMPLEMENTED(PWR_LED1)
#endif
GPIO(EC_BL_OVERRIDE,    PIN(F, 1),  GPIO_OUT_LOW)
GPIO(ENTERING_RW,       PIN(F, 0),  GPIO_OUT_LOW)


#if BOARD_REV == OAK_REV1
GPIO(AP_RESET_L,        PIN(C, 3),  GPIO_INPUT|GPIO_PULL_UP) /* AP reset signal from servo board */
GPIO(USB_C_BC12_SEL,    PIN(A, 14), GPIO_OUT_LOW)
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
GPIO(KB_OUT12,          PIN(A, 13), GPIO_KB_OUTPUT)
UNIMPLEMENTED(DP_SWITCH_CTL)

#elif BOARD_REV == OAK_REV2
GPIO(AP_RESET_L,        PIN(C, 3),  GPIO_INPUT|GPIO_PULL_UP) /* AP reset signal from servo board */
GPIO(USB_C_BC12_SEL,    PIN(D, 7),  GPIO_OUT_LOW)
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
GPIO(KB_OUT12,          PIN(D, 5),  GPIO_KB_OUTPUT)
GPIO(DP_MUX_EN_L,       PIN(E, 6),  GPIO_OUT_LOW)
GPIO(DP_SWITCH_CTL,     PIN(E, 5),  GPIO_OUT_LOW)

#elif BOARD_REV <= OAK_REV4 /* BOARD_REV 3 or 4 */
GPIO(AP_RESET_L,        PIN(C, 3),  GPIO_ODR_HIGH) /* Connect to the PMU_SYSRSTB */
GPIO(USB_C_BC12_SEL,    PIN(D, 7),  GPIO_OUT_LOW)
GPIO(KB_OUT00,          PIN(B, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT01,          PIN(B, 8),  GPIO_KB_OUTPUT)
GPIO(KB_OUT02,          PIN(B, 12), GPIO_OUT_LOW)   /* KSO2 is inverted */
GPIO(KB_OUT03,          PIN(D, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT04,          PIN(A, 8),  GPIO_KB_OUTPUT)
GPIO(KB_OUT05,          PIN(D, 4),  GPIO_KB_OUTPUT)
GPIO(KB_OUT06,          PIN(B, 2),  GPIO_KB_OUTPUT)
GPIO(KB_OUT07,          PIN(D, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT08,          PIN(D, 5),  GPIO_KB_OUTPUT)
GPIO(KB_OUT09,          PIN(B, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT10,          PIN(C, 5),  GPIO_KB_OUTPUT)
GPIO(KB_OUT11,          PIN(C, 4),  GPIO_KB_OUTPUT)
GPIO(KB_OUT12,          PIN(C, 2),  GPIO_KB_OUTPUT)
UNIMPLEMENTED(DP_SWITCH_CTL)

#else /* >= OAK_REV5 */
GPIO(AP_RESET_L,        PIN(C, 3),  GPIO_ODR_HIGH) /* Connect to the PMU_SYSRSTB */
GPIO(USB_C_BC12_SEL,    PIN(D, 7),  GPIO_OUT_LOW)
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
GPIO(C1_DP_HPD,         PIN(E,15),  GPIO_OUT_LOW)   /* inform PS8740 to exit from idle mode. */
GPIO(DP_SWITCH_CTL,     PIN(E, 5),  GPIO_OUT_LOW)
GPIO(EN_OTG_USB_A_PWR,  PIN(E, 4),  GPIO_OUT_HIGH)
GPIO(OTG_USB_A_ILIM_SEL,PIN(E, 2),  GPIO_OUT_HIGH)
GPIO(EC_IDDIG,          PIN(E,13),  GPIO_OUT_LOW)
GPIO(DP_MUX_EN_L,       PIN(E, 6),  GPIO_OUT_LOW)
GPIO(PARADE_MUX_EN,     PIN(E, 7),  GPIO_OUT_HIGH)
#endif /* BOARD_REV */

GPIO(SYSTEM_POWER_H,    PIN(B, 10), GPIO_OUT_LOW)
GPIO(PMIC_PWRON_H,      PIN(A, 12), GPIO_OUT_LOW)
GPIO(PMIC_WARM_RESET_H, PIN(B, 3),  GPIO_OUT_LOW)
#if BOARD_REV <= OAK_REV4
GPIO(LEVEL_SHIFT_EN_L,  PIN(D, 3),  GPIO_OUT_LOW)    /* LID/AC level shift */
GPIO(USB_C0_DEVMODE_L,  PIN(E, 4),  GPIO_OUT_HIGH)   /* set HSD2 (host mode) path as default */
GPIO(USB_C1_DEVMODE,    PIN(E, 2),  GPIO_OUT_LOW)    /* set HSD1 (host mode) path as default */
#else /* >= OAK_REV5 */
GPIO(LEVEL_SHIFT_EN_L,  PIN(F, 10), GPIO_OUT_LOW)    /* LID/AC level shift */
/* SPI MASTER. For SPI sensor */
GPIO(SPI2_NSS, PIN(D, 0),  GPIO_OUT_LOW)

#endif
GPIO(USB_PD_RST_L,      PIN(A, 15), GPIO_OUT_HIGH)   /* PD reset */
GPIO(USB_C0_5V_EN,      PIN(D, 8),  GPIO_OUT_LOW)    /* USBC port 0 5V */
GPIO(USB_C0_CHARGE_L,   PIN(D, 9),  GPIO_OUT_LOW)    /* USBC port 0 charge */
GPIO(USB_C1_5V_EN,      PIN(D, 10), GPIO_OUT_LOW)    /* USBC port 1 5V */
GPIO(USB_C1_CHARGE_L,   PIN(D, 11), GPIO_OUT_LOW)    /* USBC port 1 charge */
GPIO(USB_PD_VBUS_WAKE,  PIN(B, 15), GPIO_OUT_LOW)    /* PD MCU wake */
GPIO(USB_DP_HPD,        PIN(F, 3),  GPIO_OUT_LOW)

#if (BOARD_REV < OAK_REV5)
GPIO(TYPEC0_MUX_EN_L,   PIN(E, 13), GPIO_OUT_LOW)
GPIO(TYPEC1_MUX_EN_L,   PIN(E, 14), GPIO_OUT_LOW)
#endif

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

ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,       0) /* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(B, 0x00c0), 1, MODULE_I2C,        0) /* I2C MASTER:PB6/7 */
ALTERNATE(PIN_MASK(B, 0x6000), 5, MODULE_I2C,        0) /* I2C MASTER:PB13/14 */
ALTERNATE(PIN_MASK(A, 0x00f0), 0, MODULE_SPI,        0) /* SPI SLAVE:PA4/5/6/7 */
#if BOARD_REV >= OAK_REV5
ALTERNATE(PIN_MASK(D, 0x001A), 1, MODULE_SPI_MASTER, 0) /* SPI MASTER:PD1/3/4 */
#endif
