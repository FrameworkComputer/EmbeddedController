/* -*- mode:c -*-
 *
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Power sequencing signals */
GPIO(PWR_BTN,                       PIN(A,  0), GPIO_INPUT)
GPIO(EN_AC_JACK,                    PIN(A,  1), GPIO_OUT_LOW)
GPIO(EN_BB,                         PIN(A, 10), GPIO_OUT_LOW)
GPIO(EN_PP3300_B,                   PIN(B,  2), GPIO_OUT_LOW)
GPIO(EN_PP5000_A,                   PIN(C,  6), GPIO_OUT_LOW)
GPIO(EN_PP1200_A,                   PIN(E,  8), GPIO_OUT_LOW)
GPIO(EN_PP1100_A,                   PIN(C,  7), GPIO_OUT_LOW)
GPIO(EN_PP1050_A,                   PIN(A,  2), GPIO_OUT_LOW)
GPIO(EN_PP5000_C,                   PIN(D,  1), GPIO_OUT_LOW)
GPIO(EN_PP5000_HSPORT,              PIN(D,  0), GPIO_OUT_LOW)

/* MST Hub signals */
GPIO(MST_LP_CTL_L,                  PIN(D, 10), GPIO_ODR_LOW)
GPIO(MST_RST_L,                     PIN(E, 14), GPIO_ODR_LOW)

/* Display Demux signals */
GPIO(DEMUX_DUAL_DP_MODE,            PIN(B,  0), GPIO_OUT_LOW)
GPIO(DEMUX_DP_HDMI_MODE,            PIN(E, 15), GPIO_OUT_LOW)
GPIO(DEMUX_DUAL_DP_RESET_N,         PIN(E, 13), GPIO_ODR_LOW)
GPIO(DEMUX_DUAL_DP_PD_N,            PIN(E, 12), GPIO_ODR_LOW)
GPIO(DEMUX_DP_HDMI_PD_N,            PIN(B, 13), GPIO_ODR_LOW)

/* USBC Mux and Demux Signals */
GPIO(EN_DP_SINK,                    PIN(B, 14), GPIO_OUT_LOW)
GPIO(DP_SINK_RESET,                 PIN(B, 15), GPIO_OUT_LOW)
GPIO(USBC_DP_PD_RST_L,              PIN(E,  9), GPIO_ODR_LOW)
GPIO(USBC_UF_RESET_L,               PIN(D,  2), GPIO_ODR_LOW)

/* USB Hubs signals */
GPIO(EC_HUB2_RESET_L,               PIN(C,  5), GPIO_ODR_LOW)
GPIO(EC_HUB3_RESET_L,               PIN(C, 10), GPIO_ODR_LOW)

/* Write protect */
GPIO(EC_FLASH_WP_ODL,               PIN(A,  3), GPIO_ODR_HIGH)
GPIO(EC_WP_L,                       PIN(E, 11), GPIO_INT_BOTH)

/*
 * I2C SCL/SDA pins. These will normally be under control of the peripheral from
 * alt fucntion setting below. But if a port gets wedged, the unwedge code uses
 * these signals as regular GPIOs.
 */
GPIO(EC_I2C1_SCL,                   PIN(A, 15),  GPIO_ODR_HIGH)
GPIO(EC_I2C1_SDA,                   PIN(B,  7),  GPIO_ODR_HIGH)
GPIO(EC_I2C2_SDA,                   PIN(A,  8),  GPIO_ODR_HIGH)
GPIO(EC_I2C2_SCL,                   PIN(A,  9),  GPIO_ODR_HIGH)
GPIO(EC_I2C3_SCL,                   PIN(C,  8),  GPIO_ODR_HIGH)
GPIO(EC_I2C3_SDA,                   PIN(C,  9),  GPIO_ODR_HIGH)

/* Unimplemented signals since we are not an EC */
UNIMPLEMENTED(ENTERING_RW)

/* USART3_TX/RX GPIOC 10-11*/
ALTERNATE(PIN_MASK(C, 0x0C00),  7, MODULE_UART,  GPIO_PULL_UP)
/* I2C Ports
 *   I2C1: SDA/SCL -> PB7/PA15
 *   I2C2: SDA/SCL -> PA8/PA9
 *   I2C3: SDA/SCL -> PC8/PC9
 */
ALTERNATE(PIN_MASK(B, 0x0080),  4, MODULE_I2C,   GPIO_OPEN_DRAIN)
ALTERNATE(PIN_MASK(A, 0X8000),  4, MODULE_I2C,   GPIO_OPEN_DRAIN)
ALTERNATE(PIN_MASK(A, 0x0300),  4, MODULE_I2C,   GPIO_OPEN_DRAIN)
ALTERNATE(PIN_MASK(C, 0x0300),  8, MODULE_I2C,   GPIO_OPEN_DRAIN)
/* GPIOA4-7: SPI Signals */
ALTERNATE(PIN_MASK(A, 0x00F0),  5, MODULE_SPI,   0)
