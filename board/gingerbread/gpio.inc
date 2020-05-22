/* -*- mode:c -*-
 *
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Power sequencing interrupts */
GPIO(PWR_BTN,                       PIN(A, 0),  GPIO_INPUT)
GPIO(EN_AC_JACK,                    PIN(A, 1),  GPIO_OUT_LOW)
GPIO(EN_BB,                         PIN(C, 0),  GPIO_OUT_LOW)
GPIO(EN_PP3300_A,                   PIN(C, 10), GPIO_OUT_LOW)
GPIO(EN_PP5000_A,                   PIN(C, 2),  GPIO_OUT_LOW)
GPIO(EN_PP1200_A,                   PIN(C, 3),  GPIO_OUT_LOW)
GPIO(EN_PP1100_A,                   PIN(C, 4),  GPIO_OUT_LOW)
GPIO(EN_PP1000_A,                   PIN(C, 5),  GPIO_OUT_LOW)
GPIO(EN_PP1050_A,                   PIN(C, 6),  GPIO_OUT_LOW)
GPIO(EN_PP5000_HSPORT,              PIN(C, 7),  GPIO_OUT_LOW)
GPIO(STATUS_LED1,                   PIN(A, 2),  GPIO_OUT_HIGH)
GPIO(STATUS_LED2,                   PIN(B, 12), GPIO_OUT_HIGH)

/* MST Hub signals */
GPIO(MST_LP_CTL_L,                  PIN(B, 9),  GPIO_ODR_LOW)
GPIO(MST_RST_L,                     PIN(B, 3),  GPIO_ODR_LOW)

/* Display Demux signals */
GPIO(DEMUX_DUAL_DP_MODE,            PIN(D, 8),  GPIO_OUT_LOW)
GPIO(DEMUX_DP_HDMI_MODE,            PIN(D, 10), GPIO_OUT_LOW)
GPIO(DEMUX_DUAL_DP_RESET_N,         PIN(B, 14), GPIO_ODR_HIGH)
GPIO(DEMUX_DUAL_DP_PD_N,            PIN(D, 2),  GPIO_ODR_HIGH)
GPIO(DEMUX_DP_HDMI_PD_N,            PIN(D, 9),  GPIO_ODR_HIGH)

/* USBC Mux and Demux Signals */
GPIO(EN_DP_SINK,                    PIN(B, 2),  GPIO_OUT_LOW)
GPIO(DP_SINK_RESET,                 PIN(B, 1),  GPIO_OUT_LOW)
GPIO(USBC_DP_PD_RST_L,              PIN(C, 13), GPIO_ODR_LOW)
GPIO(USBC_UF_RESET_L,               PIN(C, 11), GPIO_ODR_LOW)

/* USB Hubs signals */
GPIO(EC_HUB1_RESET_L,               PIN(E, 13), GPIO_ODR_LOW)
GPIO(EC_HUB2_RESET_L,               PIN(E, 14), GPIO_ODR_LOW)

/* Misc Signals */
UNIMPLEMENTED(EC_ENTERING_RW)
UNIMPLEMENTED(EC_WP_L)

/* UART */

