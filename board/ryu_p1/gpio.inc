/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
GPIO(CHGR_ACOK,            A, 2,  GPIO_INT_BOTH | GPIO_PULL_UP,    vbus_evt)
GPIO(BC_TEMP_ALERT_L,      A, 13, GPIO_INT_FALLING,                unhandled_evt)
GPIO(POWER_BUTTON_L,       C, 13, GPIO_INT_BOTH,                   power_button_interrupt) /* active high, the name is for compatibility with existing code */
GPIO(USBC_BC12_INT_L,      D, 11, GPIO_INT_FALLING | GPIO_PULL_UP, unhandled_evt)
GPIO(LID_OPEN,             E, 1,  GPIO_INT_BOTH | GPIO_PULL_UP,    lid_interrupt)
GPIO(CHARGE_DONE,          E, 6,  GPIO_INT_BOTH,                   inductive_charging_interrupt)
GPIO(LB_INT_L,             E, 7,  GPIO_INT_FALLING | GPIO_PULL_UP, unhandled_evt)
GPIO(LIGHTBAR_EN_L,        E, 8,  GPIO_INT_FALLING | GPIO_PULL_UP, unhandled_evt)
GPIO(AP_IN_SUSPEND,        E, 9,  GPIO_INT_BOTH,                   power_signal_interrupt)
GPIO(BASE_PRES_L,          E, 10, GPIO_INT_BOTH | GPIO_PULL_UP,    unhandled_evt)
GPIO(AP_HOLD,              E, 14, GPIO_INT_BOTH, power_signal_interrupt)

/* Buttons */
GPIO(BTN_VOLD_L,           C, 0,  GPIO_INPUT | GPIO_PULL_UP,       NULL)
GPIO(BTN_VOLU_L,           C, 1,  GPIO_INPUT | GPIO_PULL_UP,       NULL)

/* PD RX/TX */
GPIO(USBC_CC1_PD,          A, 1,  GPIO_ANALOG,   NULL)
GPIO(USBC_CC2_PD,          A, 3,  GPIO_ANALOG,   NULL)
GPIO(USBC_CC_EN,           A, 4,  GPIO_OUT_LOW,  NULL)
GPIO(USBC_CC_TX_DATA,      A, 6,  GPIO_OUT_LOW,  NULL)
GPIO(USBC_CC_TX_EN,        D, 7,  GPIO_OUT_LOW,  NULL)

#if 0
/* Alternate functions */
GPIO(USBC_TX_CLKOUT,       B, 1,  GPIO_OUT_LOW,  NULL)
GPIO(USBC_TX_CLKIN,        B, 3,  GPIO_OUT_LOW,  NULL)
#endif

/* System power */
GPIO(PMIC_PWRON_L,         D, 14, GPIO_ODR_HIGH, NULL)
GPIO(PMIC_WARM_RESET_L,    E, 4,  GPIO_ODR_HIGH, NULL)
GPIO(EN_PP3300_RSVD,       E, 13, GPIO_INPUT,    NULL)
/* sensor temp output and PMIC reset input */
GPIO(PMIC_THERM_L,         D, 12, GPIO_ODR_HIGH, NULL)

GPIO(VBUS_SENSE,           A, 0,  GPIO_ANALOG,   NULL)
GPIO(CHGR_IADP,            B, 0,  GPIO_ANALOG,   NULL)
GPIO(CHGR_IBAT,            C, 3,  GPIO_ANALOG,   NULL)

/* Inductive charging */
GPIO(CHARGE_EN,            D, 13, GPIO_OUT_LOW,  NULL)
GPIO(BASE_CHG_VDD_EN,      E, 5,  GPIO_OUT_LOW,  NULL)

/* USB-C Power and muxes control */
GPIO(USBC_CHARGE_EN_L,     A, 7,  GPIO_OUT_LOW,  NULL)
GPIO(USBC_5V_EN,           A, 10, GPIO_OUT_LOW,  NULL)
GPIO(USBC_VCONN1_EN_L,     F, 10, GPIO_OUT_HIGH, NULL)
GPIO(USBC_VCONN2_EN_L,     D, 10, GPIO_OUT_HIGH, NULL)

GPIO(USBC_CC1_DEVICE_ODL,  A, 5,  GPIO_ODR_LOW,  NULL)
GPIO(USBC_CC2_DEVICE_ODL,  F, 9,  GPIO_ODR_LOW,  NULL)

GPIO(USBC_DP_MODE_L,       D, 1,  GPIO_OUT_HIGH, NULL)
GPIO(USBC_DP_POLARITY,     D, 2,  GPIO_OUT_HIGH, NULL)
GPIO(USBC_SS1_USB_MODE_L,  D, 3,  GPIO_OUT_HIGH, NULL)
GPIO(USBC_SS2_USB_MODE_L,  D, 5,  GPIO_OUT_HIGH, NULL)
GPIO(USBC_SS_EN_L,         D, 6,  GPIO_OUT_HIGH, NULL)

/* Inputs */
GPIO(BOARD_ID0,            E, 11,  GPIO_INPUT,    NULL)
GPIO(BOARD_ID1,            E, 12,  GPIO_INPUT,    NULL)
GPIO(SH_SIGNAL,            E, 2,   GPIO_INPUT,    NULL)

/* Lightbar reset */
GPIO(LB_RST_L,             D, 15, GPIO_ODR_HIGH | GPIO_PULL_UP, NULL)

#if 0
/* Alternate functions */
GPIO(USB_DM,               A, 11, GPIO_ANALOG,   NULL)
GPIO(USB_DP,               A, 12, GPIO_ANALOG,   NULL)
GPIO(UART_TX,              A, 14, GPIO_OUT_LOW,  NULL)
GPIO(UART_RX,              A, 15, GPIO_OUT_LOW,  NULL)
#endif

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(MASTER_I2C_SCL,       B, 8,  GPIO_INPUT,    NULL)
GPIO(MASTER_I2C_SDA,       B, 9,  GPIO_INPUT,    NULL)
GPIO(SLAVE_I2C_SCL,        B, 10, GPIO_INPUT,    NULL)
GPIO(SLAVE_I2C_SDA,        B, 11, GPIO_INPUT,    NULL)

/* SCL gating for PI3USB9281 */
GPIO(PERICOM_CLK_EN,       C, 15, GPIO_OUT_LOW,  NULL)

/* Case closed debugging. */
GPIO(PD_DISABLE_DEBUG,     C, 5,  GPIO_OUT_HIGH, NULL)
GPIO(SPI_FLASH_NSS,        B, 12, GPIO_INPUT,    NULL)
GPIO(SPI_FLASH_SCK,        B, 13, GPIO_INPUT,    NULL)
GPIO(SPI_FLASH_MOSI,       B, 14, GPIO_INPUT,    NULL)
GPIO(SPI_FLASH_MISO,       B, 15, GPIO_INPUT,    NULL)
GPIO(VDDSPI_EN,            C, 12, GPIO_OUT_LOW,  NULL)
GPIO(SH_RESET_L,           C, 4,  GPIO_ODR_HIGH, NULL)
GPIO(SH_BOOT,              C, 9,  GPIO_ODR_HIGH, NULL)
GPIO(EC_INT_L,             F, 2,  GPIO_ODR_HIGH, NULL)
GPIO(ENTERING_RW,          F, 3,  GPIO_OUT_LOW,  NULL)
GPIO(WP_L,                 F, 6,  GPIO_INPUT,    NULL)

#if 0
/* Alternate functions */
GPIO(SH_UART_TX,           C, 11, GPIO_OUT_LOW,  NULL)
GPIO(SH_UART_RX,           C, 10, GPIO_INPUT,    NULL)
GPIO(AP_UART_TX,           B, 6,  GPIO_OUT_LOW,  NULL)
GPIO(AP_UART_RX,           B, 7,  GPIO_INPUT,    NULL)
#endif

UNIMPLEMENTED(AP_RESET_L)

ALTERNATE(B, 0x0008, 0, MODULE_USB_PD, 0)            /* SPI1: SCK(PB3) */
ALTERNATE(B, 0x0002, 0, MODULE_USB_PD, 0)            /* TIM14_CH1: PB1 */
ALTERNATE(B, 0x00C0, 0, MODULE_UART,   0)            /* USART1: PB6/PB7 */
ALTERNATE(A, 0xC000, 1, MODULE_UART,   GPIO_PULL_UP) /* USART2: PA14/PA15 */
ALTERNATE(C, 0x0C00, 1, MODULE_UART,   0)            /* USART3: PC10/PC11 */
ALTERNATE(B, 0x0F00, 1, MODULE_I2C,    0)            /* I2C SLAVE:PB10/11 MASTER:PB8/9 */
