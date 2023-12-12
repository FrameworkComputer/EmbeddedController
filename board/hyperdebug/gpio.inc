/* -*- mode:c -*-
 *
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * List of all GPIO pins available for Host computer to manipulate.
 * The are named based on their location on the two 70-pin DIL
 * connectors on either side of the HyperDebug board.  Pins with
 * special functions are commented out, and declared with relevant
 * symbolic name further below.
 */

/*
 * The common define GPIO_INT_BOTH includes GPIO_INPUT, which interferes with
 * GPIO_ALTERNATE.  Instead we want to declare the intent to detect either
 * rising or falling edges, making it conditional on the pins possibly later
 * being re-configured as GPIO inputs.
 */
#define ANY_EDGE (GPIO_INT_F_RISING | GPIO_INT_F_FALLING)

GPIO_INT(CN7_1,			PIN(C, 6),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN7_2,			PIN(B, 8),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* I2C1 SCL */
GPIO_INT(CN7_3,			PIN(D, 11), GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN7_4,			PIN(B, 9),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* I2C1 SDA */
GPIO_INT(CN7_5,			PIN(B, 13), GPIO_INPUT | ANY_EDGE, gpio_edge)
/* CN7_6 is VREFP */
GPIO_INT(CN7_7,			PIN(D, 12), GPIO_INPUT | ANY_EDGE, gpio_edge)
/* CN7_8 is GND */
GPIO_INT(CN7_9,			PIN(A, 4),  GPIO_INPUT | ANY_EDGE, gpio_edge) /* DAC0, ADC12_IN9 */
GPIO_INT(CN7_10,		PIN(A, 5),  GPIO_INPUT | ANY_EDGE, gpio_edge) /* DAC1, ADC12_IN10 */
GPIO_INT(CN7_11,		PIN(B, 4),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN7_12,		PIN(A, 6),  GPIO_INPUT | ANY_EDGE, gpio_edge) /* SPI1 HIDO, ADC12_IN11 */
/*GPIO_INT(CN7_13,		PIN(B, 5),  GPIO_INPUT | ANY_EDGE, gpio_edge) Nucleo USB-C */
GPIO_INT(CN7_14,		PIN(A, 7),  GPIO_INPUT | ANY_EDGE, gpio_edge) /* SPI1 HODI, ADC12_IN12 */
GPIO_INT(CN7_15,		PIN(B, 3),  GPIO_INPUT | ANY_EDGE, gpio_edge) /* SPI1 CLK */
GPIO_INT(CN7_16,		PIN(D, 14), GPIO_INPUT | ANY_EDGE, gpio_edge)
/*GPIO_INT(CN7_17,		PIN(A, 4),  GPIO_INPUT | ANY_EDGE, gpio_edge)*/
GPIO_INT(CN7_18,		PIN(D, 15), GPIO_INPUT | ANY_EDGE, gpio_edge)
/*GPIO_INT(CN7_19,		PIN(B, 4),  GPIO_INPUT | ANY_EDGE, gpio_edge)*/
GPIO_INT(CN7_20,		PIN(F, 12), GPIO_INPUT | ANY_EDGE, gpio_edge)

/* CN8_1 is NC */
GPIO_INT(CN8_2,			PIN(C, 8),  GPIO_INPUT | ANY_EDGE, gpio_edge)
/* CN8_3 is IOREF */
GPIO_INT(CN8_4,			PIN(C, 9),  GPIO_INPUT | ANY_EDGE, gpio_edge)
/* CN8_5 is NRST */
GPIO_INT(CN8_6,			PIN(C, 10), GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* UART4 TX */
/* CN8_7 is 3V3 */
GPIO_INT(CN8_8,			PIN(C, 11), GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* UART4 RX */
/* CN8_9 is 5V */
GPIO_INT(CN8_10,		PIN(C, 12), GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* UART5 TX */
/* CN8_11 is GND */
GPIO_INT(CN8_12,		PIN(D, 2),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* UART5 RX */
/* CN8_13 is GND */
GPIO_INT(CN8_14,		PIN(F, 3),  GPIO_INPUT | ANY_EDGE, gpio_edge)
/* CN8_15 is VIN */
GPIO_INT(CN8_16,		PIN(F, 5),  GPIO_INPUT | ANY_EDGE, gpio_edge)

GPIO_INT(CN9_1,			PIN(A, 3),  GPIO_INPUT | ANY_EDGE, gpio_edge)     /* ADC12_IN8 */
GPIO_INT(CN9_2,			PIN(D, 7),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN9_3,			PIN(A, 2),  GPIO_INPUT | ANY_EDGE, gpio_edge)     /* ADC12_IN7 */
GPIO_INT(CN9_4,			PIN(D, 6),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* UART2 RX */
GPIO_INT(CN9_5,			PIN(C, 3),  GPIO_INPUT | ANY_EDGE, gpio_edge)     /* ADC12_IN4 */
GPIO_INT(CN9_6,			PIN(D, 5),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* UART2 TX */
GPIO_INT(CN9_7,			PIN(B, 0),  GPIO_INPUT | ANY_EDGE, gpio_edge)     /* ADC12_IN15 */
GPIO_INT(CN9_8,			PIN(D, 4),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* SPI2 HODI */
GPIO_INT(CN9_9,			PIN(C, 1),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* I2C3 SDA, ADC12_IN2 */
GPIO_INT(CN9_10,		PIN(D, 3),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* SPI2 HIDO */
GPIO_INT(CN9_11,		PIN(C, 0),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* I2C3 SCL, ADC12_IN1 */
/* CN9_12 is GND */
GPIO_INT(CN9_13,		PIN(B, 2),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN9_14,		PIN(E, 2),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN9_15,		PIN(B, 6),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN9_16,		PIN(E, 4),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN9_17,		PIN(F, 2),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN9_18,		PIN(E, 5),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN9_19,		PIN(F, 1),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* I2C2 SCL */
GPIO_INT(CN9_20,		PIN(E, 6),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN9_21,		PIN(F, 0),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* I2C2 SDA */
GPIO_INT(CN9_22,		PIN(E, 3),  GPIO_INPUT | ANY_EDGE, gpio_edge)
/* CN9_23 is GND */
GPIO_INT(CN9_24,		PIN(F, 8),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN9_25,		PIN(D, 0),  GPIO_OUT_HIGH | ANY_EDGE, gpio_edge) /* SPI2 CS */
GPIO_INT(CN9_26,		PIN(F, 7),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN9_27,		PIN(D, 1),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* SPI2 CLK */
GPIO_INT(CN9_28,		PIN(F, 9),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN9_29,		PIN(G, 0),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN9_30,		PIN(G, 1),  GPIO_INPUT | ANY_EDGE, gpio_edge)

/* CN10_1 is AVDD */
GPIO_INT(CN10_2,		PIN(F, 13), GPIO_INPUT | ANY_EDGE, gpio_edge)
/* CN10_3 is AGND */
GPIO_INT(CN10_4,		PIN(E, 9),  GPIO_INPUT | ANY_EDGE, gpio_edge)
/* CN10_5 is GND */
GPIO_INT(CN10_6,		PIN(E, 11), GPIO_OUT_HIGH | ANY_EDGE, gpio_edge) /* QSPI CS */
GPIO_INT(CN10_7,		PIN(B, 1),  GPIO_INPUT | ANY_EDGE, gpio_edge)    /* ADC12_IN16 */
GPIO_INT(CN10_8,		PIN(F, 14), GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* I2C4 SCL */
/*GPIO_INT(CN10_9,		PIN(C, 2),  GPIO_INPUT | ANY_EDGE, gpio_edge) Nucleo USB VBUS sense, ADC12_IN3 */
GPIO_INT(CN10_10,		PIN(E, 13), GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* QSPI D1 */
GPIO_INT(CN10_11,		PIN(A, 1),  GPIO_INPUT | ANY_EDGE, gpio_edge)     /* ADC12_IN6 */
GPIO_INT(CN10_12,		PIN(F, 15), GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* I2C4 SDA */
/*GPIO_INT(CN10_13,		PIN(A, 2),  GPIO_INPUT | ANY_EDGE, gpio_edge)*/
GPIO_INT(CN10_14,		PIN(D, 8),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* UART3 TX */
GPIO_INT(CN10_15,		PIN(B, 10), GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN10_16,		PIN(D, 9),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* UART3 RX */
/* CN10_17 is GND */
GPIO_INT(CN10_18,		PIN(E, 8),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN10_19,		PIN(E, 15), GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* QSPI D3 */
GPIO_INT(CN10_20,		PIN(E, 7),  GPIO_INPUT | ANY_EDGE, gpio_edge)
/*GPIO_INT(CN10_21,		PIN(B, 0),  GPIO_INPUT | ANY_EDGE, gpio_edge)*/
/* CN10_22 is GND */
GPIO_INT(CN10_23,		PIN(E, 12), GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* QSPI D0 */
GPIO_INT(CN10_24,		PIN(E, 10), GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* QSPI CLK */
GPIO_INT(CN10_25,		PIN(E, 14), GPIO_ALTERNATE | ANY_EDGE, gpio_edge) /* QSPI D2 */
/*GPIO_INT(CN10_26,		PIN(E, 12), GPIO_INPUT | ANY_EDGE, gpio_edge) QSPI */
/* CN10_27 is GND */
/*GPIO_INT(CN10_28,		PIN(E, 14), GPIO_INPUT | ANY_EDGE, gpio_edge) QSPI */
GPIO_INT(CN10_29,		PIN(A, 0),  GPIO_INPUT | ANY_EDGE, gpio_edge)     /* ADC12_IN5 */
/*GPIO_INT(CN10_30,		PIN(E, 15), GPIO_INPUT | ANY_EDGE, gpio_edge) QSPI */
GPIO_INT(CN10_31,		PIN(A, 8),  GPIO_INPUT | ANY_EDGE, gpio_edge)
/*GPIO_INT(CN10_32,		PIN(B, 10), GPIO_INPUT | ANY_EDGE, gpio_edge)*/
GPIO_INT(CN10_33,		PIN(E, 0),  GPIO_INPUT | ANY_EDGE, gpio_edge)
GPIO_INT(CN10_34,		PIN(B, 11), GPIO_INPUT | ANY_EDGE, gpio_edge)

/* These pins are used for USART and are set to alternate mode below */
GPIO_INT(HYPER_CONSOLE_TX,	PIN(G, 7),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge)
GPIO_INT(HYPER_CONSOLE_RX,	PIN(G, 8),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge)

/* USB pins */
GPIO(USB_FS_DM,			PIN(A, 11), GPIO_ALTERNATE)
GPIO(USB_FS_DP,			PIN(A, 12), GPIO_ALTERNATE)

/* Signals for hardware on the Nucleo board itself */
GPIO_INT(NUCLEO_LED1,		PIN(C, 7),  GPIO_OUT_HIGH | ANY_EDGE, gpio_edge) /* Green */
GPIO_INT(NUCLEO_LED2,		PIN(B, 7),  GPIO_OUT_LOW | ANY_EDGE, gpio_edge) /* Blue */
GPIO_INT(NUCLEO_LED3,		PIN(A, 9),  GPIO_OUT_LOW | ANY_EDGE, gpio_edge) /* Red */
GPIO_INT(NUCLEO_USER_BTN,	PIN(C, 13), GPIO_INPUT | ANY_EDGE, user_button_edge)

/* Unimplemented signals since we are not an EC */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)


ALTERNATE(PIN_MASK(A, 0x1800), 10, MODULE_USB, 0) /* USB: PA11/12 */

ALTERNATE(PIN_MASK(D, 0x0060), 7, MODULE_UART, 0) /* USART2: PD5/PD6 - OT UART */
ALTERNATE(PIN_MASK(C, 0x0C00), 8, MODULE_UART, 0) /* USART4: PC10/PC11 - AP UART */
ALTERNATE(PIN_MASK(D, 0x0300), 7, MODULE_UART, 0) /* USART3: PD8/PD9 - EC UART */
ALTERNATE(PIN_MASK(C, 0x1000), 8, MODULE_UART, 0) /* USART5: PC12 - FP MCU UART */
ALTERNATE(PIN_MASK(D, 0x0004), 8, MODULE_UART, 0) /* USART5: PD2 - FP MCU UART */
ALTERNATE(PIN_MASK(G, 0x0180), 8, MODULE_UART, 0) /* LPUART1: PG7/PG8 - HyperDebug console */

ALTERNATE(PIN_MASK(F, 0x0003), 4, MODULE_I2C, GPIO_OPEN_DRAIN) /* I2C2: PF0/PF1 */
ALTERNATE(PIN_MASK(B, 0x0300), 4, MODULE_I2C, GPIO_OPEN_DRAIN) /* I2C1: PB8/PB9 */
ALTERNATE(PIN_MASK(C, 0x0003), 4, MODULE_I2C, GPIO_OPEN_DRAIN) /* I2C3: PC0/PC1 */
ALTERNATE(PIN_MASK(F, 0xC000), 4, MODULE_I2C, GPIO_OPEN_DRAIN) /* I2C4: PF14/PF15 */
ALTERNATE(PIN_MASK(E, 0xF400), 10, MODULE_SPI, 0) /* QSPI: PE10/PE12-15 */
/* ALTERNATE(PIN_MASK(A, 0x00C0), 5, MODULE_SPI, 0) SPI1: PA6/PA7 HIDO/HODI */
/* ALTERNATE(PIN_MASK(B, 0x0008), 5, MODULE_SPI, 0) SPI1: PB3 SCK */
ALTERNATE(PIN_MASK(D, 0x001A), 5, MODULE_SPI, 0) /* SPI2: PD1/PD3/PD4 SCK/HIDO/HODI */
