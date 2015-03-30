/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
GPIO_INT(CHGR_ACOK,            D, 4,  GPIO_INT_BOTH | GPIO_PULL_UP,    vbus_evt)
GPIO_INT(POWER_BUTTON_L,       C, 13, GPIO_INT_BOTH,                   power_button_interrupt) /* active high, the name is for compatibility with existing code */
GPIO_INT(USBC_BC12_INT_L,      D, 11, GPIO_INT_FALLING | GPIO_PULL_UP, usb_evt)
GPIO_INT(LID_OPEN,             E, 1,  GPIO_INT_BOTH | GPIO_PULL_UP,    lid_interrupt)
GPIO_INT(CHARGE_DONE,          E, 6,  GPIO_INT_BOTH,                   inductive_charging_interrupt)
GPIO_INT(AP_IN_SUSPEND,        F, 9,  GPIO_INT_BOTH,                   power_signal_interrupt)
GPIO_INT(AP_HOLD,              E, 3,  GPIO_INT_BOTH,                   power_signal_interrupt)
/*
 * TODO(crosbug.com/p/38689) Workaround for MAX77620 PMIC PP3300 issue
 * Put back as GPIO_ODR_HIGH for P6+
 */
GPIO_INT(HPD_IN,               C, 1,  GPIO_INT_BOTH,                   pp1800_on_off_evt)

/* Interrupt lines not used yet */
GPIO(BC_TEMP_ALERT_L,      C, 5,  GPIO_INT_FALLING)
GPIO(LB_INT_L,             C, 14, GPIO_INT_FALLING | GPIO_PULL_UP)
GPIO(LIGHTBAR_EN_L,        E, 8,  GPIO_INT_FALLING | GPIO_PULL_UP)
GPIO(BASE_PRES_L,          E, 10, GPIO_INT_BOTH | GPIO_PULL_UP)
GPIO(CHGR_INT_L,           B, 2,  GPIO_INT_FALLING)
GPIO(CAM_SYNC_INT_L,       C, 7,  GPIO_INT_FALLING)
GPIO(ACC_IRQ,              D, 12, GPIO_INPUT)
GPIO(COMPASS_DRDY,         A, 8,  GPIO_INPUT)

/* Buttons */
GPIO(BTN_VOLD_L,           C, 0,  GPIO_INPUT | GPIO_PULL_UP)
GPIO(BTN_VOLU_L,           A, 2,  GPIO_INPUT | GPIO_PULL_UP)

/* PD RX/TX */
GPIO(USBC_CC1_PD,          A, 1,  GPIO_ANALOG)
GPIO(USBC_CC2_PD,          A, 3,  GPIO_ANALOG)
GPIO(USBC_CC_EN,           A, 4,  GPIO_OUT_LOW)
GPIO(USBC_CC_TX_DATA,      A, 6,  GPIO_OUT_LOW)
GPIO(USBC_CC_TX_EN,        D, 7,  GPIO_OUT_LOW)

#if 0
/* Alternate functions */
GPIO(USBC_TX_CLKOUT,       B, 1,  GPIO_OUT_LOW)
GPIO(USBC_TX_CLKIN,        B, 3,  GPIO_OUT_LOW)
#endif

/* System power */
GPIO(PMIC_PWRON_L,         D, 14, GPIO_ODR_HIGH)
GPIO(PMIC_WARM_RESET_L,    E, 4,  GPIO_ODR_HIGH)
/*
 * We are missing an external pull-up for EN_PP3300.
 * This GPIO is used to pull it up through an external 100kOhm.
 * EN_PP3300 is still controlled by PMIC though.
 */
GPIO(EN_PP3300_RSVD,       E, 13, GPIO_OUT_LOW)
/* sensor temp output and PMIC reset input */
GPIO(PMIC_THERM_L,      B, 8,  GPIO_ODR_HIGH)

GPIO(VBUS_SENSE,           A, 0,  GPIO_ANALOG)
GPIO(CHGR_PSEL,            B, 0,  GPIO_OUT_LOW)
GPIO(CHGR_OTG,             C, 3,  GPIO_OUT_LOW)

/* Inductive charging */
GPIO(CHARGE_EN,            D, 13, GPIO_OUT_LOW)
GPIO(BASE_CHG_VDD_EN,      E, 5,  GPIO_OUT_LOW)

/* USB-C Power and muxes control */
GPIO(USBC_CHARGE_EN_L,     A, 7,  GPIO_OUT_LOW)
GPIO(USBC_VCONN1_EN_L,     F, 10, GPIO_OUT_HIGH)
GPIO(USBC_VCONN2_EN_L,     D, 10, GPIO_OUT_HIGH)

GPIO(USBC_CC1_DEVICE_ODL,  A, 5,  GPIO_ODR_LOW)
GPIO(USBC_CC2_DEVICE_ODL,  E, 14, GPIO_ODR_LOW)
GPIO(USBC_CC_PUEN1,        D, 0,  GPIO_INPUT)
GPIO(USBC_CC_PUEN2,        C, 8,  GPIO_INPUT)

/* Pericom PI3USB30592 mux controls on Proto 5+ */
GPIO(USBC_MUX_CONF0,       D, 3,  GPIO_OUT_LOW)
GPIO(USBC_MUX_CONF1,       D, 9,  GPIO_OUT_LOW)
GPIO(USBC_MUX_CONF2,       E, 0,  GPIO_OUT_LOW)

/* Inputs */
GPIO(BOARD_ID0,            E, 11,  GPIO_INPUT)
GPIO(BOARD_ID1,            E, 12,  GPIO_INPUT)
GPIO(SH_SIGNAL,            E, 2,   GPIO_INPUT)

/* Lightbar reset */
GPIO(LB_RST_L,             D, 15, GPIO_ODR_HIGH | GPIO_PULL_UP)

#if 0
/* Alternate functions */
GPIO(USB_DM,               A, 11, GPIO_ANALOG)
GPIO(USB_DP,               A, 12, GPIO_ANALOG)
GPIO(UART_TX,              D, 5,  GPIO_OUT_LOW)
GPIO(UART_RX,              D, 6,  GPIO_OUT_LOW)
#endif

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(MASTER_I2C_SCL,       A, 15, GPIO_INPUT)
GPIO(MASTER_I2C_SDA,       A, 14, GPIO_INPUT)
GPIO(SLAVE_I2C_SCL,        A, 9,  GPIO_INPUT)
GPIO(SLAVE_I2C_SDA,        A, 10, GPIO_INPUT)

/* SCL gating for PI3USB9281 */
GPIO(PERICOM_CLK_EN,       C, 15, GPIO_OUT_HIGH)

/* Case closed debugging. */
GPIO(USB_PU_EN_L,          C, 2,  GPIO_OUT_HIGH)
GPIO(PD_DISABLE_DEBUG,     C, 6,  GPIO_OUT_LOW)
GPIO(SPI_FLASH_NSS,        B, 9,  GPIO_INPUT)
GPIO(VDDSPI_EN,            C, 12, GPIO_OUT_LOW)
GPIO(SH_RESET,             C, 4,  GPIO_ODR_HIGH)
GPIO(SH_BOOT,              C, 9,  GPIO_ODR_HIGH)
GPIO(EC_INT_L,             F, 2,  GPIO_ODR_HIGH)
GPIO(ENTERING_RW,          E, 9,  GPIO_OUT_LOW)
GPIO(WP_L,                 F, 6,  GPIO_INPUT)
GPIO(FW_DEBUG_MODE_L,      D, 1,  GPIO_ODR_HIGH)
GPIO(EC_BOOT_SPI_EN,       F, 4,  GPIO_ODR_HIGH)

#if 0
/* Alternate functions */
GPIO(SH_UART_TX,           C, 11, GPIO_OUT_LOW)
GPIO(SH_UART_RX,           C, 10, GPIO_INPUT)
GPIO(AP_UART_TX,           B, 6,  GPIO_OUT_LOW)
GPIO(AP_UART_RX,           B, 7,  GPIO_INPUT)
#endif

UNIMPLEMENTED(AP_RESET_L)

#define GPIO_ODR_UP	GPIO_OPEN_DRAIN | GPIO_PULL_UP

ALTERNATE(B, 0xC400, 5, MODULE_SPI_MASTER, 0)            /* SPI2: PB10/14/15 */
ALTERNATE(B, 0x0008, 5, MODULE_USB_PD,     0)            /* SPI1: SCK(PB3) */
ALTERNATE(B, 0x0002, 2, MODULE_USB_PD,     0)            /* TIM3_CH4: PB1 */
ALTERNATE(B, 0x00C0, 7, MODULE_USART,      0)            /* USART1: PB6/PB7 */
ALTERNATE(D, 0x0060, 7, MODULE_UART,       GPIO_PULL_UP) /* USART2: PD4/PD5 */
ALTERNATE(C, 0x0C00, 7, MODULE_USART,      GPIO_ODR_UP)  /* USART3: PC10/PC11 */
ALTERNATE(A, 0xC600, 4, MODULE_I2C,        0)            /* I2C SLAVE:PA9/10 MASTER:PA14/15 */
ALTERNATE(A, 0x1800,14, MODULE_USB,        0)            /* USB: PA11/12 */
