/* -*- mode:c -*-
 *
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

#ifdef SECTION_IS_RW
GPIO_INT(HOST_USBC_PPC_INT_ODL,  PIN(D,  9),      GPIO_INT_FALLING | GPIO_PULL_UP, ppc_interrupt)
GPIO_INT(DDI_MST_IN_HPD,         PIN(C,  14),     GPIO_INT_BOTH, hpd_interrupt)
GPIO_INT(USBC_UF_MUX_VBUS_EN,    PIN(C,  12),     GPIO_INT_BOTH, board_uf_manage_vbus_interrupt)
GPIO_INT(PWR_BTN,                PIN(A,  0),      GPIO_INT_BOTH, board_pwr_btn_interrupt)
GPIO_INT(USBC_UF_PPC_INT_ODL,    PIN(B,  5),      GPIO_INT_FALLING | GPIO_PULL_UP, board_usbc_usb3_interrupt)
#endif

/* Power sequencing signals */
GPIO(EN_AC_JACK,                    PIN(A,  1), GPIO_OUT_LOW)
GPIO(EN_BB,                         PIN(A,  8), GPIO_OUT_LOW)
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
GPIO(MST_HUB_LANE_SWITCH,           PIN(C, 15), GPIO_OUT_HIGH)
GPIO(MST_UFP_PLUG_DET,              PIN(B, 12), GPIO_OUT_HIGH)

/* Display Demux signals */
GPIO(DEMUX_DP_HDMI_MODE,            PIN(E, 15), GPIO_OUT_LOW)
GPIO(DEMUX_DP_HDMI_PD_N,            PIN(B, 13), GPIO_ODR_LOW)

/* USBC Mux and Demux Signals */
GPIO(EN_DP_SINK,                    PIN(B, 14), GPIO_OUT_LOW)
GPIO(DP_SINK_RESET,                 PIN(B, 15), GPIO_OUT_LOW)
GPIO(USBC_UF_RESET_L,               PIN(D,  2), GPIO_ODR_LOW)

/* USB Hubs signals */
GPIO(EC_HUB2_RESET_L,               PIN(C,  5), GPIO_ODR_LOW)
GPIO(EC_HUB3_RESET_L,               PIN(B, 10), GPIO_ODR_LOW)
GPIO(USBC_DATA_OCP_NOTIFY,          PIN(C,  2), GPIO_OUT_HIGH)
GPIO(HUB_BPWRDET,                   PIN(C,  1), GPIO_OUT_LOW)

/* USB-A Current limit switches, set default to 1.5A */
GPIO(GBE_RESET_EC,                  PIN(C,  0), GPIO_OUT_LOW)
GPIO(EC_DFU_MUX_CTRL,               PIN(C,  3), GPIO_OUT_HIGH)
GPIO(USB3_A5_CDP_EN,                PIN(B,  9), GPIO_OUT_LOW)
GPIO(USB3_A6_CDP_EN,                PIN(C, 13), GPIO_OUT_LOW)


/* Write protect */
GPIO(EC_FLASH_WP_ODL,               PIN(A,  3), GPIO_ODR_HIGH)
GPIO(EC_WP_L,                       PIN(E, 11), GPIO_INT_BOTH)

/* UART Bus */
GPIO(EC_UART_TX,                    PIN(C, 10), GPIO_INT_BOTH)
GPIO(EC_UART_RX,                    PIN(C, 11), GPIO_INT_BOTH)

/*
 * I2C SCL/SDA pins. These will normally be under control of the peripheral from
 * alt function setting below. But if a port gets wedged, the unwedge code uses
 * these signals as regular GPIOs.
 */
GPIO(EC_I2C1_SCL,                   PIN(A, 15),  GPIO_ODR_HIGH)
GPIO(EC_I2C1_SDA,                   PIN(B,  7),  GPIO_ODR_HIGH)
GPIO(EC_I2C3_SCL,                   PIN(C,  8),  GPIO_ODR_HIGH)
GPIO(EC_I2C3_SDA,                   PIN(C,  9),  GPIO_ODR_HIGH)

/* misc signals */
GPIO(BOOT0,                         PIN(B,  8),  GPIO_INPUT)

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
ALTERNATE(PIN_MASK(C, 0x0300),  8, MODULE_I2C,   GPIO_OPEN_DRAIN)
/* GPIOA4-7: SPI Signals */
ALTERNATE(PIN_MASK(A, 0x00F0),  5, MODULE_SPI,   0)
