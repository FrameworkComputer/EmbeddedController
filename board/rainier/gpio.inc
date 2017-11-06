/* -*- mode:c -*-
 *
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first.
 */

GPIO_INT(SPI1_NSS,         PIN(A, 15), GPIO_INT_BOTH,
         spi_event)
GPIO_INT(USB_C0_PD_INT_L,  PIN(C, 13), GPIO_INT_FALLING | GPIO_PULL_UP,
         tcpc_alert_event)
GPIO_INT(VOLUME_UP_L,      PIN(D, 10), GPIO_INT_BOTH | GPIO_PULL_UP,
         button_interrupt)
GPIO_INT(VOLUME_DOWN_L,    PIN(E, 11), GPIO_INT_BOTH | GPIO_PULL_UP,
         button_interrupt)
GPIO_INT(POWER_BUTTON_L,   PIN(A, 0),  GPIO_INT_BOTH | GPIO_PULL_UP,
         power_button_interrupt)
GPIO_INT(PP1250_S3_PG,     PIN(D, 8),  GPIO_INT_BOTH | GPIO_PULL_UP,
         power_signal_interrupt)
GPIO_INT(PP900_S0_PG,      PIN(D, 9),  GPIO_INT_BOTH | GPIO_PULL_UP,
         power_signal_interrupt)
GPIO_INT(AP_EC_S3_S0_L,    PIN(C, 7),  GPIO_INT_BOTH | GPIO_PULL_DOWN,
         power_signal_interrupt)
GPIO_INT(WARM_RESET_REQ,   PIN(E, 1),  GPIO_INT_RISING | GPIO_PULL_DOWN,
         warm_reset_request_interrupt)
GPIO_INT(AP_OVERTEMP,      PIN(E, 4),  GPIO_INT_RISING | GPIO_PULL_DOWN,
         overtemp_interrupt)
GPIO_INT(ACCEL_INT_L,      PIN(D, 14), GPIO_INT_FALLING | GPIO_SEL_1P8V,
         bmi160_interrupt)

/* Voltage rails control pins */
GPIO(PP1800_S0_EN,         PIN(D, 11), GPIO_OUT_LOW)
GPIO(AP_CORE_EN,           PIN(C, 1),  GPIO_OUT_LOW)
GPIO(PP3300_S0_EN,         PIN(E, 12), GPIO_OUT_LOW)
GPIO(PP1800_USB_EN,        PIN(C, 4),  GPIO_OUT_LOW)
GPIO(PP900_S0_EN,          PIN(E, 8),  GPIO_OUT_LOW)
GPIO(PP1250_S3_EN,         PIN(D, 13), GPIO_OUT_LOW)
GPIO(PP1800_S3_EN,         PIN(C, 3),  GPIO_OUT_LOW)
GPIO(PP3300_S3_EN,         PIN(E, 2),  GPIO_OUT_LOW)
GPIO(PP900_S3_EN,          PIN(E, 10), GPIO_OUT_LOW)

GPIO(PP3300_REDUCE_EFF_L,  PIN(D, 12), GPIO_ODR_HIGH)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C0_SCL,             PIN(B, 8),  GPIO_INPUT)
GPIO(I2C0_SDA,             PIN(B, 9),  GPIO_INPUT)
GPIO(I2C1_SCL,             PIN(B, 10), GPIO_INPUT)
GPIO(I2C1_SDA,             PIN(B, 11), GPIO_INPUT)

/* Analog pins */
GPIO(BOARD_ID,             PIN(C, 0),  GPIO_ANALOG)

/* SPI sensors */
GPIO(SPI_BARO_CS_L,        PIN(B, 12), GPIO_OUT_HIGH)
GPIO(SPI_ACCEL_CS_L,       PIN(D, 0),  GPIO_OUT_HIGH)

/* Other input pins */
GPIO(WP_L,                 PIN(E, 5),  GPIO_INPUT)
GPIO(CCD_MODE_ODL,         PIN(C, 5),  GPIO_INPUT | GPIO_PULL_UP)
GPIO(CHARGER_INT_L,        PIN(E, 6),  GPIO_INPUT | GPIO_PULL_UP)
/* Non-INT power signal pin */
GPIO(AP_CORE_PG,           PIN(D, 7),  GPIO_INPUT | GPIO_PULL_UP)



/* Other output pins */
GPIO(ENTERING_RW,          PIN(C, 6),  GPIO_ODR_HIGH)
GPIO(SYS_RST_L,            PIN(C, 8),  GPIO_ODR_HIGH)
GPIO(EC_INT_L,             PIN(E, 3),  GPIO_ODR_HIGH)
GPIO(EC_BOARD_ID_EN_L,     PIN(F, 1),  GPIO_ODR_HIGH)
GPIO(USB_C0_DISCHARGE,     PIN(A, 11), GPIO_OUT_LOW)
GPIO(PCA9468_EN,           PIN(E, 15), GPIO_OUT_LOW)

/* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,       0)
/* I2C MASTER: PB8/9 */
ALTERNATE(PIN_MASK(B, 0x0300), 1, MODULE_I2C,        0)
/* I2C MASTER: PB10/11 */
ALTERNATE(PIN_MASK(B, 0x0c00), 1, MODULE_I2C,        0)
/* SPI SLAVE: PB3/4/5 */
ALTERNATE(PIN_MASK(B, 0x0038), 0, MODULE_SPI,        0)
/* SPI SLAVE CS: PA15 */
ALTERNATE(PIN_MASK(A, 0x8000), 0, MODULE_SPI,        0)
/* SPI MASTER: PD1/3/4 */
ALTERNATE(PIN_MASK(D, 0x001a), 1, MODULE_SPI_MASTER, 0)
