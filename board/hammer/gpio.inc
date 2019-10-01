/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

#ifdef SECTION_IS_RW
#ifndef HAS_NO_TOUCHPAD
GPIO_INT(TOUCHPAD_INT, PIN(B, 8), GPIO_INT_FALLING, touchpad_interrupt)
#endif /* !HAS_NO_TOUCHPAD */
#ifdef CONFIG_GMR_TABLET_MODE
GPIO_INT(TABLET_MODE_L, PIN(B, 11), GPIO_PULL_UP | GPIO_INT_BOTH, gmr_tablet_switch_isr)
#endif /* CONFIG_GMR_TABLET_MODE */
#endif /* SECTION_IS_RW */

/* Keyboard inputs */
#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT GPIO_ODR_HIGH

#ifdef BOARD_MASTERBALL
GPIO_INT(KB_IN00, PIN(B, 2),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN01, PIN(B, 13), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN02, PIN(B, 1),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN03, PIN(A, 6),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN04, PIN(B, 10), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN05, PIN(B, 12), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN06, PIN(A, 7),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN07, PIN(B, 0),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)

/* Do not forget to update KB_OUT_PORT_LIST to match this. */
GPIO(KB_OUT00,			PIN(C, 15), GPIO_KB_OUTPUT)
GPIO(KB_OUT01,			PIN(C, 14), GPIO_KB_OUTPUT)
GPIO(KB_OUT02,			PIN(A, 2),  GPIO_KB_OUTPUT)
GPIO(KB_OUT03,			PIN(A, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT04,			PIN(F, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT05,			PIN(A, 3),  GPIO_KB_OUTPUT)
GPIO(KB_OUT06,			PIN(A, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT07,			PIN(F, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT08,			PIN(A, 5),  GPIO_KB_OUTPUT)
GPIO(KB_OUT09,			PIN(A, 4),  GPIO_KB_OUTPUT)
GPIO(KB_OUT10,			PIN(B, 14), GPIO_KB_OUTPUT)
GPIO(KB_OUT11,			PIN(B, 15), GPIO_KB_OUTPUT)
GPIO(KB_OUT12,			PIN(A, 8),  GPIO_KB_OUTPUT)
#else /* !BOARD_MASTERBALL */
GPIO_INT(KB_IN00, PIN(A, 4),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
#ifdef BOARD_WHISKERS
GPIO_INT(KB_IN01, PIN(B, 10),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
#else
GPIO_INT(KB_IN01, PIN(B, 3),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
#endif
GPIO_INT(KB_IN02, PIN(B, 0),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN03, PIN(A, 7),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN04, PIN(B, 12), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN05, PIN(B, 2),  GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN06, PIN(B, 14), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)
GPIO_INT(KB_IN07, PIN(B, 15), GPIO_KB_INPUT, keyboard_raw_gpio_interrupt)

/* Do not forget to update KB_OUT_PORT_LIST to match this. */
GPIO(KB_OUT00,			PIN(B, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT01,			PIN(A, 5),  GPIO_KB_OUTPUT)
#ifdef BOARD_WAND
GPIO(KB_OUT02,			PIN(A, 8),  GPIO_KB_OUTPUT)
GPIO(KB_OUT03,			PIN(B, 4),  GPIO_KB_OUTPUT)
#else
GPIO(KB_OUT02,			PIN(A, 3),  GPIO_KB_OUTPUT)
GPIO(KB_OUT03,			PIN(A, 2),  GPIO_KB_OUTPUT)
#endif
GPIO(KB_OUT04,			PIN(A, 6),  GPIO_KB_OUTPUT)
GPIO(KB_OUT05,			PIN(A, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT06,			PIN(A, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT07,			PIN(B, 13), GPIO_KB_OUTPUT)
GPIO(KB_OUT08,			PIN(C, 14), GPIO_KB_OUTPUT)
GPIO(KB_OUT09,			PIN(C, 15), GPIO_KB_OUTPUT)
GPIO(KB_OUT10,			PIN(F, 1),  GPIO_KB_OUTPUT)
GPIO(KB_OUT11,			PIN(F, 0),  GPIO_KB_OUTPUT)
GPIO(KB_OUT12,			PIN(C, 13), GPIO_KB_OUTPUT)
#endif /* !BOARD_MASTERBALL */

/* I2C pins should be configured as inputs until I2C module is    */
/* initialized. This will avoid driving the lines unintentionally.*/
GPIO(MASTER_I2C_SCL,		PIN(B, 6), GPIO_INPUT)
GPIO(MASTER_I2C_SDA,		PIN(B, 7), GPIO_INPUT)
#ifndef HAS_NO_TOUCHPAD
/* TODO(crosbug.com/p/59083): Disable trackpad when appropriate */
#ifdef HAS_EN_PP3300_TP_ACTIVE_HIGH
GPIO(EN_PP3300_TP,		PIN(A, 14), GPIO_OUT_HIGH)
#else
GPIO(EN_PP3300_TP_ODL,		PIN(A, 14), GPIO_OUT_LOW)
#endif
#endif /* !HAS_NO_TOUCHPAD */

#ifdef HAS_BACKLIGHT
GPIO(KEYBOARD_BACKLIGHT,	PIN(B, 9), GPIO_INPUT)
#endif

GPIO(WP_L,			PIN(A, 13), GPIO_INPUT | GPIO_PULL_UP)

#ifdef BOARD_WAND
GPIO(BASE_UART_TX_RX,		PIN(A, 2), GPIO_ODR_HIGH)

GPIO(CHARGER_I2C_SCL,		PIN(B, 10), GPIO_INPUT)
GPIO(CHARGER_I2C_SDA,		PIN(B, 11), GPIO_INPUT)

GPIO(SWITCH_STATUS,		PIN(A, 15), GPIO_INPUT)
GPIO(EN_OTG,			PIN(B, 5), GPIO_INPUT)
#elif defined(BOARD_WHISKERS)
GPIO(DETECT_PATH_DISABLE_L,	PIN(A, 8), GPIO_ODR_LOW)
GPIO(SPI1_NSS,			PIN(A, 15), GPIO_OUT_HIGH)
GPIO(BACKLIGHT_EN,		PIN(B, 4), GPIO_ODR_HIGH | GPIO_PULL_UP)
#else
GPIO(BASE_DET,			PIN(A, 15), GPIO_INPUT)
#endif

/* Unimplemented signals since we are not an EC */
UNIMPLEMENTED(ENTERING_RW)

ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART, GPIO_PULL_UP) /* USART1: PA09/PA10 - Servo stm32 console UART */

#ifdef BOARD_WAND
ALTERNATE(PIN_MASK(A, 0x0004), 1, MODULE_USART, GPIO_ODR_HIGH) /* USART2: PA2 - EC-EC UART */
ALTERNATE(PIN_MASK(B, 0x0c00), 1, MODULE_I2C, 0) /* I2C CHARGER: PB10/11  GPIO_ODR_HIGH */
#endif

#ifdef HAS_SPI_TOUCHPAD
ALTERNATE(PIN_MASK(B, 0x0038), 0, MODULE_SPI_MASTER, 0) /* SPI MASTER:PB3/4/5 */
#endif

#ifdef CONFIG_I2C
ALTERNATE(PIN_MASK(B, 0x00c0), 1, MODULE_I2C, 0) /* I2C TOUCHPAD: PB6/7 GPIO_ODR_HIGH */
#endif

#ifdef HAS_BACKLIGHT
ALTERNATE(PIN_MASK(B, 0x0200), 2, MODULE_PWM, GPIO_PULL_DOWN) /* PWM: PB9 */
#endif
